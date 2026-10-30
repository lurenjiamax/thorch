#!/usr/bin/env bash
# Native ARM64 custom distribution: published component main/master snapshots.
set -euo pipefail
[[ $(uname -m) == aarch64 && $(id -u) == 0 ]] || {
  echo 'Run inside the native privileged ARM64 builder' >&2; exit 1;
}
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
source scripts/lib/common.sh
load_thorch_config
out="$root/output/custom-image"
work="$root/build/custom-image-packaging"
repo="$root/$THORCH_LOCAL_REPO_DIR"
mkdir -p "$out" "$work" "$repo"
pacman -S --noconfirm --needed clang libevdev libiio json-c glib2 libqmi protobuf protobuf-c pkgconf
id builder >/dev/null 2>&1 || useradd --create-home builder
cp /etc/makepkg.conf "$work/makepkg.conf"
printf '\nPKGEXT=".pkg.tar.zst"\nBUILDDIR="%s/work"\nPKGDEST="%s"\n' "$work" "$out" >> "$work/makepkg.conf"
chown -R builder:builder "$work" "$out"
for component in inputplumber-src motion-src hexagonrpc-src libssc-src dump-src; do
  [[ -d "$root/$component/.git" ]] || { echo "Missing checkout: $component" >&2; exit 1; }
  git config --global --add safe.directory "$root/$component"
  runuser -u builder -- git config --global --add safe.directory "$root/$component"
done
package() {
  local recipe=$1; shift
  # makepkg writes package metadata in startdir, while BUILDDIR/PKGDEST keep
  # compilation and archives outside the maintained component source trees.
  chown builder:builder "$recipe"
  runuser -u builder -- env "$@" makepkg --config "$work/makepkg.conf" \
    --dir "$recipe" --nodeps --nocheck --noconfirm
}
bash scripts/package-custom-kernel.sh
cp output/custom-kernel/*.pkg.tar.zst "$out/"
package "$root/packages/linux-thorch-headers" \
  THORCH_KERNEL_SOURCE_DIR="$root/kernel-src" THORCH_KERNEL_BUILD_DIR="$root/build/kernel-build"
# Compile a real external module against the package contents, not the CI
# source/build directories. This also verifies shipped native host utilities.
mkdir -p "$work/headers-root"
bsdtar -xf "$out"/linux-thorch-headers-*.pkg.tar.zst -C "$work/headers-root"
release=$(cat "$root/build/kernel-build/include/config/kernel.release")
bash "$root/dump-src/build-module.sh" \
  "$work/headers-root/usr/lib/modules/$release/build" "$work/dump-module"
mkdir -p "$root/build/inputplumber-target"
chown builder:builder "$root/build/inputplumber-target"
runuser -u builder -- env CARGO_TARGET_DIR="$root/build/inputplumber-target" \
  bash "$root/inputplumber-src/pkg/archlinux/thorch/build-artifact.sh" "$work/inputplumber-artifact"
package "$root/inputplumber-src/pkg/archlinux/thorch" CARGO_TARGET_DIR="$root/build/inputplumber-target"
package "$root/motion-src/packaging/arch/thorch-motion-source" \
  THORCH_HEXAGONRPC_SOURCE="$root/hexagonrpc-src" THORCH_LIBSSC_SOURCE="$root/libssc-src"
cp "$out"/*.pkg.tar.zst "$repo/"
python3 - "$repo" "$root/output/custom-sources.json" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

repo, sources = map(Path, sys.argv[1:])
bindings = repo / '.thorch-inputs'
bindings.mkdir(exist_ok=True)
inputs_hash = hashlib.sha256(sources.read_bytes()).hexdigest()
for archive in repo.glob('*.pkg.tar.zst'):
    digest = hashlib.sha256()
    with archive.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    (bindings / f'{archive.name}.json').write_text(json.dumps({
        'schema_version': 1, 'artifact': archive.name,
        'artifact_sha256': digest.hexdigest(), 'inputs_sha256': inputs_hash,
    }, indent=2) + '\n')
PY
repo-add "$repo/thorch.db.tar.gz" "$repo"/*.pkg.tar.zst
# The common image stack still comes from Thorch; external components are not
# rebuilt through the legacy in-tree artifact recipes.
mapfile -t remaining < <(python3 scripts/package-manifest.py --repo "$root" profile image |
  grep -Ev '^(linux-thorch|thorch-bsp|thorch-inputplumber)$')
remaining_csv=$(IFS=,; printf '%s' "${remaining[*]}")
bash scripts/build-packages.sh --packages "$remaining_csv"
cp "$repo"/*.pkg.tar.zst "$out/"
export THORCH_IMAGE_EXTRA_PACKAGES='linux-thorch-headers thorch-tme-modules thorch-motion'
bash scripts/build-image.sh --skip-package-build
bash scripts/check-custom-rootfs.sh "$root/build/image-rootfs"
bash scripts/check-thorch-image.sh "$root/output/thorch-arch-aarch64.img"
# Optional diagnostics is a separate release asset, never an image package.
package "$root/dump-src/packaging" THORCH_DUMP_MODULE="$work/dump-module/thorch-dump-watchdog.ko"
cp output/custom-sources.json "$out/SOURCES.json"
cp output/custom-kernel/PROVENANCE output/custom-kernel/TME-PROVENANCE "$out/"
cp docs/custom-image-install.md "$out/INSTALL.md"
zstd -T0 -10 -f output/thorch-arch-aarch64.img -o "$out/thorch-custom-aarch64.img.zst"
(cd "$out"; sha256sum ./*.pkg.tar.zst ./*.img.zst SOURCES.json PROVENANCE TME-PROVENANCE INSTALL.md > SHA256SUMS)
