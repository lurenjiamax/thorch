#!/system/bin/sh
# Root entry point: automatic logical-brightness sweep; always restore settings.
set -u
if [ "$(id -u)" != 0 ]; then
    echo 'Run this entry script from your root terminal.' >&2
    exit 1
fi
snapshot() {
    label=$1
    sample="$out/$label"
    mkdir -p "$sample" || return 1
    {
        date
        id
        for d in /sys/class/backlight/* /sys/class/leds/*backlight*; do
            [ -d "$d" ] || continue
            echo "--- $d ---"
            readlink -f "$d/device"
            for f in brightness actual_brightness max_brightness bl_power type; do
                [ ! -f "$d/$f" ] || { echo "[$f]"; cat "$d/$f"; }
            done
        done
        for key in screen_brightness dual_screen_brightness_level screen_brightness_float screen_brightness_mode; do
            echo "$key=$(settings get system "$key")"
        done
    } > "$sample/backlight.txt" 2>&1
    for service in display power SurfaceFlinger; do
        dumpsys "$service" > "$sample/$service.txt" 2>&1
    done
    echo "Saved: $sample"
}
out="${1:-/sdcard/Download/thorch-brightness}/auto-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$out" || exit 1
exec > "$out/run.log" 2>&1
echo 'Capture mode: direct panel backlight sweep, not Android slider calibration'
for panel in panel0-backlight panel1-backlight; do
    node="/sys/class/backlight/$panel"
    [ -w "$node/brightness" ] || { echo "Not writable: $node/brightness"; exit 1; }
    value=$(cat "$node/brightness") || exit 1
    maximum=$(cat "$node/max_brightness") || exit 1
    case "$value" in ''|*[!0-9]*) echo 'Invalid saved brightness'; exit 1 ;; esac
    case "$maximum" in ''|*[!0-9]*|0) echo 'Invalid maximum'; exit 1 ;; esac
    printf '%s %s %s\n' "$panel" "$value" "$maximum" >> "$out/original-backlights.txt"
done
restore() {
    while read -r panel value maximum; do
        echo "$value" > "/sys/class/backlight/$panel/brightness" || echo "RESTORE FAILED: $panel"
        echo "Restored $panel requested=$value readback=$(cat /sys/class/backlight/$panel/brightness)"
    done < "$out/original-backlights.txt"
    echo "Results: $out"
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
getprop > "$out/device-properties.txt" 2>&1
snapshot initial || exit 1
echo 'Sweeping both screens automatically. Keep screens awake; do not adjust sliders.'
echo 'Each panel is scaled against its own sysfs maximum. Android settings are unchanged.'
# Avoid a zero setting that may blank the UI; include descending points for hysteresis.
step=0
for percent in 1 10 25 50 75 100 75 50 25 10 1; do
    step=$((step + 1))
    while read -r panel value maximum; do
        raw=$(( (percent * maximum + 50) / 100 ))
        echo "$raw" > "/sys/class/backlight/$panel/brightness" || exit 1
        echo "Requested $panel $percent% raw=$raw"
    done < "$out/original-backlights.txt"
    sleep 3
    while read -r panel value maximum; do
        expected=$(( (percent * maximum + 50) / 100 ))
        actual=$(cat "/sys/class/backlight/$panel/brightness") || exit 1
        [ "$actual" = "$expected" ] || {
            echo "Brightness did not hold: $panel expected=$expected readback=$actual; stopping"
            exit 1
        }
    done < "$out/original-backlights.txt"
    snapshot "${step}-${percent}pct" || exit 1
done
