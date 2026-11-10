#!/usr/bin/env bash
# Inspect the assembled production root, not the live diagnostic device.
set -euo pipefail
root=${1:?Supply assembled root filesystem}
[[ -d "$root/usr" && -d "$root/var/lib/pacman/local" ]]
packages=(linux-thorch linux-thorch-headers thorch-bsp thorch-tme-modules thorch-motion thorch-motion-firmware thorch-inputplumber)
for package in "${packages[@]}"; do
  pacman --root "$root" -Q "$package"
done
if pacman --root "$root" -Q thorch-dump-debug >/dev/null 2>&1; then
  echo 'Diagnostic package must not be preinstalled' >&2; exit 1
fi
grep -qx 'THORCH_BOOT_DEBUG=0' "$root/etc/thorch/boot-debug.conf"
if find "$root/etc/systemd/system" -type l \
  \( -name 'thorch-dump-debug.service' -o -name 'thorch-motion.service' \) | grep -q .; then
  echo 'Private motion/diagnostic services must not be enabled by default' >&2; exit 1
fi
if grep -R -E '^[[:space:]]*kernel\.hardlockup_panic[[:space:]]*=[[:space:]]*1' \
  "$root/etc/sysctl.d" "$root/usr/lib/sysctl.d"; then
  echo 'Forced hardlockup panic in production root' >&2; exit 1
fi
echo 'Custom package inventory and production defaults passed'
for file in \
  usr/lib/firmware/qcom/sm8550/ayn/thor/adsp.mdt \
  usr/lib/firmware/qcom/sm8550/ayn/thor/adsp.b02 \
  usr/share/thorch-motion/vendor-runtime/dsp/adsp/fastrpc_shell_0 \
  usr/share/thorch-motion/vendor-runtime/sensors/sns_reg_version \
  usr/share/licenses/thorch-motion-firmware/NOTICE.txt \
  usr/share/licenses/thorch-firmware-rocknix/THOR-SENSOR-FIRMWARE-NOTICE.txt; do
  [[ -s "$root/$file" ]] || { echo "Missing sensor firmware/notice: $file" >&2; exit 1; }
done
[[ ! -e "$root/usr/share/thorch-motion/vendor-runtime/socinfo" && \
   ! -e "$root/usr/share/thorch-motion/vendor-runtime/sensors/registry" ]]
echo 'Sensor firmware and copyright notices present; device-private state excluded'
