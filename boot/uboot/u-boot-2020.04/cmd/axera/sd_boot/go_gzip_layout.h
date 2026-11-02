/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef ONEKVM_GO_GZIP_LAYOUT_H
#define ONEKVM_GO_GZIP_LAYOUT_H

/* GZIPD merges a tail shorter than 8 KiB into the previous command. */
static inline int go_gzip_last_range(unsigned long first, unsigned int bytes,
	unsigned long tile, unsigned int count, unsigned long *final,
	unsigned int *last)
{
	unsigned long offset;
	if (!bytes || tile < 8192 || tile % 8192 || count > 16)
		return -1;
	offset = (count ? count - 1 : 0) * tile;
	if (offset >= bytes || bytes - offset >= tile + 8192)
		return -1;
	*final = first + offset;
	*last = bytes - offset;
	return 0;
}
#endif
