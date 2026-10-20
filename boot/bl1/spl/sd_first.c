#include "sd_first.h"
#include "atf.h"
#include "mmc.h"
#include "printf.h"
#include "trace.h"
#include "timer.h"

_Static_assert(sizeof(struct img_header) == 1024, "AXERA package header ABI");

static u32 probe_start_ticks;
static int probe_active;

void sd_first_log(const char *message)
{
	/* Keep media decisions visible with the production build's quiet logging. */
	while (*message)
		uart_putc(UART0_BASE, *message++, 100);
}

int sd_first_probe_expired(void)
{
	/* Read the raw down-counter: unsigned subtraction also handles its wrap. */
	return probe_active && (u32)(probe_start_ticks - readl(TIMER_CURRENT_VALUE)) >=
		SD_FIRST_PROBE_MS * TICK2MSEC;
}

void sd_first_boot(void)
{
	boot_mode_info_t *boot_mode = (boot_mode_info_t *)BOOT_MODE_INFO_ADDR;
	atf_boot_fn atf_boot;
	int uboot_addr, atf_addr;

	sd_first_log("SD first: checking SD boot firmware\r\n");
	probe_start_ticks = readl(TIMER_CURRENT_VALUE);
	probe_active = 1;
	mmc_sd_probe_begin();
	uboot_addr = sd_first_load(UBOOT, UBOOT_IMG_HEADER_BASE);
	if (uboot_addr != UBOOT_IMG_HEADER_BASE + sizeof(struct img_header) ||
	    sd_first_probe_expired())
		goto fallback;
	atf_addr = sd_first_load(ATF, ATF_IMG_HEADER_BASE);
	if (atf_addr != ATF_IMG_HEADER_BASE + sizeof(struct img_header))
		goto fallback;
	if (sd_first_probe_expired())
		goto fallback;

	sd_first_close();
	probe_active = 0;
	/* Commit the media choice only after both images have passed validation.
	 * Retain the physical eMMC storage/boot type, as the vendor SD SPL does.
	 * This flag tells SD U-Boot to select its own SD operating system.
	 */
	boot_mode->is_sd_boot = 1;
	if (sd_first_requests_el2((struct img_header *)UBOOT_IMG_HEADER_BASE)) {
		sd_first_log("SD first: OneKVM SD package requests EL2\r\n");
		atf_boot_prepare_el2(uboot_addr, 0);
	} else {
		atf_boot_prepare(uboot_addr, 0);
	}
	sd_first_log("SD first: starting SD U-Boot through SD ATF\r\n");
	atf_boot = (atf_boot_fn)(unsigned long)atf_addr;
	atf_boot((unsigned long)&atf_bl_params, 0, 0, ARM_BL31_PLAT_PARAM_VAL);
	/* After handing control to ATF, no return to the old EL3 state is safe. */
	AX_LOG_STR_ERROR("SD first: ATF unexpectedly returned\r\n");
	while (1);

fallback:
	sd_first_close();
	probe_active = 0;
	mmc_sd_probe_end();
	sd_first_log("SD first: SD firmware unavailable, continue eMMC\r\n");
}
