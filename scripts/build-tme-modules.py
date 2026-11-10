#!/usr/bin/env python3
"""Compile the maintained TME checkout without fetching or rewriting source."""
import argparse
from pathlib import Path
import shutil
import subprocess


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=root / 'kernel-src',
                        help='Maintained kernel Git checkout')
    parser.add_argument('--build', type=Path, default=root / 'build/kernel-build')
    parser.add_argument('--module-source', type=Path, default=root / 'tme-src')
    parser.add_argument('--output', type=Path, default=root / 'build/tme-modules')
    args = parser.parse_args()
    source, build, module_source, out = (
        p.resolve() for p in (args.source, args.build, args.module_source, args.output)
    )
    for checkout in (source, module_source):
        subprocess.run(['git', '-C', str(checkout), 'rev-parse', '--verify', 'HEAD'],
                       check=True, stdout=subprocess.DEVNULL)
        if out == checkout or checkout in out.parents or out in checkout.parents:
            parser.error('Module output and maintained source must be separate')
    if not (build / 'Module.symvers').is_file():
        parser.error('Compile the matching kernel first: missing Module.symvers')
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(['make', '-C', str(source), f'O={build}', 'ARCH=arm64',
                    'LOCALVERSION=', f'M={module_source}', f'MO={out}', 'modules'],
                   check=True)
    subprocess.run(['dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o',
                    str(out / 'tme-overlay.dtbo'),
                    str(module_source / 'tme-overlay.dts')], check=True)
    shutil.copyfile(module_source / 'PROVENANCE', out / 'PROVENANCE')
    with (out / 'PROVENANCE').open('a') as record:
        for name, checkout in (('KERNEL', source), ('TME', module_source)):
            commit = subprocess.check_output(
                ['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
            status = subprocess.check_output(
                ['git', '-C', str(checkout), 'status', '--porcelain', '-uno'], text=True)
            record.write(f'{name}_COMMIT={commit}\n{name}_DIRTY={bool(status)}\n')
            if status:
                diff = subprocess.check_output(
                    ['git', '-C', str(checkout), 'diff', 'HEAD', '--binary'])
                (out / f'{name.lower()}-source.diff').write_bytes(diff)


if __name__ == '__main__':
    main()
