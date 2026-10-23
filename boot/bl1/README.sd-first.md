# NanoKVM Pro SD-first bootloader candidate

`AX_SPL_SD_FIRST=TRUE` adds a selector to the AX630C NanoKVM Pro **eMMC SPL**.
The original eMMC DDR initialization and its parameter package run first. The
selector then loads the SD card's `uboot.bin` and `atf.img`, enters **SD ATF**, and
starts **SD U-Boot**. It does not use the eMMC U-Boot to load an SD kernel.
A card containing only a kernel and `boot.scr` cannot satisfy this firmware path.

The probe runs for normal eMMC boot, including UDA and all three eMMC boot
partition modes, without changing the physical eMMC boot type. It skips native
ROM-SD mode, other storage modes, and explicit download, recovery or kernel-failure requests.
Missing or rejected SD firmware returns to the original eMMC chain. The selector
does not advance eMMC A/B bookkeeping or save an environment; `is_sd_boot` is
committed only after both SD images pass validation. After entering SD ATF,
failure in SD U-Boot or Linux cannot return to the previous SPL. OS boot-success
fallback is outside this selector's contract.

## Loading and fallback

The SD loader requires complete FAT reads, valid header and payload word
checksums, and the image-check flag. It rejects encrypted SD payloads and bounds
compressed input, output and gzipd geometry before starting decompression.
The complete compressed payload is checked before it can drive gzip DMA.

| File | Package header | Entry | Maximum payload/output |
| --- | --- | --- | --- |
| `uboot.bin` | `0x5c000000` | `0x5c000400` | 2 MiB |
| `atf.img` | `0x4003fc00` | `0x40040000` | 256 KiB |

The 1500 ms probe deadline uses the raw hardware down-counter with wrap-safe
subtraction. MMC inhibit, command, data and gzip completion polls observe it.
Cleanup is separately bounded. It unmounts FAT, stops SD DMA, resets gzipd,
and restores the eMMC MMC software context plus SD clocks and pad routing.
SD initialization sets up its own clock and 3.3 V pads instead of assuming ROM
already initialized the card. It does not use the Desk screen's GPIO1_A11 as
card detection or alter eMMC's clock bank during the probe.

The SD ATF parameter chain has no BL32/OP-TEE entry. An ordinary vendor SD
package receives BL33 at **EL1**, as does the original eMMC chain. Only an
explicitly tagged OneKVM SD U-Boot package requests EL2:

```c
header.reserved[0] = 0x4d564b4f; /* bytes "OKVM" */
header.reserved[1] = 1;          /* tag ABI */
header.reserved[2] = 1;          /* bit 0: EL2 requested */
```

Packaging must recompute the header checksum after setting the tag. This tag
is an opt-in, not a cryptographic identity. The selected U-Boot must preserve
EL2 when starting Linux; tagging stock U-Boot is insufficient. Vendor secure
boot/fuse enforcement remains a separate platform policy.

## Isolated OneKVM SD U-Boot

`boot/uboot/u-boot-2020.04/configs/onekvm_nanokvm_pro_sd_defconfig` selects
`CONFIG_ONEKVM_SD_BOOT`. The vendor eMMC configurations leave it disabled.
This image:

- Describes the Pro's actual 1 GiB RAM, reserving the first 4 KiB.
- Preserves EL2 and uses a RAM-only environment, without `saveenv` or `eraseenv`.
- Skips factory A/B/environment bookkeeping and factory kernel arguments.
- Boots with `onekvm_sd_boot`, which selects SD MMC 1 and loads `boot.scr`.
- Limits a legacy boot script to 64 KiB, rejects a truncated payload before
  `source`, and leaves CRC verification to the existing script loader.
- Sets `initrd_high=ffffffffffffffff` to keep the ramdisk at its loaded address.
- Does not program the persistent eMMC reset-enable flag or run factory updater
  commands during its startup.

