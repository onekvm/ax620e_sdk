"""Explicitly requested development packages; run from an OE task, never flash."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True


def word_sum(data):
    if len(data) % 4:
        raise ValueError('Package checksum input must be word aligned')
    return sum(struct.unpack('<%dI' % (len(data) // 4), data)) & 0xffffffff


def load_module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_package(data, maximum, pubkey, rsa, compressed=False):
    if len(data) < 1024:
        raise ValueError('Missing firmware package header')
    checksum, magic, capability, size = struct.unpack_from('<4I', data)
    if magic != 0x55543322 or checksum != word_sum(data[8:1024]):
        raise ValueError('Bad firmware header checksum/magic')
    if not 0 < size <= maximum or len(data) < 1024 + size:
        raise ValueError('Firmware payload exceeds package boundary')
    payload = data[1024:1024 + size]
    if not capability & (1 << 13) or word_sum(payload) != struct.unpack_from('<I', data, 20)[0]:
        raise ValueError('Bad firmware payload checksum')
    rsa.verify(payload, data[444:700][::-1], pubkey)
    if compressed:
        magic, blocks, output, length, _ = struct.unpack_from('<2sHIII', payload)
        if magic != b'20' or not 0 < blocks <= 0x7fff or not 0 < output <= maximum or not 16 < length <= size:
            raise ValueError('Invalid gzipd payload geometry')
    return size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--spl-dir', type=Path, required=True)
    parser.add_argument('--uboot', type=Path, required=True)
    parser.add_argument('--atf', type=Path, required=True)
    parser.add_argument('--atf-sha256', required=True)
    parser.add_argument('--private-key', type=Path, required=True)
    parser.add_argument('--public-key', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--development-only', action='store_true', required=True)
    args = parser.parse_args()
    args.sdk = args.sdk.resolve()
    sys.path.insert(0, str(args.sdk / 'tools/imgsign'))
    import rsa
    public = rsa.PublicKey.load_pkcs1(args.public_key.read_bytes())
    if public.n.bit_length() != 2048:
        raise ValueError('This NanoKVM Pro test layout requires RSA-2048')
    args.out.mkdir(parents=True, exist_ok=True)
    work = args.out / 'work'
    work.mkdir(exist_ok=True)
    signer = load_module('axera_image_sign', args.sdk / 'build/tools/imgsign/sec_boot_AX620E_sign.py')
    spl_signer = load_module('axera_spl_sign', args.sdk / 'build/tools/imgsign/spl_AX620E_sign.py')
    firmware = args.sdk / 'build/tools/imgsign/eip_ax620e.bin'
    if firmware.stat().st_size > 0x13400:
        raise ValueError('EIP firmware exceeds its reserved package region')
    for policy in ('baseline', 'sd-first', 'rom-test'):
        payload = args.spl_dir / policy / 'sdk/boot/bl1/spl/spl_AX630C_emmc_arm64_k419_sipeed_nanokvm.bin'
        if payload.stat().st_size > 0xc800:
            raise ValueError('SPL overlaps EIP firmware')
        output = args.out / ('boot-rom-test.bin' if policy == 'rom-test' else 'spl-' + policy + '-full.bin')
        if not spl_signer.do_spl(str(payload), str(args.public_key), str(args.private_key),
                               str(output), str(firmware), None, 0x54fafe):
            raise ValueError('SPL signing failed')
        packed = output.read_bytes()
        if len(packed) != 0x40000 or packed[:0x20000] != packed[0x20000:]:
            raise ValueError('Unexpected primary/backup SPL geometry')
        check_package(packed[:0x20000], 0xc800, public, rsa)
        fw_size = struct.unpack_from('<I', packed, 16)[0]
        if packed[0xcc00:0xcc00 + fw_size] != firmware.read_bytes():
            raise ValueError('EIP firmware changed during packaging')
        if policy != 'rom-test':
            (args.out / ('spl-' + policy + '-primary.bin')).write_bytes(packed[:0x20000])
    binary = work / 'u-boot.bin'
    shutil.copyfile(args.uboot, binary)
    raw = binary.read_bytes()
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('RAM U-Boot image exceeds its staging window')
    (args.out / 'u-boot-ram.bin').write_bytes(raw + bytes(2 * 1024 * 1024 - len(raw)))
    subprocess.run([str(args.sdk / 'tools/ax_gzip_tool/ax_gzip'), '-9', str(binary)], check=True)
    compressed = work / 'u-boot_axgzip.bin'
    data = compressed.read_bytes()
    compressed.write_bytes(data + bytes((-len(data)) % 4))
    output = args.out / 'uboot.bin'
    if not signer.make_image(str(compressed), str(args.public_key), str(args.private_key), str(output), 0x54fafe, 2048):
        raise ValueError('U-Boot signing failed')
    data = bytearray(output.read_bytes())
    struct.pack_into('<3I', data, 956, 0x4d564b4f, 1, 1)
    struct.pack_into('<I', data, 0, word_sum(data[8:1024]))
    check_package(data, 2 * 1024 * 1024, public, rsa, compressed=True)
    output.write_bytes(data)
    atf = args.atf.read_bytes()
    if hashlib.sha256(atf).hexdigest() != args.atf_sha256:
        raise ValueError('Vendor SD ATF input hash mismatch')
    # The supplied official package has its own key; validate structure and
    # checksums here without replacing its header, signature, or payload.
    checksum, magic, capability, size = struct.unpack_from('<4I', atf)
    if (magic != 0x55543322 or checksum != word_sum(atf[8:1024]) or
            not capability & (1 << 13) or not 16 <= size <= 256 * 1024 or
            len(atf) < 1024 + size or word_sum(atf[1024:1024 + size]) != struct.unpack_from('<I', atf, 20)[0]):
        raise ValueError('Invalid vendor SD ATF package')
    (args.out / 'atf.img').write_bytes(atf)
    manifest = {'development_only': True, 'key_policy': 'explicit supplied RSA-2048 test key',
                'device': 'NanoKVM Pro / AX630C', 'storage_writes': False,
                'uboot_scope': 'OneKVM SD only', 'sd_bl33_el': 2, 'emmc_bl33_el': 1,
                'probe_budget_ms': 1500, 'files': {}}
    manifest['rom_test_only'] = 'boot-rom-test.bin; SD boot.bin only; never flash to eMMC'
    for file in sorted(args.out.iterdir()):
        if file.is_file() and file.name != 'manifest.json':
            data = file.read_bytes()
            manifest['files'][file.name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
