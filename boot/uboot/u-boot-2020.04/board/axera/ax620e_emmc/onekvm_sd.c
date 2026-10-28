// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <command.h>
#include <image.h>

#define ONEKVM_SCRIPT_ADDR 0x48000000UL
#define ONEKVM_SCRIPT_MAX  0x10000UL

static int onekvm_source_bootscr(const char *select, const char *load,
				 const char *unavailable, const char *bad_load,
				 const char *bad_script)
{
	const image_header_t *header = (const image_header_t *)ONEKVM_SCRIPT_ADDR;
	unsigned long size;

	if (run_command(select, 0) || run_command("mmc rescan", 0)) {
		puts(unavailable);
		return CMD_RET_FAILURE;
	}
	/* Limit a boot script to 64 KiB and check each step before sourcing it. */
	if (run_command(load, 0)) {
		puts(bad_load);
		return CMD_RET_FAILURE;
	}
	size = env_get_hex("filesize", 0);
	/* source validates the CRCs; first ensure its complete legacy script is
	 * inside the bytes just read, rather than trailing data left in RAM.
	 */
	if (size < sizeof(*header) || size > ONEKVM_SCRIPT_MAX ||
	    !image_check_magic(header) || !image_check_type(header, IH_TYPE_SCRIPT) ||
	    image_get_data_size(header) > size - sizeof(*header)) {
		puts(bad_script);
		return CMD_RET_FAILURE;
	}
	return run_command("source 0x48000000", 0) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

static int do_onekvm_sd_boot(cmd_tbl_t *cmdtp, int flag, int argc,
			     char *const argv[])
{
	return onekvm_source_bootscr(
		"mmc dev 1",
		"fatload mmc 1:1 0x48000000 boot.scr 0x10000",
		"OneKVM: SD card unavailable\n",
		"OneKVM: cannot load SD boot.scr\n",
		"OneKVM: invalid or truncated SD boot.scr\n");
}

static int do_onekvm_emmc_boot(cmd_tbl_t *cmdtp, int flag, int argc,
			       char *const argv[])
{
	return onekvm_source_bootscr(
		"mmc dev 0",
		"fatload mmc 0:1 0x48000000 boot.scr 0x10000",
		"OneKVM: eMMC unavailable\n",
		"OneKVM: cannot load eMMC boot.scr\n",
		"OneKVM: invalid or truncated eMMC boot.scr\n");
}

U_BOOT_CMD(onekvm_sd_boot, 1, 0, do_onekvm_sd_boot,
	   "boot the OneKVM SD script", "");
U_BOOT_CMD(onekvm_emmc_boot, 1, 0, do_onekvm_emmc_boot,
	   "boot the OneKVM eMMC script", "");
