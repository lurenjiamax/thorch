#!/usr/bin/env bash
# Package an already compiled, committed recovery source. No device operations.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
workspace="$(dirname "$root")"
source="$root/kernel-src"
build="$root/build/kernel-build"
tme_source="$root/tme-src"
tme="$root/build/tme-modules"
research="$workspace/research/power-20261015"
out="$root/artifacts/kernel28"
release=7.1.2-thorch28
test "$(cat "$build/include/config/kernel.release")" = "$release"
test -s "$build/arch/arm64/boot/Image"
test -s "$tme/aynthor_tme.ko"
test -z "$(git -C "$source" status --porcelain -uno)"
mkdir -p "$out/boot" "$out/packages"
exec > >(tee "$out/package.log") 2>&1
make -C "$source" O="$build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  LOCALVERSION= INSTALL_MOD_PATH="$out" INSTALL_MOD_STRIP=1 modules_install
mkdir -p "$out/usr/lib"
mv "$out/lib/modules" "$out/usr/lib/"
cp "$build/arch/arm64/boot/Image" "$out/boot/Image"
boot_tool="$root/packages/thorch-bsp/payload/usr/lib/thorch/boot_image.py"
python3 "$boot_tool" replace-kernel "$research/KERNEL.current" \
  "$out/boot/Image" "$out/boot/KERNEL" \
  "$research/current-dtbs/"baseline20-{00..12}.dtb
python3 "$research/prepare-current-dtbs.py" "$out/boot/KERNEL" "$out/verified-dtbs"
for index in {00..12}; do
  cmp "$research/current-dtbs/baseline20-$index.dtb" \
    "$out/verified-dtbs/baseline20-$index.dtb"
done
{
  printf 'Recovered Thor candidate: %s\n' "$release"
  printf 'Kernel source commit: '; git -C "$source" rev-parse HEAD
  printf 'TME source commit: '; git -C "$tme_source" rev-parse HEAD
  printf '%s\n' 'Exact retained DTBs. Suspend and charging stability NOT fixed.'
  sha256sum "$build/.config" "$out/boot/Image" "$out/boot/KERNEL"
} > "$out/PROVENANCE"
cp /etc/makepkg.conf "$out/makepkg.conf"
printf '\nCARCH=aarch64\n' >> "$out/makepkg.conf"
export THORCH_ROCKNIX_KERNEL_DIR="$out" THORCH_TME_MODULE_DIR="$tme"
export THORCH_BOOT_IMAGE_TOOL="$boot_tool"
for package in linux-thorch thorch-tme-modules; do
  cp -a "$root/packages/$package" "$out/packages/"
  (cd "$out/packages/$package"; makepkg --config "$out/makepkg.conf" \
    --ignorearch --nodeps --noconfirm)
done
(cd "$out"; sha256sum packages/*/*-aarch64.pkg.tar.zst > SHA256SUMS)
printf '%s\n' "$release" > "$out/BUILD-COMPLETE"
