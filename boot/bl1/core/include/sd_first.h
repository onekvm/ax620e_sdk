#ifndef AXERA_SD_FIRST_H
#define AXERA_SD_FIRST_H

#include "boot.h"

/* Only these two images are accepted by the optional eMMC SPL selector. */
#define SD_FIRST_UBOOT_MAX (2U * 1024U * 1024U)
#define SD_FIRST_ATF_MAX   (256U * 1024U)
#define SD_FIRST_PROBE_MS  1500U
#define SD_FIRST_TILE_SIZE (64U * 1024U)

/* Reserved image-header words, covered by the vendor header checksum.
 * Ordinary vendor SD packages have no tag and retain the EL1 handoff.
 */
#define ONEKVM_SD_HEADER_MAGIC 0x4d564b4fU /* "OKVM" */
#define ONEKVM_SD_HEADER_ABI   1U
#define ONEKVM_SD_FLAG_EL2     1U

static inline int sd_first_requests_el2(const struct img_header *header)
{
	return header->reserved[0] == ONEKVM_SD_HEADER_MAGIC &&
	       header->reserved[1] == ONEKVM_SD_HEADER_ABI &&
	       (header->reserved[2] & ONEKVM_SD_FLAG_EL2);
}

int atf_boot_prepare_el2(unsigned long uboot_addr, unsigned long optee_addr);

int sd_first_load(boot_img_e image, unsigned long header_addr);
void sd_first_close(void);
void sd_first_boot(void);
int sd_first_probe_expired(void);
void sd_first_log(const char *message);

#endif
