/* SPDX-License-Identifier: GPL-2.0+ */
#include <assert.h>
#include "go_gzip_layout.h"

int main(void)
{
	const unsigned long first = 0x44500410;
	unsigned long final;
	unsigned int last;
	const unsigned int lengths[] = {1, 8191, 8192, 8193, 16383, 16384, 16385};
	const unsigned int counts[] = {0, 0, 1, 1, 1, 2, 2};
	const unsigned int tails[] = {1, 8191, 8192, 8193, 16383, 8192, 8193};
	unsigned int i;
	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
		assert(!go_gzip_last_range(first, lengths[i], 8192, counts[i], &final, &last));
		assert(last == tails[i]);
		assert(final + last == first + lengths[i]);
	}
	assert(go_gzip_last_range(first, 0, 8192, 0, &final, &last));
	assert(go_gzip_last_range(first, 8192, 8193, 1, &final, &last));
	assert(go_gzip_last_range(first, 16384, 8192, 3, &final, &last));
	assert(go_gzip_last_range(first, 16384, 8192, 0, &final, &last));
	assert(go_gzip_last_range(first, 20 * 8192, 8192, 17, &final, &last));
	return 0;
}
