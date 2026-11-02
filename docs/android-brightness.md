# Android dual-screen brightness collection

## AYN root script runner

Place these two scripts together on the device:

- `/sdcard/collect-android-brightness.sh`: entry selected in AYN settings.
- `/sdcard/collect-android-brightness-main.sh`: complete automatic collector.

AYN's root runner executes each line independently. The entry contains one shell
invocation so loops, variables and restoration traps run in a single shell.
Select **collect-android-brightness.sh**, not the main script, in the runner.
No `su` invocation is needed inside the scripts.

The main script saves existing brightness settings, disables automatic brightness,
and sweeps both controls through 1%, 10%, 25%, 50%, 75%, 100% and back to 1%.
It restores the original settings on normal exit or a handled signal. Keep both
screens awake on matching static content; do not adjust sliders during collection.
Forced termination or power loss cannot run restoration; original values are
saved in `original-settings.txt`.

Results: `/sdcard/Download/thorch-brightness/auto-<timestamp>-<pid>/`.
Each stage contains raw panel backlight values and Android display, power and
SurfaceFlinger dumps. The Thor logical controls are `screen_brightness` (0–255)
and `dual_screen_brightness_level` (0–100). Check raw readings to confirm settings
actually changed both panels. Numeric percentages do not measure luminance.
Review dumps for private window/application information before sharing.

## Optional guided host collection

`python3 scripts/collect-android-brightness.py --serial THOR_SERIAL` performs
read-only snapshots while you manually change brightness. `--su` is optional for
devices exposing a working `su` command; AYN's root runner does not imply that
ADB has `su`. Host outputs go under `build/android-brightness/`.
