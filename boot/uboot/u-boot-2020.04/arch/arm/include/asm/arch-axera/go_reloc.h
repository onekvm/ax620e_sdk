/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __AXERA_GO_RELOC_H
#define __AXERA_GO_RELOC_H

/* AArch64 ADRP keeps working only with a page-aligned relocation delta. */
static inline unsigned long go_align_relocaddr(unsigned long ceiling,
					      unsigned long text_base)
{
	return ceiling - ((ceiling - text_base) & 4095UL);
}

#endif
