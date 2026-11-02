# OE-only configuration; Go memory layout is supplied by its DTS.
ARCH := arm64
OS_MEM :=
BOARD_256M_OS_MEM :=
BOARD_512M_OS_MEM :=
BOARD_0_5G_OS_MEM :=
BOARD_1G_OS_MEM :=
BOARD_2G_OS_MEM :=
BOARD_4G_OS_MEM :=
KERNEL_BOOTARGS := console=ttyS3,115200n8 earlycon=uart8250,mmio32,0x06080000
RECOVERY_BOOTARGS := console=ttyS3,115200n8
ROOTFS_TYPE := ext4
ROOTFS_DEV := /dev/mmcblk1p2
UBOOT_SPI2_SIPEED_LOGO := FALSE
