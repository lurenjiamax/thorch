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
work=$(mktemp -d /tmp/thorch-kernel-package-XXXXXXXX)
trap 'rm -rf -- "$work"' EXIT
cp -a packages/linux-thorch "$work/"
chown -R builder:builder "$work"
chmod 755 "$work"
# Artifact-only packaging: target BSP/readiness dependencies must not be
# installed or spoofed in CI. --nodeps preserves them in package metadata.
runuser -u builder -- env THORCH_ROCKNIX_KERNEL_DIR="$artifacts" \
  bash -c 'cd "$1/linux-thorch"; makepkg --nodeps --noconfirm --cleanbuild' bash "$work"
mapfile -t packages < <(find "$work/linux-thorch" -maxdepth 1 -name '*.pkg.tar.zst' -type f)
[[ ${#packages[@]} == 1 ]] || { echo 'Expected one kernel package' >&2; exit 1; }
pkg=${packages[0]}
bsdtar -xOf "$pkg" .PKGINFO > "$output/PKGINFO"
version=$(sed -n 's/^pkgver = //p' "$output/PKGINFO")
[[ "$version" == 7.1.2-20 ]] || { echo "Unexpected version: $version" >&2; exit 1; }
grep -qx 'arch = aarch64' "$output/PKGINFO"
bsdtar -tf "$pkg" > "$output/package-files.txt"
grep -q 'usr/lib/modules/7.1.2-thorch20/kernel/drivers/md/dm-default-key.ko' "$output/package-files.txt"
grep -q 'usr/lib/modules/7.1.2-thorch20/kernel/fs/quota/quota_v2.ko' "$output/package-files.txt"
cp "$pkg" "$output/"
printf '%s\n' "$version" > "$output/version.txt"
cp "$artifacts/PROVENANCE" "$output/PROVENANCE"
cp docs/custom-kernel-install.md "$output/INSTALL.md"
printf '\nCI source commit: `%s`\n' "${GITHUB_SHA:-$(git rev-parse HEAD)}" >> "$output/INSTALL.md"
(cd "$output"; sha256sum ./*.pkg.tar.zst INSTALL.md PROVENANCE > SHA256SUMS)
