#include "cmn.h"
#include "sdhci_cdns.h"
#include "ax_gzipd_api.h"
#include <assert.h>
#include <stdio.h>

static int scenario, polls;
int sd_first_probe_expired(void) { return ++polls >= 3; }
void udelay(u32 us) { (void)us; }
u32 readl(unsigned long address) {
	if (address == SD_MST1_BASE + SRS_BASE_OFFSET + SDHCI_PSTATE_REG && scenario == 1)
		return SDHCI_DATA_INHIBIT | SDHCI_CMD_INHIBIT;
	return 0;
}
u16 readw(unsigned long address) {
	(void)address;
	return scenario == 3 ? BIT_SDHCI_NORMAL_INT_BUF_RD_READY : 0;
}
u8 readb(unsigned long address) { (void)address; return 0; }
void writel(u32 value, unsigned long address) { (void)value; (void)address; }
void writew(u16 value, unsigned long address) { (void)value; (void)address; }
void writeb(u8 value, unsigned long address) { (void)value; (void)address; }
void *ax_memcpy(void *out, const void *input, u32 size) { return __builtin_memcpy(out, input, size); }
extern int sdhci_transfer_data(void *host, struct mmc_data *data);

int main(void)
{
	void *host = (void *)(SD_MST1_BASE + SRS_BASE_OFFSET);
	struct mmc_cmd cmd = { .cmdidx = MMC_CMD_SEND_STATUS, .resp_type = MMC_RSP_R1 };
	struct mmc_data data = { .flags = MMC_DATA_READ, .blocks = 1, .blocksize = 512 };
	u32 tiles, last;
	scenario = 1; polls = 0;
	assert(sdhci_send_cmd(host, &cmd, NULL) < 0 && polls == 3);
	scenario = 2; polls = 0;
	assert(sdhci_send_cmd(host, &cmd, NULL) < 0 && polls == 3);
	scenario = 3; polls = 0;
	/* PIO ready without a ready buffer used to skip the iteration timeout. */
	assert(sdhci_transfer_data(host, &data) < 0 && polls == 3);
	scenario = 4; polls = 0;
	gzipd_dev_init();
	assert(gzipd_dev_wait_complete_finish() < 0 && polls == 3);
	assert(!gzipd_dev_cfg(64 * 1024, (void *)0x5c000400,
			     sizeof(gzipd_header_info_t) + 64, 128, 1, &tiles, &last));
	assert(tiles == 0 && last == 64); /* A tiny payload must not gain a 64 KiB tail. */
	puts("SD-first SPL: 4 stuck hardware polls bounded; short gzip tile stays in bounds");
	return 0;
}
