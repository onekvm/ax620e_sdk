// SPDX-License-Identifier: GPL-2.0+
#include <common.h>
#include <command.h>
#include <image.h>

#define ONEKVM_SCRIPT_ADDR 0x48000000UL
#define ONEKVM_SCRIPT_MAX  0x10000UL

static int do_onekvm_sd_boot(cmd_tbl_t *cmdtp, int flag, int argc,
			     char *const argv[])
{
	const image_header_t *header = (const image_header_t *)ONEKVM_SCRIPT_ADDR;
	unsigned long size;

	if (run_command("mmc dev 1", 0) || run_command("mmc rescan", 0)) {
		puts("OneKVM: SD card unavailable\n");
		return CMD_RET_FAILURE;
	}
	/* Limit a boot script to 64 KiB and check each step before sourcing it. */
	if (run_command("fatload mmc 1:1 0x48000000 boot.scr 0x10000", 0)) {
		puts("OneKVM: cannot load SD boot.scr\n");
		return CMD_RET_FAILURE;
	}
	size = env_get_hex("filesize", 0);
	/* source validates the CRCs; first ensure its complete legacy script is
	 * inside the bytes just read, rather than trailing data left in RAM.
	 */
	if (size < sizeof(*header) || size > ONEKVM_SCRIPT_MAX ||
	    !image_check_magic(header) || !image_check_type(header, IH_TYPE_SCRIPT) ||
	    image_get_data_size(header) > size - sizeof(*header)) {
		puts("OneKVM: invalid or truncated SD boot.scr\n");
		return CMD_RET_FAILURE;
	}
	return run_command("source 0x48000000", 0) ? CMD_RET_FAILURE : CMD_RET_SUCCESS;
}

U_BOOT_CMD(onekvm_sd_boot, 1, 0, do_onekvm_sd_boot,
	   "boot the OneKVM SD script", "");
