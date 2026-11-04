#!/usr/bin/env python3
"""Read-only, explicitly targeted Android dual-panel brightness collection."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess


SNAPSHOT = r'''
echo '=== time ==='
date
echo '=== backlights ==='
for d in /sys/class/backlight/* /sys/class/leds/*backlight*; do
    [ -d "$d" ] || continue
    echo "--- $d ---"
    readlink -f "$d/device"
    for f in brightness actual_brightness max_brightness bl_power type uevent; do
        [ ! -f "$d/$f" ] || { echo "[$f]"; cat "$d/$f"; }
    done
done
echo '=== Android brightness settings ==='
for key in screen_brightness screen_brightness_float screen_brightness_mode screen_auto_brightness_adj; do
    echo "$key=$(settings get system "$key")"
done
echo '=== display ==='
dumpsys display
echo '=== power ==='
dumpsys power
echo '=== SurfaceFlinger ==='
dumpsys SurfaceFlinger
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='Thor ADB serial; no automatic device selection')
    parser.add_argument('--su', action='store_true', help='Read snapshots through Android su (may require approval on device)')
    parser.add_argument('--output', type=Path, default=Path('build/android-brightness'))
    args = parser.parse_args()
    adb = ['adb', '-s', args.serial]
    subprocess.run(adb + ['get-state'], check=True, capture_output=True, text=True)
    model = subprocess.check_output(adb + ['shell', 'getprop', 'ro.product.model'], text=True).strip()
    print(f'Target: {args.serial} / {model}')
    if input('Confirm this is Thor (type THOR): ').strip() != 'THOR':
        raise SystemExit('Cancelled')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    output = args.output / stamp
    output.mkdir(parents=True, exist_ok=False)

    def collect(name, command):
        result = subprocess.run(adb + ['shell'] + command, capture_output=True, text=True, timeout=90)
        (output / name).write_text(result.stdout + '\n=== stderr ===\n' + result.stderr)
        if result.returncode:
            print(f'Warning: {name} exited {result.returncode}; see saved log')
        return result.returncode

    collect('identity.txt', ['getprop'])
    collect('kernel.txt', ['uname', '-a'])
    collect('permissions.txt', ['su', '-c', 'id'] if args.su else ['id'])
    print('Disable automatic/adaptive brightness manually. Keep the same static image on both screens.')
    print('For each label, set BOTH screens if Android offers separate controls; otherwise use its shared control.')
    print('Labels are requested slider positions, not measured luminance. No settings are changed by this script.')
    records = []
    try:
        for label in ('initial', 'minimum', '10pct', '25pct', '50pct', '75pct', '100pct', '50pct-return'):
            note = input(f'[{label}] Adjust manually, wait for brightness to settle; enter notes then Enter (q quits): ')
            if note.strip().lower() == 'q':
                break
            command = ['su', '-c', SNAPSHOT] if args.su else ['sh', '-c', SNAPSHOT]
            # adb shell joins arguments remotely: quote the complete script explicitly.
            import shlex
            code = collect(f'{label}.txt', [shlex.join(command)])
            records.append({'label': label, 'note': note, 'exit_code': code,
                            'time': datetime.datetime.now().isoformat()})
            print(f'Saved {label}')
    finally:
        (output / 'manifest.json').write_text(json.dumps({
            'serial': args.serial, 'model': model, 'su': args.su,
            'samples': records,
        }, indent=2) + '\n')
        print(f'Collection: {output.resolve()}')


if __name__ == '__main__':
    main()
