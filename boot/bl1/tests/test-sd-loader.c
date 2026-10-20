/* Real boot.c loader; only media, crypto hardware, and gzipd I/O are mocked. */
#include "boot.h"
#include "sd_first.h"
#include "ax_gzipd_api.h"
#include "secure.h"
#include "ff.h"
#include "timer.h"
#include "trace.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

int hash_digest[8], aes_value[8];
static unsigned char file[8192];
static u32 file_size, cursor;
static int mount_error, short_read, reads, configs, cfg_error, run_error;
static int expired, expire_on_read, cleanup_count;

int sd_first_probe_expired(void) { return expired; }
void udelay(u32 us) { (void)us; }
u32 readl(unsigned long addr) { (void)addr; return 0; }
void writel(u32 val, unsigned long addr) { (void)val; (void)addr; }
int axi_dma_word_checksum(u32 *out, u64 addr, int size) {
	(void)out; (void)addr; (void)size; return -1; /* Exercise real CPU checksum. */
}
void *ax_memcpy(void *dst, const void *src, u32 size) { return __builtin_memcpy(dst, src, size); }
void *ax_memset(void *dst, u8 val, u32 size) { return __builtin_memset(dst, val, size); }
enum AX_LOG_LEVEL ax_log_level;
void ax_print_str(char *s) { (void)s; }
void ax_print_num(unsigned long n, unsigned int radix) { (void)n; (void)radix; }
int emmc_init(u32 type, u32 clk, u32 width, u8 part) {
	(void)type; (void)clk; (void)width; (void)part; assert(0); return -1;
}
int emmc_read(char *out, u32 offset, int size) { (void)out; (void)offset; (void)size; assert(0); return -1; }
int sd_init(u32 clk, u32 width) { (void)clk; (void)width; return 0; }
void sdhci_sd_probe_stop(void) { cleanup_count++; }
int is_secure_enable(void) { return 0; }
int get_aes_key(int *key) { (void)key; assert(0); return -1; }
SECURE_STAUS_T public_key_verify(unsigned long key, int size) { (void)key; (void)size; assert(0); return -1; }
int cipher_aes_ecb_decrypto(int *key, unsigned long src, unsigned long dst, int size) {
	(void)key; (void)src; (void)dst; (void)size; assert(0); return -1;
}
int cipher_sha256(const char *data, char *out, int size) { (void)data; (void)out; (void)size; assert(0); return -1; }
int cipher_rsa_verify(char *key, char *sig, int *hash, int bits) {
	(void)key; (void)sig; (void)hash; (void)bits; assert(0); return -1;
}
FRESULT f_mount(FATFS *fs, const TCHAR *path, BYTE opt) {
	(void)path; (void)opt; return fs && mount_error ? FR_DISK_ERR : FR_OK;
}
FRESULT f_open(FIL *fil, const TCHAR *path, BYTE mode) {
	(void)mode;
	assert(!strcmp(path, "0:uboot.bin") || !strcmp(path, "0:atf.img"));
	fil->obj.objsize = file_size; cursor = 0; return FR_OK;
}
FRESULT f_close(FIL *fil) { (void)fil; return FR_OK; }
FRESULT f_lseek(FIL *fil, FSIZE_t offset) { (void)fil; cursor = offset; return FR_OK; }
FRESULT f_read(FIL *fil, void *out, UINT size, UINT *actual) {
	(void)fil;
	reads++;
	assert(cursor + size <= file_size);
	*actual = short_read && reads == 2 ? size - 1 : size;
	__builtin_memcpy(out, file + cursor, *actual);
	if (expire_on_read == reads) expired = 1;
	return FR_OK;
}
int gzipd_dev_init(void) { return 0; }
int gzipd_dev_get_header_info(void *input, gzipd_header_info_t *header) {
	__builtin_memcpy(header, input, sizeof(*header));
	return header->type[0] != '2' || header->type[1] != '0';
}
int gzipd_dev_cfg(u64 tile, void *out, u64 input, u64 output, u32 blocks, u32 *count, u32 *last) {
	(void)tile; (void)out; (void)output; (void)blocks;
	configs++; *count = 0; *last = input - sizeof(gzipd_header_info_t);
	return cfg_error;
}
int gzipd_dev_run(void *start, void *end, u32 *queued) { (void)start; (void)end; *queued = 1; return run_error; }
int gzipd_dev_run_last_tile(void *input, u64 size) { (void)input; (void)size; return run_error; }
int gzipd_dev_wait_complete_finish(void) { return 0; }
int gzipd_dev_get_fifo_level(void) { return 0; }

