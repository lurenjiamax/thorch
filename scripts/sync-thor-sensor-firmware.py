#!/usr/bin/env python3
"""Fetch the pinned sensor firmware bundle; never import calibration or keys."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dest', type=Path, default=root / 'vendor/thor-sensor-firmware')
args = parser.parse_args()
config = dict(line.split('=', 1) for line in
              (root / 'source-checkouts.conf').read_text().splitlines()
              if line and not line.startswith('#') and '=' in line)
url = ('https://github.com/' + config['SENSOR_FIRMWARE_REPOSITORY'] +
       '/releases/download/' + config['SENSOR_FIRMWARE_TAG'] + '/' +
       config['SENSOR_FIRMWARE_ASSET'])
cache = root / 'build/cache/sensor-firmware'
cache.mkdir(parents=True, exist_ok=True)
archive = cache / config['SENSOR_FIRMWARE_ASSET']
if not archive.exists():
    with urllib.request.urlopen(url, timeout=120) as response:
        archive.write_bytes(response.read())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
if digest != config['SENSOR_FIRMWARE_SHA256']:
    raise SystemExit('Sensor firmware checksum mismatch')
destination = args.dest.resolve()
destination.mkdir(parents=True, exist_ok=True)
with subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE) as process:
    with tarfile.open(fileobj=process.stdout, mode='r|') as bundle:
        for member in bundle:
            path = Path(member.name)
            if (path.is_absolute() or '..' in path.parts or not member.isfile()
                    or not (member.name == 'NOTICE.txt' or member.name.startswith(
                        ('firmware/qcom/sm8550/ayn/thor/', 'runtime/dsp/adsp/',
                         'runtime/sensors/config/', 'runtime/sensors/sns_reg.'))
                        or member.name == 'runtime/sensors/sns_reg_version')):
                raise SystemExit(f'Unexpected firmware archive member: {member.name}')
            bundle.extract(member, destination, filter='data')
    if process.wait() != 0:
        raise SystemExit('Could not decompress sensor firmware')
for required in ('NOTICE.txt', 'firmware/qcom/sm8550/ayn/thor/adsp.mdt',
                 'firmware/qcom/sm8550/ayn/thor/adsp.b02',
                 'runtime/dsp/adsp/fastrpc_shell_0', 'runtime/sensors/sns_reg_version'):
    if not (destination / required).is_file():
        raise SystemExit(f'Missing sensor firmware: {required}')
(destination / 'SOURCE-SHA256').write_text(digest + '\n')
print(f'Verified proprietary sensor firmware: {digest}')
