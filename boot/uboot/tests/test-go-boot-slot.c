/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include "go_boot_slot.h"

int main(void)
{
	char slot = 0;
	assert(!go_parse_boot_slot("A", 1, &slot) && slot == 'A');
	assert(!go_parse_boot_slot("B\n", 2, &slot) && slot == 'B');
	slot = 0;
	assert(go_parse_boot_slot("", 0, &slot) && !slot);
	assert(go_parse_boot_slot("a", 1, &slot) && !slot);
	assert(go_parse_boot_slot("C", 1, &slot) && !slot);
	assert(go_parse_boot_slot("A ", 2, &slot) && !slot);
	assert(go_parse_boot_slot("AB", 2, &slot) && !slot);
	assert(go_parse_boot_slot("A\n\n", 3, &slot) && !slot);
	assert(go_other_slot('A') == 'B' && go_other_slot('B') == 'A');
	return 0;
}