static struct img_header *package(void) { return (struct img_header *)file; }
static gzipd_header_info_t *compressed(void) { return (void *)(file + sizeof(struct img_header)); }
static void checksum(void) {
	package()->img_check_sum = calc_word_chksum((void *)compressed(), package()->img_size);
	package()->check_sum = calc_word_chksum((void *)&package()->capability, sizeof(struct img_header) - 8);
}
static void reset_case(void) {
	__builtin_memset(file, 0, sizeof(file));
	file_size = 1024 + 4096;
	package()->magic_data = IMG_HEADER_MAGIC_DATA;
	package()->capability = IMG_CHECK_ENABLE;
	package()->img_size = 4096;
	compressed()->type[0] = '2'; compressed()->type[1] = '0';
	compressed()->isize = 4096; compressed()->osize = 8192; compressed()->blk_num = 1;
	mount_error = short_read = reads = configs = cfg_error = run_error = 0;
	expired = expire_on_read = cleanup_count = 0;
	checksum();
}
static void check(int success, int dma_configs, boot_img_e image) {
	unsigned long base = image == UBOOT ? 0x5c000000UL : 0x4003fc00UL;
	int result = sd_first_load(image, base);
	assert(success ? result == base + 1024 : result < 0);
	assert(configs == dma_configs);
	sd_first_close();
	assert(cleanup_count == 1);
}
static void map_region(unsigned long address, u32 size) {
	void *p = mmap((void *)address, size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	assert(p == (void *)address);
}
int main(void) {
	map_region(0x03000000, 4096);
	map_region(0x5c000000, 0x210000);
	map_region(0x4003f000, 0x50000);
	map_region(0x58000000, 0x210000);
	reset_case(); check(1, 1, UBOOT);
	reset_case(); check(1, 1, ATF);
	reset_case(); mount_error = 1; check(0, 0, UBOOT);
	reset_case(); file_size = 512; check(0, 0, UBOOT);
	reset_case(); file_size--; check(0, 0, UBOOT);
	reset_case(); short_read = 1; check(0, 0, UBOOT);
	reset_case(); package()->magic_data = 0; checksum(); check(0, 0, UBOOT);
	reset_case(); package()->check_sum++; check(0, 0, UBOOT);
	reset_case(); package()->capability = 0; checksum(); check(0, 0, UBOOT);
	reset_case(); package()->capability |= IMG_CIPHER_ENABLE; checksum(); check(0, 0, UBOOT);
	reset_case(); package()->img_size = 0xfffffffc; package()->check_sum = calc_word_chksum((void *)&package()->capability, 1016); check(0, 0, UBOOT);
	reset_case(); file[1200]++; check(0, 0, UBOOT);
	reset_case(); compressed()->osize = SD_FIRST_UBOOT_MAX + 1; checksum(); check(0, 0, UBOOT);
	reset_case(); compressed()->osize = SD_FIRST_ATF_MAX + 1; checksum(); check(0, 0, ATF);
	reset_case(); compressed()->isize = sizeof(gzipd_header_info_t); checksum(); check(0, 0, UBOOT);
	reset_case(); compressed()->isize = 4097; checksum(); check(0, 0, UBOOT);
	reset_case(); compressed()->blk_num = 0; checksum(); check(0, 0, UBOOT);
	reset_case(); compressed()->blk_num = 0x8000; checksum(); check(0, 0, UBOOT);
	reset_case(); expired = 1; check(0, 0, UBOOT);
	reset_case(); expire_on_read = 2; check(0, 0, UBOOT);
	reset_case(); cfg_error = -1; check(0, 1, UBOOT);
	reset_case(); run_error = -1; check(0, 1, UBOOT);
	puts("SD-first SPL: 22 real loader/media/corruption/length/deadline cases passed");
	return 0;
}
