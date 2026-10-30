#!/usr/bin/env python3
"""Publish and validate OpenABL Type-1 raw Image boot entries."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

import boot_image


def fields(path):
    result = {}
    for line in path.read_text().splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) == 2 and not parts[0].startswith('#'):
            if parts[0] in result:
                raise ValueError(f'Duplicate loader field: {parts[0]}')
            result[parts[0]] = parts[1]
    return result


def validate(boot, uuid):
    entry = fields(boot / 'loader/entries/thorch.conf')
    for key, expected in [('linux', '/KERNEL'),
                          ('initrd', '/initramfs-linux-thorch.img'),
                          ('devicetree', '/dtbs/qcs8550-ayn-thor.dtb')]:
        if entry.get(key) != expected:
            raise ValueError(f'Invalid {key} in native loader entry')
        if not (boot / expected.lstrip('/')).is_file():
            raise ValueError(f'Missing {expected}')
    boot_image.validate_arm64_image((boot / 'KERNEL').read_bytes())
    boot_image.validate_dtb_contract(
        boot_image.parse_dtbs((boot / entry['devicetree'].lstrip('/')).read_bytes()),
        require_symbols=True, require_thor=True, forbid_aim300=True)
    options = shlex.split(entry['options'])
    roots = [token for token in options if token.startswith('root=')]
    if roots != [f'root=UUID={uuid}']:
        raise ValueError('Loader root UUID mismatch')
    if not (boot / 'initramfs-linux-thorch.img').stat().st_size:
        raise ValueError('Empty native initramfs')


def atomic_write(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f'.{path.name}.', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(name, 0o644)
        os.replace(name, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        Path(name).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['publish', 'validate'])
    parser.add_argument('--boot-dir', type=Path, default=Path('/boot'))
    parser.add_argument('--root-uuid')
    parser.add_argument('--rootfstype', choices=['ext4', 'btrfs'], default='ext4')
    parser.add_argument('--source-dir', type=Path, default=Path('/usr/share/thorch/rocknix'))
    args = parser.parse_args()
    if args.root_uuid is None:
        args.root_uuid = subprocess.check_output(
            ['findmnt', '-nro', 'UUID', '/'], text=True).strip()
    if not re.fullmatch(r'[A-Za-z0-9-]+', args.root_uuid):
        raise ValueError('Invalid root UUID')
    if args.command == 'publish':
        initramfs = args.boot_dir / 'initramfs-linux-thorch.img'
        if not initramfs.is_file() or not initramfs.stat().st_size:
            raise ValueError('Generate the Thorch initramfs before publishing boot files')
        source = args.source_dir
        kernel = (source / 'KERNEL').read_bytes()
        boot_image.validate_arm64_image(kernel)
        dtb = (source / 'dtbs/qcs8550-ayn-thor.dtb').read_bytes()
        boot_image.validate_dtb_contract(boot_image.parse_dtbs(dtb),
                                        require_symbols=True, require_thor=True,
                                        forbid_aim300=True)
        upstream = fields(source / 'loader/entries/rocknix.conf')
        tokens = [item for item in shlex.split(upstream['options'])
                  if item.split('=', 1)[0] not in
                  ('boot', 'disk', 'root', 'rootfstype', 'fbcon', 'mem_sleep_default')]
        tokens += [f'root=UUID={args.root_uuid}', f'rootfstype={args.rootfstype}',
                   'rw', 'fbcon=rotate:1', 'mem_sleep_default=s2idle',
                   'qcom_ice.use_wrapped_keys=1', 'qcom_ice.android_legacy_hwkm=1']
        debug = os.environ.get('THORCH_BOOT_DEBUG', '0')
        if debug not in ('0', '1'):
            raise ValueError('THORCH_BOOT_DEBUG must be 0 or 1')
        if debug == '1':
            tokens = [token for token in tokens if token != 'quiet']
            tokens.append('no_console_suspend')
        scm_dump = os.environ.get('THORCH_SCM_DUMP', 'off')
        if scm_dump not in ('off', 'full', 'mini', 'full,mini'):
            raise ValueError('Invalid explicit SCM dump selection')
        if scm_dump != 'off':
            tokens.append(f'qcom_scm.download_mode={scm_dump}')
        atomic_write(args.boot_dir / 'dtbs/qcs8550-ayn-thor.dtb', dtb)
        atomic_write(args.boot_dir / 'KERNEL', kernel)
        atomic_write(args.boot_dir / 'loader/entries/thorch.conf', (
            'title Thorch\nlinux /KERNEL\ninitrd /initramfs-linux-thorch.img\n'
            'devicetree /dtbs/qcs8550-ayn-thor.dtb\noptions ' + ' '.join(tokens) + '\n'
        ).encode())
    validate(args.boot_dir, args.root_uuid)
    print('Native OpenABL Image, DTB, initramfs and loader entry validated')


if __name__ == '__main__':
    main()
