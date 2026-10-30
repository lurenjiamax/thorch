#!/usr/bin/env python3
"""Normalize official legacy/raw ROCKNIX boot inputs for Thor's legacy ABL."""
import argparse
import gzip
import importlib.util
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('boot', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--config', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location(
        'boot_image', root / 'packages/thorch-bsp/payload/usr/lib/thorch/boot_image.py')
    boot_image = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(boot_image)
    kernel = (args.boot / 'KERNEL').read_bytes()
    args.output.mkdir(parents=True, exist_ok=True)
    if kernel.startswith(boot_image.ANDROID_MAGIC):
        boot = boot_image.BootImage.read(args.boot / 'KERNEL')
        image, dtb_stream = boot_image.decompress_kernel_payload(boot.kernel_payload)
        boot_image.validate_arm64_image(image)
        dtbs = boot_image.parse_dtbs(dtb_stream)
        boot_image.validate_dtb_contract(
            dtbs, require_symbols=True, require_thor=True, forbid_aim300=False)
        (args.output / 'Image').write_bytes(image)
        (args.output / 'KERNEL').write_bytes(kernel)
        return
    boot_image.validate_arm64_image(kernel)
    dtb = (args.boot / 'dtbs/qcs8550-ayn-thor.dtb').read_bytes()
    boot_image.validate_dtb_contract(
        boot_image.parse_dtbs(dtb), require_symbols=True,
        require_thor=True, forbid_aim300=True)
    options = [line.split(None, 1)[1] for line in
               (args.boot / 'loader/entries/rocknix.conf').read_text().splitlines()
               if line.strip().startswith('options ')]
    if len(options) != 1:
        raise ValueError('Expected exactly one official loader options line')
    config = json.loads((args.config or root / 'config/thor-legacy-boot.json').read_text())
    (args.output / 'Image').write_bytes(kernel)
    # Temporary template ramdisk is replaced by the image's actual initramfs.
    (args.output / 'template-ramdisk').write_bytes(gzip.compress(b'', mtime=0))
    (args.output / 'kernel-payload').write_bytes(gzip.compress(kernel, mtime=0) + dtb)
    command = ['mkbootimg', '--header_version', '0', '--base', '0x0']
    for field in ('kernel_offset', 'ramdisk_offset', 'second_offset', 'tags_offset', 'pagesize'):
        command.extend([f'--{field}', config[field]])
    command.extend(['--kernel', str(args.output / 'kernel-payload'),
                    '--ramdisk', str(args.output / 'template-ramdisk'),
                    '--cmdline', ' '.join([options[0], *config['cmdline_append']]),
                    '-o', str(args.output / 'KERNEL')])
    subprocess.run(command, check=True)
    boot_image.BootImage.read(args.output / 'KERNEL')


if __name__ == '__main__':
    main()