The board retains the vendor's `CONFIG_UBOOT_SKIP_RELOCATE=y`. Its code runs at
`0x5c000400`; the reservation/relocation address printed by `bdinfo` does **not**
mean the code was moved there. Never TFTP a replacement onto that address while
factory U-Boot is executing. The lab RAM trampoline stages a padded 2 MiB image
at `0x52000000`, runs at `0x50000000`, copies to the link address, and jumps
without returning. Disable caches/MMU before this test. RAM chainloading from
factory EL1 cannot itself prove the EL2 handoff; the ROM-SD test covers that.

## OpenEmbedded build and lab packaging

The common MMIO accessors must order normal memory as well as volatile device
registers. They use an ARM `dmb sy` and compiler memory clobber before writes and
after reads. Without this, GCC LTO removes the checksum DMA's stack descriptor
initialization entirely, allowing DMA to interpret stale stack bytes as addresses
and commands. This caused layout-dependent DDR failures, corrupted chip metadata,
and failed warm reboots. Keep these barriers when optimizing SPL; a volatile
register access alone is insufficient. The linked ELF must retain the descriptor
stores before the DMA trigger, as well as the barriers around device completion.

Use the adjacent OneKVM distro repository, not a standalone SDK toolchain:

```sh
make ONEKVM_MACHINE=onekvm-nanokvm-pro build image sd
```

The SPL recipe builds independent `baseline`, `sd-first` and `rom-test`
directories. It uses GNU11 freestanding GCC, LTO, section GC and the Pro partition
offsets in `spl/nanokvm-pro-config.mak`, retaining the original DDR package.

Each 128 KiB signed SPL package has a 1 KiB header and EIP firmware at `0xcc00`:
**executable payload must fit in `0xc800` (50 KiB)**. A 128 KiB payload limit would
overwrite EIP firmware. Deployment checks this boundary and the end of OCM BSS.
Unsigned payloads, ELF/maps and manifests are deployed under `ax620e-spl/`;
the isolated U-Boot and its configuration are under `ax620e-uboot/`.
These component deploy outputs are not directly flashable signed firmware.
The image depends on `onekvm-nanokvm-pro-sd-firmware`, which also builds
`ax620e-atf` and runs `tools/package-sd-firmware.py` inside OE. It verifies the
component manifests, signs the ROM-SD SPL, compresses and signs SD U-Boot and
BL31, adds the U-Boot EL2 tag, and verifies signatures and loader bounds.
The resulting `onekvm-pro-sd-firmware/firmware.json` records source revisions,
sizes and hashes beside `boot.bin`, `uboot.bin` and `atf.img`.

The optional `bitbake -c test_package ax620e-spl` task runs **inside OE** after
both firmware deploy checks. It requires explicit `AX620E_TEST_SIGNING=1`, an
official SD ATF input in `AX620E_TEST_ATF`, and its exact SHA-256 in
`AX620E_TEST_ATF_SHA256`. Use a local kas fragment with the complete SDK debug
worktree for these lab inputs. The owned `tools/package-test-firmware.py` uses
the SDK's test signing tools/key, validates package boundaries, word checksums
and signatures, tags only the owned SD U-Boot, and copies official ATF unchanged.
Private keys are not copied to output or logged. Output stays in
`${B}/test-package/`; this task performs no device storage writes.

`boot-rom-test.bin` is **only a lab SD `boot.bin`; never flash it to eMMC**.
It requires ROM-SD entry, freshly enumerates eMMC at 50 MHz, and loads the
original eMMC DDR parameter package. A DDR read failure stops the test instead
of reusing RAM from a previous boot. Before probing, it switches SD CMD to GPIO
so hardware acceptance also exercises the selector's pad setup. These lab-only
changes are absent from the normal eMMC SD-first SPL.

## Verification

The OE build tests selector deadlines, ATF privilege handoff, media corruption,
loader bounds, stuck hardware polls, and MMC clock/pad restoration with mocked
hardware. Hardware acceptance must cover cold boot, warm reboot, missing or
corrupt SD firmware, factory EL1 fallback, and OneKVM EL2 with both KVM vCPUs.

The complete SD image includes all three source-built boot packages in FAT;
no previous image or external firmware archive is needed. Its ROM-SD `boot.bin`
still reads the factory eMMC DDR parameter package. Enabling SD priority on a
factory system is a separate primary eMMC SPL update and requires a
board-specific backup and ROM recovery path.
