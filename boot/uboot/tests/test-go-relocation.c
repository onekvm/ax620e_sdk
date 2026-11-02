/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include "go_reloc.h"

/* Decode the page-relative part of an ADRP after moving the instruction. */
static unsigned long moved_adrp(unsigned long pc, unsigned long target,
				unsigned long delta)
{
	return ((pc + delta) & ~4095UL) +
		(target & ~4095UL) - (pc & ~4095UL);
}

int main(void)
{
	const unsigned long text = 0x44000400UL;
	const unsigned long top = 0x4eff0000UL;
	const unsigned long monitor = 0xfa7a0UL;
	const unsigned long old = 0x4eef5000UL;
	unsigned long dest = go_align_relocaddr(top - monitor, text);
	unsigned long residue;

	/* Geometry and real board_init_r ADRPs from the failed first boot. */
	assert(dest == 0x4eef5400UL);
	assert(dest + monitor <= top);
	assert(moved_adrp(0x4401aab8UL, 0x440c6000UL, old - text) !=
	       0x440c6000UL + old - text);
	assert(moved_adrp(0x4401aab8UL, 0x440c6000UL, dest - text) ==
	       0x440c6000UL + dest - text);
	assert(moved_adrp(0x4401aafcUL, 0x440b0000UL, dest - text) ==
	       0x440b0000UL + dest - text);

	/* Every ceiling residue, including a ceiling below the payload offset. */
	for (residue = 0; residue < 4096; ++residue) {
		unsigned long ceiling = 0x4eef5000UL + residue;
		dest = go_align_relocaddr(ceiling, text);
		assert(dest <= ceiling && ceiling - dest < 4096);
		assert(((dest - text) & 4095UL) == 0);
	}
	/* Keep page-aligned payloads working as well. */
	assert(go_align_relocaddr(0x4eef5860UL, 0x44000000UL) == old);
	return 0;
}
