// SPDX-License-Identifier: GPL-2.0+
/* NanoKVM Go: bounded SD capsules, resident BL31 and watchdog handoff. */
#include <common.h>
#include <command.h>
#include <cpu_func.h>
#include <dm.h>
#include <fs.h>
#include <memalign.h>
#include <asm/io.h>
#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/libfdt.h>
#include <linux/sizes.h>
#include "../secureboot/secureboot.h"
#include "../gzipd/ax_gzipd_api.h"
#include "go_boot_slot.h"
#include "go_gzip_layout.h"

#define GO_KERNEL       0x40200000UL
#define GO_KERNEL_END   0x43000000UL
#define GO_DTB          0x43000000UL
#define GO_DTB_END      0x43100000UL
#define GO_KERNEL_ZIP   0x44500000UL
#define GO_KERNEL_ZEND  0x46d00000UL
#define GO_DTB_ZIP      0x44400000UL
#define GO_DTB_ZEND     0x44500000UL

static void go_watchdog_arm(void)
{
	const ulong base = WDT0_BASE;
	const u32 reload = (180ULL * 24000000 / 2 + 65535) >> 16;
	writel(BIT(19), PERI_SYS_GLB_CLK_MUX0_SET);
	writel(BIT(14), PERI_SYS_GLB_CLK_EB0_SET);
	writel(BIT(19), PERI_SYS_GLB_CLK_EB1_SET);
	writel(0, base + 0x54);
	writel(reload, base + 0x0c);
	writel(1, base + 0x18);
	udelay(1);
	writel(0, base + 0x18);
	writel(1, base + 0x3c);
	writel(1, base);
	writel(0x61696370, base + 0x30);
	udelay(1);
	writel(0, base + 0x30);
}

int onekvm_go_board_init(void)
{
	go_watchdog_arm();
	env_set("bootcmd", "onekvm_go_boot");
	env_set("bootargs", "console=ttyS3,115200n8 earlycon=uart8250,mmio32,0x06080000");
	/* Keep FDT in its checked 1 MiB Linux window. No external initrd. */
	env_set("fdt_high", "ffffffffffffffff");
	env_set("initrd_high", "ffffffffffffffff");
	env_set("bootm_low", "40080000");
	env_set("bootm_size", "7f80000");
	puts("OneKVM Go: ARM64 SD, UART3, hardware watchdog active\n");
	{
		/* Probing the panel draws the OneKVM boot logo; boot continues without it. */
		struct udevice *panel;
		int ret = uclass_first_device_err(UCLASS_PANEL_SPI, &panel);

		if (ret)
			printf("OneKVM Go: boot logo unavailable (%d)\n", ret);
	}
	return 0;
}

static u32 go_word_sum(const void *data, size_t size)
{
	const u32 *word = data;
	u32 sum = 0;
	while (size >= sizeof(*word)) {
		sum += *word++;
		size -= sizeof(*word);
	}
	return sum;
}

static int go_load_capsule(const char *name, ulong address, ulong end, u32 *size)
{
	struct img_header *header = (void *)address;
	loff_t length, actual;
	void *payload = (void *)(address + SECBOOT_HEADER_SIZE);
	ALLOC_CACHE_ALIGN_BUFFER(char, public_key, 396);

	/* fs_size/fs_read close their filesystem context; select it each time. */
	if (fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT) || fs_size(name, &length) ||
	    length <= SECBOOT_HEADER_SIZE || length > end - address ||
	    fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT) ||
	    fs_read(name, address, 0, length, &actual) || actual != length)
		return -1;
	if (header->magic_data != IMG_HEADER_MAGIC_DATA ||
	    header->capability != 0x54fafe ||
	    header->img_size != length - SECBOOT_HEADER_SIZE ||
	    (header->img_size & 3) ||
	    header->check_sum != go_word_sum((void *)(address + 8), SECBOOT_HEADER_SIZE - 8) ||
	    header->img_check_sum != go_word_sum(payload, header->img_size))
		return -1;
	/* Preserve the vendor's fuse-based authentication when it is enabled. */
	if (is_secure_enable()) {
		memset(public_key, 0, 396);
		memcpy(public_key, &header->pub_key.key_n_header, 260);
		memcpy(public_key + 260, &header->pub_key.key_e_header, 8);
		flush_dcache_all();
		if (public_key_verify(public_key, 396) < 0 ||
		    rsa_img_verify(&header->pub_key, payload,
			(char *)header->signature.signature, header->img_size, 2048) < 0)
			return -1;
	}
	*size = header->img_size;
	go_watchdog_arm();
	return 0;
}

static int go_inflate(ulong source, u32 size, ulong destination, ulong end, u32 *raw_size)
{
	gzipd_header_info_t info;
	u32 count, last, queued;
	ulong first, final, tile = (DIV_ROUND_UP(size, SZ_8K) / 12 + 1) * SZ_8K;

	if (size < sizeof(info))
		return -1;
	memcpy(&info, (void *)source, sizeof(info));
	if (memcmp(info.type, "20", 2) || info.isize <= sizeof(info) ||
	    info.isize > size || size - info.isize > 3 || !info.blk_num ||
	    !info.osize || info.osize > end - destination)
		return -1;
	flush_dcache_all();
	if (gzipd_dev_init() || gzipd_dev_get_header_info((void *)source, &info) ||
	    gzipd_dev_cfg(tile, (void *)destination, info.isize, info.osize,
			 info.blk_num, &count, &last))
		return -1;
	first = source + sizeof(info);
	/* The vendor helper's count==0 last-size calculation wraps. */
	if (go_gzip_last_range(first, info.isize - sizeof(info), tile, count,
			       &final, &last))
		return -1;
	if ((count && gzipd_dev_run((void *)first, (void *)final, &queued)) ||
	    gzipd_dev_run_last_tile((void *)final, last) ||
	    gzipd_dev_wait_complete_finish())
		return -1;
	invalidate_dcache_range(destination, ALIGN(destination + info.osize, ARCH_DMA_MINALIGN));
	*raw_size = info.osize;
	go_watchdog_arm();
	return 0;
}

