"""Package OE-built OneKVM SD boot firmware; never perform device writes."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'spl-dir', 'uboot-dir', 'atf-dir', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    work = args.out / 'work'
    work.mkdir(exist_ok=True)
    sys.path.insert(0, str(sdk / 'tools/imgsign'))
    import rsa
    helpers = module('sd_package_checks', sdk / 'boot/bl1/tools/package-test-firmware.py')
    signer = module('sd_image_signer', sdk / 'build/tools/imgsign/sec_boot_AX620E_sign.py')
    spl_signer = module('sd_spl_signer', sdk / 'build/tools/imgsign/spl_AX620E_sign.py')
    public_path = sdk / 'tools/imgsign/public.pem'
    private_path = sdk / 'tools/imgsign/private.pem'
    public = rsa.PublicKey.load_pkcs1(public_path.read_bytes())
    if public.n.bit_length() != 2048:
        raise ValueError('OneKVM SD packages require the SDK RSA-2048 development key')

    manifests = {}
    for component, directory in (('spl', args.spl_dir), ('uboot', args.uboot_dir), ('atf', args.atf_dir)):
        manifests[component] = json.loads((directory / 'manifest.json').read_text())
    for directory, filename, expected in (
        (args.spl_dir, 'spl-rom-test.bin', manifests['spl']['payloads']['rom-test']),
        (args.uboot_dir, 'u-boot.bin', manifests['uboot']),
        (args.atf_dir, 'bl31.bin', manifests['atf']),
    ):
        payload = (directory / filename).read_bytes()
        if len(payload) != expected['size'] or hashlib.sha256(payload).hexdigest() != expected['sha256']:
            raise ValueError('OE firmware input differs from its manifest: ' + filename)
    if (manifests['uboot']['bl33_el'] != 2 or manifests['uboot']['persistent_environment'] or
            manifests['atf']['optee'] or manifests['atf']['load_address'] != '0x40040000'):
        raise ValueError('Firmware inputs do not satisfy the isolated SD boot contract')

    spl = args.spl_dir / 'spl-rom-test.bin'
    eip = sdk / 'build/tools/imgsign/eip_ax620e.bin'
    if spl.stat().st_size > 0xc800 or eip.stat().st_size > 0x13400:
        raise ValueError('ROM-SD SPL/EIP package boundaries exceeded')
    boot = args.out / 'boot.bin'
    if not spl_signer.do_spl(str(spl), str(public_path), str(private_path), str(boot), str(eip), None, 0x54fafe):
        raise ValueError('ROM-SD SPL packaging failed')
    packed = boot.read_bytes()
    if len(packed) != 0x40000 or packed[:0x20000] != packed[0x20000:]:
        raise ValueError('ROM-SD SPL must contain identical primary/backup packages')
    helpers.check_package(packed[:0x20000], 0xc800, public, rsa)
    if packed[0xcc00:0xcc00 + eip.stat().st_size] != eip.read_bytes():
        raise ValueError('ROM-SD EIP firmware changed during packaging')

    for filename, source, maximum in (
        ('uboot.bin', args.uboot_dir / 'u-boot.bin', 2 * 1024 * 1024),
        ('atf.img', args.atf_dir / 'bl31.bin', 256 * 1024),
    ):
        raw = source.read_bytes()
        if not 0 < len(raw) <= maximum:
            raise ValueError('SD firmware exceeds its loader output window: ' + filename)
        binary = work / (filename + '.bin')
        binary.write_bytes(raw)
        subprocess.run([str(sdk / 'tools/ax_gzip_tool/ax_gzip'), '-9', str(binary)], check=True)
        compressed = binary.with_name(binary.stem + '_axgzip.bin')
        data = compressed.read_bytes()
        compressed.write_bytes(data + bytes((-len(data)) % 4))
        output = args.out / filename
        if not signer.make_image(str(compressed), str(public_path), str(private_path), str(output), 0x54fafe, 2048):
            raise ValueError('SD firmware packaging failed: ' + filename)
        data = bytearray(output.read_bytes())
        if filename == 'uboot.bin':
            struct.pack_into('<3I', data, 956, 0x4d564b4f, 1, 1)
            struct.pack_into('<I', data, 0, helpers.word_sum(data[8:1024]))
        helpers.check_package(data, maximum, public, rsa, compressed=True)
        output.write_bytes(data)

    manifest = {'scope': 'SD FAT only; boot.bin must never be written to eMMC',
                'key_policy': 'SDK RSA-2048 development key; not vendor production signing',
                'sources': {name: value['source_revision'] for name, value in manifests.items()},
                'sd_bl33_el': 2, 'persistent_environment': False, 'files': {}}
    for name in ('boot.bin', 'uboot.bin', 'atf.img'):
        data = (args.out / name).read_bytes()
        manifest['files'][name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    (args.out / 'firmware.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
