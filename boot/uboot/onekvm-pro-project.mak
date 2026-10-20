# OE U-Boot input only; no vendor toolchain or userspace SDK is included.
ARCH := arm64
OS_MEM := mem=724M
BOARD_256M_OS_MEM := mem=256M
BOARD_512M_OS_MEM := mem=512M
BOARD_0_5G_OS_MEM := mem=512M
BOARD_1G_OS_MEM := mem=724M
BOARD_2G_OS_MEM := mem=1748M
BOARD_4G_OS_MEM := mem=3796M
KERNEL_BOOTARGS := console=ttyS0,115200n8
RECOVERY_BOOTARGS := console=ttyS0,115200n8
ROOTFS_TYPE := squashfs
ROOTFS_DEV := /dev/mmcblk1p2
UBOOT_SPI2_SIPEED_LOGO := FALSE
