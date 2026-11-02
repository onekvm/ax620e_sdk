/* Bounded boot watchdog; BL31 leaves this running until U-Boot takes over. */
#ifndef NANOKVM_GO_BOOT_H
#define NANOKVM_GO_BOOT_H
#include "cmn.h"
#include "chip_reg.h"
#include "timer.h"

static inline void nanokvm_go_boot_watchdog(void)
{
	const unsigned long base = 0x04840000;
	/* 24 MHz, two countdown stages, 180 seconds in total (rounded up). */
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
#endif
