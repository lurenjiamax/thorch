#!/usr/bin/env python3
"""Import a locally extracted Android collection; never print key material."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('collection', type=Path)
parser.add_argument('--recipient-private', type=Path, required=True)
parser.add_argument('--base', type=Path, default=Path('/var/lib/aynthor/android'))
args = parser.parse_args()
if os.geteuid() != 0:
    parser.error('Run as root on Linux')
os.umask(0o077)
base = args.base.resolve()
runtime = base / 'keymint-runtime'
if runtime.exists() or (base / 'keymint-private').exists():
    parser.error('Existing provisioning found; preserve it rather than overwrite')
collection = args.collection.resolve()
properties = dict(line.split('=', 1) for line in
                  (collection / 'manifest.txt').read_text().splitlines() if '=' in line)
if properties.get('format') != 'aynthor-userdata-collection-v1':
    parser.error('Unsupported collection format')
mapping = {'android_release': 'AYNTHOR_ANDROID_RELEASE',
           'android_os_patch': 'AYNTHOR_ANDROID_OS_PATCH',
           'android_vendor_patch': 'AYNTHOR_ANDROID_VENDOR_PATCH'}
for name in mapping:
    if not re.fullmatch(r'[0-9][0-9.-]*', properties.get(name, '')):
        parser.error(f'Missing or invalid collected property: {name}')
pool = collection / 'library-pool'
directories = [pool / 'vendor/lib64', pool / 'system/lib64',
               pool / 'apex/com.android.runtime/lib64/bionic']
directories += sorted((pool / 'apex').glob('com.android.vndk.v*/lib64'))
pending = ['libqtikeymint.so', 'libbinder_ndk.so', 'libc++.so']
resolved = {}
while pending:
    name = pending.pop()
    if name in resolved:
        continue
    source = next((d / name for d in directories if (d / name).is_file()), None)
    if source is None:
        parser.error(f'Missing Android library: {name}; do not mix firmware versions')
    info = subprocess.check_output(['readelf', '-d', str(source)], text=True)
    pending += re.findall(r'\(NEEDED\).*\[([^\]]+)\]', info)
    resolved[name] = source
base.mkdir(mode=0o700, parents=True, exist_ok=True)
os.chmod(base, 0o700)
subprocess.run([sys.executable, str(Path(__file__).parent / 'src/keyblob_bundle.py'),
                'import', '--recipient-private', str(args.recipient_private),
                '--encrypted-directory', str(collection / 'keys'),
                '--output', str(base / 'keymint-private')], check=True)
(runtime / 'lib64').mkdir(parents=True, mode=0o700)
(runtime / 'bin').mkdir(mode=0o700)
for name, source in resolved.items():
    shutil.copyfile(source, runtime / 'lib64' / name)
shutil.copyfile(collection / 'bin/linker64', runtime / 'bin/linker64')
os.chmod(runtime / 'bin/linker64', 0o700)
(runtime / 'properties.env').write_text(''.join(
    f'export {env}={properties[name]}\n' for name, env in mapping.items()))
print(f'Imported device-bound blobs and {len(resolved)} libraries into {base}')
print('Next: build/install the native backend; do not mount until prerequisites pass.')
