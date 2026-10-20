"""Exercise the real SPL selection function with mocked loader/ATF boundaries."""
import pathlib
import shlex
import subprocess
import sys

compiler, source, output = sys.argv[1:]
source, output = pathlib.Path(source), pathlib.Path(output)
output.mkdir(parents=True, exist_ok=True)
# Platform headers only: sd_first.c and its public policy header stay unchanged.
(output / 'platform.h').write_text(r'''
#include <stdint.h>
#include <stddef.h>
typedef uint32_t u32;
u32 readl(unsigned long);
#define __BOOT_H__
typedef enum { UBOOT = 3, ATF = 2 } boot_img_e;
struct img_header { unsigned char bytes[956]; uint32_t reserved[17]; };
typedef struct { uint32_t magic, mode, channel, storage, type; uint8_t is_sd_boot; } boot_mode_info_t;
extern boot_mode_info_t test_mode;
#define BOOT_MODE_INFO_ADDR ((uintptr_t)&test_mode)
#define UBOOT_IMG_HEADER_BASE 0x5c000000UL
#define ATF_IMG_HEADER_BASE 0x4003fc00UL
''')
(output / 'atf.h').write_text(r'''
typedef void (*atf_boot_fn)(unsigned long, unsigned long, unsigned long, unsigned long);
extern unsigned long atf_bl_params;
#define ARM_BL31_PLAT_PARAM_VAL 0
int atf_boot_prepare(unsigned long, unsigned long);
''')
(output / 'mmc.h').write_text('void mmc_sd_probe_begin(void);\nvoid mmc_sd_probe_end(void);\n')
(output / 'printf.h').write_text('#define info(...) ((void)0)\n#define uart_putc(base, ch, timeout) ((void)(ch))\n#define UART0_BASE 0x4880000UL\n')
(output / 'timer.h').write_text('#define TIMER_CURRENT_VALUE 0x4830004UL\n#define TICK2MSEC 24000U\n')
(output / 'trace.h').write_text('#define AX_LOG_STR_ERROR(...) ((void)0)\n')
(output / 'test.c').write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "sd_first.h"
#include "timer.h"
boot_mode_info_t test_mode;
unsigned long atf_bl_params;
static int begun, restored, closed, loads;
static int uboot_result, atf_result;
static int expected_el2;
static u32 ticks;
static int expire_after_load;
u32 readl(unsigned long address) { assert(address == TIMER_CURRENT_VALUE); return ticks; }
static jmp_buf handoff;
void mmc_sd_probe_begin(void) { assert(!begun); begun++; }
void mmc_sd_probe_end(void) { assert(begun && closed); restored++; }
void sd_first_close(void) { closed++; }
int sd_first_load(boot_img_e image, unsigned long address) {
    assert(begun && !restored && !closed);
    assert(!test_mode.is_sd_boot);
    loads++;
    if (expire_after_load == loads) ticks -= SD_FIRST_PROBE_MS * TICK2MSEC;
    if (loads == 1) {
        assert(image == UBOOT && address == UBOOT_IMG_HEADER_BASE);
        return uboot_result;
    }
    assert(loads == 2 && image == ATF && address == ATF_IMG_HEADER_BASE);
    return atf_result;
}
int atf_boot_prepare(unsigned long uboot, unsigned long optee) {
    assert(uboot == UBOOT_IMG_HEADER_BASE + sizeof(struct img_header));
    assert(!optee && test_mode.is_sd_boot && closed == 1 && !restored);
    assert(!expected_el2);
    longjmp(handoff, 1);
}
int atf_boot_prepare_el2(unsigned long uboot, unsigned long optee) {
    assert(uboot == UBOOT_IMG_HEADER_BASE + sizeof(struct img_header));
    assert(!optee && test_mode.is_sd_boot && closed == 1 && !restored);
    assert(expected_el2);
    longjmp(handoff, 1);
}
static void run(int uboot, int atf, int expected_loads, int success, int el2) {
    boot_mode_info_t original = {0x12345678, 2, 0, 0, 1, 0};
    test_mode = original;
    begun = restored = closed = loads = 0;
    ticks = 0x100;  /* Exercise the hardware down-counter's unsigned wrap. */
    uboot_result = uboot;
    atf_result = atf;
    expected_el2 = el2;
    if (!setjmp(handoff)) {
        sd_first_boot();
        assert(!success);
        assert(restored == 1);
        assert(!memcmp(&original, &test_mode, sizeof(original)));
    } else {
        assert(success);
        original.is_sd_boot = 1;
        assert(!memcmp(&original, &test_mode, sizeof(original)));
    }
    assert(begun == 1 && closed == 1 && loads == expected_loads);
    assert(!sd_first_probe_expired());
}
int main(void) {
    struct img_header *header = mmap((void *)UBOOT_IMG_HEADER_BASE, 4096,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(header == (void *)UBOOT_IMG_HEADER_BASE);
    assert(sizeof(*header) == 1024);
    int uboot = UBOOT_IMG_HEADER_BASE + sizeof(struct img_header);
    int atf = ATF_IMG_HEADER_BASE + sizeof(struct img_header);
    run(-1, atf, 1, 0, 0);             /* No SD / no readable U-Boot. */
    run(uboot + 4, atf, 1, 0, 0);      /* Reject an unexpected entry. */
    run(uboot, -1, 2, 0, 0);           /* Missing/invalid ATF. */
    run(uboot, atf + 4, 2, 0, 0);      /* Reject an unexpected ATF entry. */
    run(uboot, atf, 2, 1, 0);          /* Vendor SD remains at EL1. */
    header->reserved[0] = ONEKVM_SD_HEADER_MAGIC;
    header->reserved[1] = ONEKVM_SD_HEADER_ABI;
    header->reserved[2] = ONEKVM_SD_FLAG_EL2;
    run(uboot, atf, 2, 1, 1);          /* Only tagged OneKVM requests EL2. */
    header->reserved[2] = 0;
    run(uboot, atf, 2, 1, 0);          /* Tag alone does not enable EL2. */
    header->reserved[1]++;
    header->reserved[2] = ONEKVM_SD_FLAG_EL2;
    run(uboot, atf, 2, 1, 0);          /* Unknown tag ABI is not an EL2 opt-in. */
    expire_after_load = 1;
    run(uboot, atf, 1, 0, 0);          /* Deadline after U-Boot load. */
    expire_after_load = 2;
    run(uboot, atf, 2, 0, 0);          /* Deadline after ATF load. */
    puts("SD-first SPL: 10 selection/handoff/deadline cases passed");
}
''')
binary = output / 'test-sd-first'
subprocess.run(shlex.split(compiler) + [
    '-std=c99', '-D_GNU_SOURCE', '-Wall', '-Wextra', '-Werror', '-include', str(output / 'platform.h'),
    '-I' + str(output), '-I' + str(source / 'core/include'),
    str(output / 'test.c'), str(source / 'spl/sd_first.c'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
