// SPDX-License-Identifier: GPL-2.0+
/*
 * Synthetic partitions for the OneKVM Pro eMMC layout.
 *
 * Sector 0 is Axera SPL, so there is no MBR. The boot FAT starts at sector
 * 40960 and each of the first three regions is 262144 sectors (128 MiB):
 * boot, rootfs A, rootfs B. Userdata is the remainder. These numbers match
 * onekvm-nanokvm-machine storage layout "pro-emmc". Linux still discovers
 * the same regions from blkdevparts; this table only lets fatload see the FAT.
 * A real DOS or GPT signature at sector 0 keeps the normal partition drivers.
 */

#include <common.h>
#include <memalign.h>
#include <part.h>
#include <linux/string.h>

#define ONEKVM_EMMC_BOOT_LBA		40960ULL
#define ONEKVM_EMMC_SLOT_SECTORS	262144ULL
#define ONEKVM_EMMC_PART_COUNT		4
#define ONEKVM_EMMC_LABEL		"ONEKVMBOOT"

static int onekvm_emmc_label(const unsigned char *buf, int offset)
{
	return memcmp(buf + offset, ONEKVM_EMMC_LABEL,
		      sizeof(ONEKVM_EMMC_LABEL) - 1) == 0;
}

static int part_test_onekvm_emmc(struct blk_desc *dev_desc)
{
	int fat16;
	int fat32;
	ALLOC_CACHE_ALIGN_BUFFER(unsigned char, buf, 512);

	if (!dev_desc || dev_desc->if_type != IF_TYPE_MMC ||
	    dev_desc->devnum != 0 || dev_desc->blksz != 512)
		return -1;
	if (blk_dread(dev_desc, 0, 1, buf) != 1)
		return -1;
	if (buf[510] == 0x55 && buf[511] == 0xaa)
		return -1;
	if (blk_dread(dev_desc, ONEKVM_EMMC_BOOT_LBA, 1, buf) != 1)
		return -1;
	if (!(buf[0] == 0xeb || buf[0] == 0xe9) ||
	    buf[510] != 0x55 || buf[511] != 0xaa)
		return -1;
	fat16 = memcmp(buf + 54, "FAT", 3) == 0 && onekvm_emmc_label(buf, 43);
	fat32 = memcmp(buf + 82, "FAT32", 5) == 0 && onekvm_emmc_label(buf, 71);
	return (fat16 || fat32) ? 0 : -1;
}

static int part_get_info_onekvm_emmc(struct blk_desc *dev_desc, int part,
				     disk_partition_t *info)
{
	lbaint_t start;
	lbaint_t size;

	if (part < 1 || part > ONEKVM_EMMC_PART_COUNT)
		return -1;
	start = ONEKVM_EMMC_BOOT_LBA +
		(lbaint_t)(part - 1) * ONEKVM_EMMC_SLOT_SECTORS;
	if (start >= dev_desc->lba)
		return -1;
	size = (part < ONEKVM_EMMC_PART_COUNT) ?
		ONEKVM_EMMC_SLOT_SECTORS : dev_desc->lba - start;
	if (start + size > dev_desc->lba)
		size = dev_desc->lba - start;
	info->start = start;
	info->size = size;
	info->blksz = dev_desc->blksz;
	info->bootable = part == 1;
	strcpy((char *)info->type, "U-Boot");
	part_set_generic_name(dev_desc, part, (char *)info->name);
#if CONFIG_IS_ENABLED(PARTITION_UUIDS)
	info->uuid[0] = 0;
#endif
	return 0;
}

static void part_print_onekvm_emmc(struct blk_desc *dev_desc)
{
	int part;

	printf("Part\tStart Sector\tNum Sectors\tType\n");
	for (part = 1; part <= ONEKVM_EMMC_PART_COUNT; part++) {
		disk_partition_t info;

		if (part_get_info_onekvm_emmc(dev_desc, part, &info))
			break;
		printf("%d\t" LBAFU "\t" LBAFU "\t%s\n",
		       part, info.start, info.size, info.type);
	}
}

U_BOOT_PART_TYPE(onekvm_emmc) = {
	.name		= "ONEKVM_EMMC",
	.part_type	= PART_TYPE_ONEKVM_EMMC,
	.max_entries	= ONEKVM_EMMC_PART_COUNT,
	.get_info	= part_get_info_ptr(part_get_info_onekvm_emmc),
	.print		= part_print_ptr(part_print_onekvm_emmc),
	.test		= part_test_onekvm_emmc,
};
