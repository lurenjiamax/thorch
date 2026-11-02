# Android dual-screen brightness collection

## Run directly on Android

For automatic collection, push both `collect-android-brightness-auto.sh` (entry)
and `collect-android-brightness.sh` (snapshot worker) into the same directory.
From the device's root terminal, run this single line:

```sh
sh /data/local/tmp/collect-android-brightness-auto.sh
```

The entry saves existing settings, disables automatic brightness, sweeps both
logical controls through 1–100% and back, then restores the original settings on
exit or a handled signal. Do not change sliders during collection; keep both
screens awake with static content. Force-killing the shell or powering off cannot
run restoration; original values are saved in `original-settings.txt`.
This is Thor-specific: primary uses 0–255 and `dual_screen_brightness_level`
uses 0–100. Raw readings must confirm that Android applies both settings; a
successful settings write alone is not proof of panel brightness change.
The automatic entry intentionally changes brightness; the worker remains read-only.

Push `scripts/collect-android-brightness.sh` to `/data/local/tmp/`, then run after
each manual adjustment (replace `50pct` with the stage label):

```sh
sh /data/local/tmp/collect-android-brightness.sh 50pct
```

From an ADB shell with working root, use
`su -c 'sh /data/local/tmp/collect-android-brightness.sh 50pct'` for privileged
read access. Results go to `/sdcard/Download/thorch-brightness/` by default;
an optional second argument selects a different output directory. No settings
are changed. Capture initial, minimum, 10%, 25%, 50%, 75%, 100%, and 50% again.

## Guided host collection

Run from the Thorch repository on the host after booting Thor into Android and
enabling USB debugging. Approve the host on Thor, then identify its serial:

```sh
adb devices -l
python3 scripts/collect-android-brightness.py --serial THOR_SERIAL --su
```

Replace `THOR_SERIAL` explicitly; do not select an unrelated connected device.
Use `--su` only if Android has working root access; otherwise omit it. Permission
errors remain in the logs so missing data can be distinguished from empty values.
The script never runs `adb root`, writes sysfs/settings, or reboots the device.

Manually disable adaptive brightness and any extra-dim/night-light modes. Record
their previous state so you can restore it afterward. Use matching static content
on both panels. Follow prompts from minimum through maximum and back to 50%; if
Android offers independent sliders, set both and note their positions. Note any
visible mismatch, sudden steps, or saturation. Numeric slider percentages do not
measure physical luminance; use a light meter if quantitative matching is needed.

Outputs are stored under `build/android-brightness/<timestamp>/`: device identity,
per-stage backlight/LED raw values and maxima, Android settings, display/power and
SurfaceFlinger dumps. These help correlate Android's logical displays with Linux
DSI panels, but do not capture DSI command traffic or prove panel luminance curves.
Display dumps may contain application/window information; review before sharing.
If backlight paths are absent or inaccessible, retain the logs for a targeted
vendor-interface follow-up rather than guessing sysfs paths or brightness values.
