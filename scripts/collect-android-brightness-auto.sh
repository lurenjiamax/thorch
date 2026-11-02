#!/system/bin/sh
# Root entry point: automatic logical-brightness sweep; always restore settings.
set -u
if [ "$(id -u)" != 0 ]; then
    echo 'Run this entry script from your root terminal.' >&2
    exit 1
fi
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
worker="$base/collect-android-brightness.sh"
[ -r "$worker" ] || { echo "Missing worker: $worker" >&2; exit 1; }
primary=$(settings get system screen_brightness) || exit 1
secondary=$(settings get system dual_screen_brightness_level) || exit 1
mode=$(settings get system screen_brightness_mode) || exit 1
case "$primary:$secondary:$mode" in
    *null*|*[!0-9:]*) echo 'Expected numeric Thor brightness settings; stopping without changes.' >&2; exit 1 ;;
esac
out="${1:-/sdcard/Download/thorch-brightness}/auto-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$out" || exit 1
printf 'screen_brightness=%s\ndual_screen_brightness_level=%s\nscreen_brightness_mode=%s\n' \
    "$primary" "$secondary" "$mode" > "$out/original-settings.txt"
restore() {
    settings put system screen_brightness "$primary"
    settings put system dual_screen_brightness_level "$secondary"
    settings put system screen_brightness_mode "$mode"
    echo "Restored original brightness settings. Results: $out"
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
sh "$worker" initial "$out" || exit 1
settings put system screen_brightness_mode 0 || exit 1
echo 'Sweeping both screens automatically. Keep screens awake; do not adjust sliders.'
echo 'Primary uses 0..255; Thor secondary setting uses percent 0..100.'
echo 'Raw panel values are collected to verify these logical settings take effect.'
# Avoid a zero setting that may blank the UI; include descending points for hysteresis.
for percent in 1 10 25 50 75 100 75 50 25 10 1; do
    raw=$(( (percent * 255 + 50) / 100 ))
    settings put system screen_brightness "$raw" || exit 1
    settings put system dual_screen_brightness_level "$percent" || exit 1
    echo "Requested $percent%: primary=$raw secondary=$percent"
    sleep 3
    sh "$worker" "${percent}pct" "$out" || exit 1
done
