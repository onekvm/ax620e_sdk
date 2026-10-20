/* Exercise the real MMC probe setup/restore, including its private state. */
#include "../driver/mmc/mmc.c"
#include <assert.h>
#include <stdio.h>

u32 current_bus_width, current_clock;
static u32 mux, div, enable, pads[SD_PROBE_PAD_COUNT];

void udelay(u32 us) { (void)us; }

u32 readl(unsigned long address)
{
	if (address == FLASH_SYS_GLB_CLK_MUX0) return mux;
	if (address == FLASH_SYS_GLB_BASE + 0xc) return div;
	if (address == FLASH_SYS_GLB_BASE + 4) return enable;
	for (u32 i = 0; i < SD_PROBE_PAD_COUNT; ++i)
		if (address == sd_probe_pads[i].address) return pads[i];
	assert(!"unexpected MMIO read");
	return 0;
}

void writel(u32 value, unsigned long address)
{
	if (address == FLASH_SYS_GLB_CLK_MUX0_SET) { mux |= value; return; }
	if (address == FLASH_SYS_GLB_CLK_MUX0_CLR) { mux &= ~value; return; }
	if (address == FLASH_SYS_GLB_CLK_DIV0_SET) { div |= value; return; }
	if (address == FLASH_SYS_GLB_CLK_DIV0_CLR) { div &= ~value; return; }
	if (address == FLASH_SYS_GLB_CLK_EB0_SET) { enable |= value; return; }
	if (address == FLASH_SYS_GLB_CLK_EB0_CLR) { enable &= ~value; return; }
	for (u32 i = 0; i < SD_PROBE_PAD_COUNT; ++i) {
		if (address == sd_probe_pads[i].address) { pads[i] = value; return; }
	}
	/* In particular, probing SD must never write eMMC's clock bank. */
	assert(!"unexpected MMIO write");
}

static void run(u32 initial_enable)
{
	u32 before[SD_PROBE_PAD_COUNT];
	u32 initial_mux = 0xa5a10055;
	u32 initial_div = 0x02a54321; /* Divider update strobe is idle. */
	mux = initial_mux; div = initial_div; enable = initial_enable;
	for (u32 i = 0; i < SD_PROBE_PAD_COUNT; ++i)
		before[i] = pads[i] = 0xa5060000 + i;
	high_capacity = 0; sd_version = 2; rca = 0x1234;
	current_bus_width = 8; current_clock = 200000000;
	mmc_sd_probe_begin();
	assert((mux & GENMASK(17, 16)) == 3U << 16);
	assert((div & GENMASK(25, 20)) == 1U << 20);
	assert(enable & BIT(9));
	assert(!current_bus_width && !current_clock && !sd_card_initialized);
	assert(readl(0x104f1030) == 0x85); /* CMD pull-up, SD function. */
	assert(readl(0x023020a8) == 0x43); /* Hardware SD power, not a GPIO. */
	high_capacity = 1; sd_version = 1; rca = 0x4321;
	current_bus_width = 4; current_clock = 25000000; sd_card_initialized = 1;
	mmc_sd_probe_end();
	assert(mux == initial_mux && div == initial_div && enable == initial_enable);
	for (u32 i = 0; i < SD_PROBE_PAD_COUNT; ++i) assert(pads[i] == before[i]);
	assert(!high_capacity && sd_version == 2 && rca == 0x1234);
	assert(current_bus_width == 8 && current_clock == 200000000);
	assert(!sd_card_initialized);
}

int main(void)
{
	run(0x104);       /* Initially gated SD card clock. */
	run(0x104 | BIT(9));
	puts("SD-first SPL: MMC context, pads and clocks restored; eMMC clock untouched");
	return 0;
}
