#!/usr/bin/env bash
# Compile an existing maintained checkout; never fetch, reset or patch source.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$script_dir/lib/common.sh"
load_thorch_config
root="$(repo_root)"
source_dir="${THORCH_KERNEL_SOURCE_DIR:-$root/kernel-src}"
build_dir="${THORCH_KERNEL_BUILD_DIR:-$root/build/kernel-build}"
config="${THORCH_KERNEL_CONFIG:-$source_dir/arch/arm64/configs/thorch_recovered_defconfig}"
dest="${THORCH_ROCKNIX_KERNEL_DIR}"
jobs="${THORCH_KERNEL_JOBS:-$(nproc)}"
cross_compile="${THORCH_KERNEL_CROSS_COMPILE-aarch64-linux-gnu-}"
if [[ "$(uname -m)" == aarch64 && -z "${THORCH_KERNEL_CROSS_COMPILE+x}" ]]; then
  cross_compile=""
fi
prepare_only=0
reuse=0
while (( $# )); do
  case "$1" in
    --source-dir) source_dir="$2"; shift 2 ;;
    --build-dir) build_dir="$2"; shift 2 ;;
    --config) config="$2"; shift 2 ;;
    --dest) dest="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    --cross-compile) cross_compile="$2"; shift 2 ;;
    --prepare-only) prepare_only=1; shift ;;
    --reuse-build-dir|--incremental) reuse=1; shift ;;
    --help|-h)
      echo 'Compile committed kernel-src without downloading or editing source.'
      echo 'Options: --source-dir --build-dir --config --dest --jobs'
      echo '         --cross-compile --prepare-only --reuse-build-dir'
      exit 0 ;;
    *) die "Unsupported option $1; source acquisition/patching is a separate Git operation" ;;
  esac
done
[[ "$source_dir" = /* ]] || source_dir="$root/$source_dir"
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"
[[ "$config" = /* ]] || config="$root/$config"
[[ "$dest" = /* ]] || dest="$root/$dest"
source_dir="$(realpath "$source_dir")"
build_dir="$(realpath -m "$build_dir")"
dest="$(realpath -m "$dest")"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || die 'Invalid job count'
case "$build_dir/" in "$source_dir/"* ) die 'Build output must be outside source' ;; esac
case "$source_dir/" in "$build_dir/"* ) die 'Source must be outside build output' ;; esac
git -C "$source_dir" rev-parse --verify HEAD >/dev/null
[[ -z "$(git -C "$source_dir" status --porcelain -uno)" ]] || die 'Commit kernel source changes before release build'
[[ -f "$config" ]] || die "Missing committed kernel config: $config"
release="$(linux_thorch_expected_kernel_release "$root/packages/linux-thorch/PKGBUILD")"
mkdir -p "$build_dir"
if (( !reuse )) || [[ ! -f "$build_dir/.config" ]]; then
  cp "$config" "$build_dir/.config"
fi
"$source_dir/scripts/config" --file "$build_dir/.config" \
  --set-str LOCALVERSION "-thorch${release##*-thorch}" --disable LOCALVERSION_AUTO
args=(-C "$source_dir" O="$build_dir" ARCH=arm64 CROSS_COMPILE="$cross_compile" LOCALVERSION=)
make "${args[@]}" olddefconfig syncconfig
[[ "$(make -s "${args[@]}" kernelrelease)" == "$release" ]] || die 'Kernel release mismatch'
(( prepare_only )) && exit 0
[[ -s "$dest/boot/loader/entries/rocknix.conf" ]] || \
  die 'Import the current official ROCKNIX loader configuration before building'
mapfile -t targets < <(find "$source_dir/arch/arm64/boot/dts/qcom" -maxdepth 1 \
  -name 'qcs8550-*.dts' ! -name 'qcs8550-aim300-aiot.dts' -printf 'qcom/%f\n' | sort)
(( ${#targets[@]} )) || die 'Missing committed SM8550 DTS sources'
dtbs=()
for target in "${targets[@]}"; do dtbs+=("${target%.dts}.dtb"); done
make "${args[@]}" -j"$jobs" DTC_FLAGS=-@ Image modules "${dtbs[@]}"
stage="$build_dir/modules-stage"
mkdir -p "$stage"
make "${args[@]}" INSTALL_MOD_PATH="$stage" INSTALL_MOD_STRIP=1 modules_install
paths=()
for dtb in "${dtbs[@]}"; do paths+=("$build_dir/arch/arm64/boot/dts/$dtb"); done
boot_tool="$root/packages/thorch-bsp/payload/usr/lib/thorch/boot_image.py"
python3 "$boot_tool" check-config "$build_dir/arch/arm64/boot/Image" \
  "$root/packages/linux-thorch/waydroid-kernel.config"
mkdir -p "$dest/boot" "$dest/usr/lib/modules"
# Replace imported upstream modules with exactly this build's release. The
# staging directory may also retain earlier incremental-build releases.
rsync -a --delete --delete-excluded --include="/$release/***" --exclude='*' \
  "$stage/lib/modules/" "$dest/usr/lib/modules/"
cp "$build_dir/arch/arm64/boot/Image" "$dest/boot/Image"
cp "$build_dir/arch/arm64/boot/Image" "$dest/boot/KERNEL"
mkdir -p "$dest/boot/dtbs"
cp "${paths[@]}" "$dest/boot/dtbs/"
{
  printf 'THORCH_KERNEL_SOURCE=maintained-git-checkout\nTHORCH_KERNEL_RELEASE=%s\n' "$release"
  printf 'THORCH_KERNEL_RESOLVED_REF=%s\n' "$(git -C "$source_dir" rev-parse HEAD)"
  sha256sum "$build_dir/.config" "$dest/boot/Image" "$dest/boot/KERNEL"
  sha256sum "$dest/boot/loader/entries/rocknix.conf" "$dest/boot/dtbs/qcs8550-ayn-thor.dtb"
} > "$dest/KERNEL-PROVENANCE"
cat "$dest/KERNEL-PROVENANCE" >> "$dest/PROVENANCE"
log "Built $release from committed source; NOT deployed"