/* Load, inflate and check one kernel/DTB pair; nothing is booted yet. */
static int go_load_images(const char *kernel_name, const char *dtb_name)
{
	u32 kernel_size, dtb_size, raw_kernel, raw_dtb;
	u64 text_offset, image_size, image_flags;
	void *kernel = (void *)GO_KERNEL, *dtb = (void *)GO_DTB;

	if (go_load_capsule(kernel_name, GO_KERNEL_ZIP, GO_KERNEL_ZEND, &kernel_size) ||
	    go_load_capsule(dtb_name, GO_DTB_ZIP, GO_DTB_ZEND, &dtb_size) ||
	    go_inflate(GO_KERNEL_ZIP + SECBOOT_HEADER_SIZE, kernel_size,
		       GO_KERNEL, GO_KERNEL_END, &raw_kernel) ||
	    go_inflate(GO_DTB_ZIP + SECBOOT_HEADER_SIZE, dtb_size,
		       GO_DTB, GO_DTB_END, &raw_dtb))
		return -1;
	if (raw_kernel < 64 || get_unaligned_le32(kernel + 56) != 0x644d5241 ||
	    raw_dtb < 40 || fdt_check_header(dtb) || fdt_totalsize(dtb) > raw_dtb ||
	    raw_dtb > GO_DTB_END - GO_DTB - SZ_64K ||
	    fdt_node_check_compatible(dtb, 0, "sipeed,nanokvm-go"))
		return -1;
	text_offset = get_unaligned_le64(kernel + 8);
	image_size = get_unaligned_le64(kernel + 16);
	image_flags = get_unaligned_le64(kernel + 24);
	/* Reject Image headers that make booti relocate over resident BL31. */
	if (!(image_flags & BIT(3)) || text_offset > GO_KERNEL ||
	    ((GO_KERNEL - text_offset) & (SZ_2M - 1)) ||
	    image_size < raw_kernel || image_size > GO_KERNEL_END - GO_KERNEL)
		return -1;
	return 0;
}

/* 0 with *slot set, -ENOENT without the file, -EINVAL for any other content. */
static int go_read_boot_slot(char *slot)
{
	ALLOC_CACHE_ALIGN_BUFFER(char, data, ARCH_DMA_MINALIGN);
	loff_t length, actual;

	if (fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT) ||
	    fs_size(GO_BOOT_SLOT_FILE, &length))
		return -ENOENT;
	if (length < 1 || length > 2 ||
	    fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT) ||
	    fs_read(GO_BOOT_SLOT_FILE, (ulong)data, 0, length, &actual) ||
	    actual != length || go_parse_boot_slot(data, length, slot))
		return -EINVAL;
	return 0;
}

/* Load *slot's kernel/DTB pair, else the other slot's; *slot names the result. */
static int go_load_slot(char *slot)
{
	char kernel_name[16], dtb_name[16];
	int attempt;

	for (attempt = 0; attempt < 2; ++attempt) {
		const char lower = *slot - 'A' + 'a';

		snprintf(kernel_name, sizeof(kernel_name), "kernel-%c.img", lower);
		snprintf(dtb_name, sizeof(dtb_name), "dtb-%c.img", lower);
		if (!go_load_images(kernel_name, dtb_name))
			return 0;
		printf("OneKVM Go: slot %c boot images invalid or unavailable\n", *slot);
		*slot = go_other_slot(*slot);
	}
	return -1;
}

static int do_onekvm_go_boot(cmd_tbl_t *cmdtp, int flag, int argc, char *const argv[])
{
	char command[80], args[CONFIG_SYS_CBSIZE], slot = 'A';
	const char *bootargs;
	int status, length;

	go_watchdog_arm();
	if (run_command("mmc dev 1", 0) || run_command("mmc rescan", 0))
		goto failed;
	status = go_read_boot_slot(&slot);
	if (status == -ENOENT) {
		/* Without boot-slot keep the single-root kernel.img/dtb.img layout. */
		if (go_load_images("kernel.img", "dtb.img"))
			goto failed;
	} else {
		if (status) {
			puts("OneKVM Go: invalid boot-slot; trying slot A first\n");
			slot = 'A';
		}
		if (go_load_slot(&slot))
			goto failed;
		/* The initramfs mounts this slot's root; RAUC reads the same value. */
		bootargs = env_get("bootargs");
		length = snprintf(args, sizeof(args), "%s rauc.slot=%c",
				  bootargs ? bootargs : "", slot);
		if (length < 0 || length >= (int)sizeof(args) || env_set("bootargs", args))
			goto failed;
		printf("OneKVM Go: booting slot %c\n", slot);
	}
	snprintf(command, sizeof(command), "booti %lx - %lx", GO_KERNEL, GO_DTB);
	return run_command(command, 0);
failed:
	puts("OneKVM Go: invalid or unavailable SD boot images; watchdog remains active\n");
	return CMD_RET_FAILURE;
}

U_BOOT_CMD(onekvm_go_boot, 1, 0, do_onekvm_go_boot,
	   "boot NanoKVM Go ARM64 from SD FAT, slot from boot-slot", "");
