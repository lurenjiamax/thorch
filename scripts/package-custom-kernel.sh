#!/usr/bin/env bash
# Run as root inside the native ARM64 CI builder, never on the target device.
set -euo pipefail
[[ $(uname -m) == aarch64 ]] || { echo 'Native ARM64 builder required' >&2; exit 1; }
[[ $(id -u) == 0 ]] || { echo 'Container root required' >&2; exit 1; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
artifacts="$root/vendor/rocknix-kernel"
output="$root/output/custom-kernel"
mkdir -p "$output"
install -Dm755 packages/thorch-bsp/payload/usr/lib/thorch/boot_image.py /usr/lib/thorch/boot_image.py
id builder >/dev/null 2>&1 || useradd --create-home builder
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/kernel-package-XXXXXXXX")
cp -a packages/linux-thorch "$work/"
cp /etc/makepkg.conf "$work/makepkg.conf"
printf '\nPKGEXT=".pkg.tar.zst"\n' >> "$work/makepkg.conf"
chown -R builder:builder "$work"
chmod 755 "$work"
# Artifact-only packaging: target BSP/readiness dependencies must not be
# installed or spoofed in CI. --nodeps preserves them in package metadata.
runuser -u builder -- env THORCH_ROCKNIX_KERNEL_DIR="$artifacts" \
  bash -c 'cd "$1/linux-thorch"; makepkg --config "$1/makepkg.conf" --nodeps --noconfirm --cleanbuild' bash "$work"
mapfile -t packages < <(find "$work/linux-thorch" -maxdepth 1 -name '*.pkg.tar.zst' -type f)
[[ ${#packages[@]} == 1 ]] || { echo 'Expected one kernel package' >&2; exit 1; }
pkg=${packages[0]}
bsdtar -xOf "$pkg" .PKGINFO > "$output/PKGINFO"
version=$(sed -n 's/^pkgver = //p' "$output/PKGINFO")
expected=$(bash -c 'source "$1"; printf "%s-%s" "$pkgver" "$pkgrel"' bash packages/linux-thorch/PKGBUILD)
release="${expected%-*}-thorch${expected##*-}"
[[ "$version" == "$expected" ]] || { echo "Unexpected version: $version" >&2; exit 1; }
grep -qx 'arch = aarch64' "$output/PKGINFO"
bsdtar -tf "$pkg" > "$output/package-files.txt"
grep -Fq "usr/lib/modules/$release/kernel/drivers/md/dm-default-key.ko" "$output/package-files.txt"
grep -Fq "usr/lib/modules/$release/kernel/fs/quota/quota_v2.ko" "$output/package-files.txt"
cp "$pkg" "$output/"
python scripts/build-tme-modules.py --source kernel-src \
  --build build/kernel-build --output build/tme-modules
cp -a packages/thorch-tme-modules "$work/"
chown -R builder:builder "$work/thorch-tme-modules"
runuser -u builder -- env THORCH_TME_MODULE_DIR="$root/build/tme-modules" \
  bash -c 'cd "$1/thorch-tme-modules"; makepkg --config "$1/makepkg.conf" --nodeps --noconfirm --cleanbuild' bash "$work"
mapfile -t module_packages < <(find "$work/thorch-tme-modules" -maxdepth 1 -name '*.pkg.tar.zst' -type f)
[[ ${#module_packages[@]} == 1 ]] || { echo 'Expected one module package' >&2; exit 1; }
cp "${module_packages[0]}" "$output/"
cp -a packages/thorch-bsp "$work/"
chown -R builder:builder "$work/thorch-bsp"
runuser -u builder -- \
  bash -c 'cd "$1/thorch-bsp"; makepkg --config "$1/makepkg.conf" --nodeps --noconfirm --cleanbuild' bash "$work"
mapfile -t bsp_packages < <(find "$work/thorch-bsp" -maxdepth 1 -name '*.pkg.tar.zst' -type f)
[[ ${#bsp_packages[@]} == 1 ]] || { echo 'Expected one BSP package' >&2; exit 1; }
cp "${bsp_packages[0]}" "$output/"
# Audit the actual BSP archive against the builder's installed mkinitcpio hook.
# Fail if upstream adds a rebuild trigger that our transaction does not cover.
python3 - "${bsp_packages[0]}" <<'PY'
from pathlib import Path
import subprocess
import sys

def triggers(text):
    result = set()
    section = None
    fields = {}
    def finish():
        if section == 'Trigger':
            for operation in fields.get('Operation', []):
                for target in fields.get('Target', []):
                    result.add((fields['Type'][0], operation, target))
    for line in text.splitlines() + ['[End]']:
        line = line.strip()
        if line.startswith('['):
            finish()
            section, fields = line[1:-1], {}
        elif '=' in line and not line.startswith('#'):
            key, value = (part.strip() for part in line.split('=', 1))
            fields.setdefault(key, []).append(value)
    return result

required = triggers(Path('/usr/share/libalpm/hooks/90-mkinitcpio-install.hook').read_text())
for name in ('60-thorch-boot-transaction-prepare.hook', '95-thorch-boot-transaction-commit.hook'):
    text = subprocess.check_output(['bsdtar', '-xOf', sys.argv[1],
                                   f'usr/share/libalpm/hooks/{name}'], text=True)
    missing = required - triggers(text)
    if missing:
        raise SystemExit(f'{name} lacks mkinitcpio triggers: {sorted(missing)}')
print('Packaged BSP covers every installed mkinitcpio rebuild trigger')
PY
cp build/tme-modules/PROVENANCE "$output/TME-PROVENANCE"
printf '%s\n' "$version" > "$output/version.txt"
cp "$artifacts/PROVENANCE" "$output/PROVENANCE"
cp docs/custom-kernel-install.md "$output/INSTALL.md"
(cd "$output"; sha256sum ./*.pkg.tar.zst INSTALL.md PROVENANCE TME-PROVENANCE > SHA256SUMS)
