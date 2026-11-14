/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef ONEKVM_GO_BOOT_SLOT_H
#define ONEKVM_GO_BOOT_SLOT_H

/* FAT data file naming the A/B slot; Linux replaces it by rename. */
#define GO_BOOT_SLOT_FILE "boot-slot"

/* Accept exactly "A" or "B", optionally followed by one newline. */
static inline int go_parse_boot_slot(const char *data, unsigned long length,
	char *slot)
{
	if (length < 1 || length > 2 || (data[0] != 'A' && data[0] != 'B') ||
	    (length == 2 && data[1] != '\n'))
		return -1;
	*slot = data[0];
	return 0;
}

/* Boot the selected slot first, then the other one. */
static inline char go_other_slot(char slot)
{
	return slot == 'A' ? 'B' : 'A';
}
#endif
