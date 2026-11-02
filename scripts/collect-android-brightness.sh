#!/system/bin/sh
# Read-only Android-side snapshot. Usage: sh SCRIPT [sample-label] [output-root]
set -u
label=${1:-initial}
case "$label" in
    ''|*[!a-zA-Z0-9_-]*) echo 'Label must contain only letters, digits, _ or -' >&2; exit 2 ;;
esac
root=${2:-/sdcard/Download/thorch-brightness}
stamp=$(date +%Y%m%d-%H%M%S)
out="$root/$stamp-$label-$$"
mkdir -p "$out" || exit 1
{
    date
    id
    uname -a
    getprop
} > "$out/identity.txt" 2>&1
{
    for d in /sys/class/backlight/* /sys/class/leds/*backlight*; do
        [ -d "$d" ] || continue
        echo "--- $d ---"
        readlink -f "$d/device"
        for f in brightness actual_brightness max_brightness bl_power type uevent; do
            [ ! -f "$d/$f" ] || { echo "[$f]"; cat "$d/$f"; }
        done
    done
} > "$out/backlight.txt" 2>&1
{
    for key in screen_brightness screen_brightness_float screen_brightness_mode screen_auto_brightness_adj; do
        echo "$key=$(settings get system "$key")"
    done
} > "$out/settings.txt" 2>&1
for service in display power SurfaceFlinger; do
    dumpsys "$service" > "$out/$service.txt" 2>&1
    status=$?
    echo "$service exit=$status"
done
echo "Saved: $out"
echo 'Permission errors are retained in logs. This script changes no brightness settings.'
