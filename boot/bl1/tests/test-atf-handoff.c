#include "atf.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
	unsigned long uboot = 0x5c000400;
	atf_boot_prepare(uboot, 0x44200000);
	assert(atf_bl_params.head == &atf_bl33_params);
	assert(atf_bl33_ep_info.pc == uboot);
	assert(atf_bl33_ep_info.spsr == SPSR_64(MODE_EL1, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS));
	assert(atf_bl33_params.next_params_info == &atf_bl32_params);
	assert(atf_bl32_ep_info.pc == 0x44200000);
	atf_boot_prepare(uboot, 0);
	assert(atf_bl33_params.next_params_info == NULL);
	atf_boot_prepare_el2(uboot, 0);
	assert(atf_bl33_params.next_params_info == NULL);
	assert(atf_bl33_ep_info.spsr == SPSR_64(MODE_EL2, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS));
	atf_boot_prepare(uboot, 0x44200000);
	assert(atf_bl33_params.next_params_info == &atf_bl32_params);
	assert(atf_bl33_ep_info.spsr == SPSR_64(MODE_EL1, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS));
	puts("SD-first SPL: real ATF parameters preserve eMMC EL1 and isolate SD EL2");
	return 0;
}
