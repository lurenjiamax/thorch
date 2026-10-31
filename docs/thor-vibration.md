# AYN Thor vibration setup

For **AYN Thor / SM8550 running Thorch (Arch Linux ARM)**. Stock ROCKNIX and
Android need their own packages; do not install Arch packages on those systems.
See [sensor setup](thor-sensors.md) for gyro/accelerometer support. Sensors,
Android userdata keys and the motion service are not required for vibration.

## Repositories and software

| Component | Source | What to install / why |
| --- | --- | --- |
| Thorch integration | [Thorch custom-kernel branch](https://github.com/lurenjiamax/thorch/tree/feat/thor-custom-kernel) | Matching `thorch-bsp`, `linux-thorch`, `thorch-tme-modules` release packages; TME is a companion kernel dependency, not the rumble backend |
| Maintained kernel | [linux-thorch](https://github.com/lurenjiamax/linux-thorch) | Already compiled into `linux-thorch`; includes Thor DTS and the haptics driver/atomic-context bridge correction |
| Input routing | [InputPlumber Thor branch](https://github.com/lurenjiamax/InputPlumber/tree/feature/thor-native-ssc) | `thorch-inputplumber` built from this branch, including its Thor device configuration; required for the downstream gain interface |
| Optional controls | [HandheldDash](https://github.com/lurenjiamax/HandheldDash) | Enable vibration, select controller output and adjust gain; not required by the physical driver |

The upstream projects are [thorch-os/thorch](https://github.com/thorch-os/thorch),
[ROCKNIX](https://github.com/ROCKNIX/distribution) and
[ShadowBlip/InputPlumber](https://github.com/ShadowBlip/InputPlumber).
An upstream package with the same version number need not include the downstream
changes. Do not assume every fork has a downloadable binary release.

## 1. Install the kernel and input stack

1. Retain the currently working kernel/BSP/InputPlumber packages and boot backup.
2. Follow [kernel installation and recovery](custom-kernel-install.md). Install
   the three companion packages from one release, pass `thorch-check-boot`,
   reboot, then check again. Do not copy individual modules from another kernel.
3. Obtain a reviewed aarch64 `thorch-inputplumber` package built from the Thor
   branch above and install it with `sudo pacman -U "$INPUT_PACKAGE"`, where
   `INPUT_PACKAGE` is the downloaded package's path. Verify its supplied checksums
   first. Replacing the input daemon briefly interrupts controller input; keep
   SSH or another input device available.
4. Run `sudo systemctl restart inputplumber.service` and confirm:

   ```sh
   systemctl is-active inputplumber.service
   journalctl -b -u inputplumber.service --no-pager -n 40
   ```

The Thorch InputPlumber recipe consumes a separately produced archive and its
checksum manifest. A source checkout alone is not that archive: do not run
`makepkg` expecting it to download a complete Thor binary. If no suitable package
is published, the producer must build/package the branch with the Thorch rootfs
configuration first. Do not replace `/usr/bin/inputplumber` with an unowned binary
or install two competing input services.

## 2. Verify the board-specific correction

The Thor override sets `qcom,vmax-mv` to **1300 mV** and removes `qcom,use-erm`,
matching the inspected original Android configuration. The driver interprets
`qcom,use-erm` by presence: assigning zero still selects ERM mode. Removing it
selects the LRA path instead of forcing unmodulated ERM drive/braking and
disabling direct-play auto resonance. Keep the override in Thor's DTS; other
AYN boards have not been validated with it.

Read the running device tree, rather than assuming the package was booted:

```sh
python3 - <<'PY'
from pathlib import Path
import struct
nodes = list(Path('/sys/firmware/devicetree/base').rglob('qcom,hv-haptics@*'))
if len(nodes) != 1:
    raise SystemExit('Expected one HV haptics node; inspect this board manually')
node = nodes[0]
voltage = struct.unpack('>I', (node / 'qcom,vmax-mv').read_bytes())[0]
erm = (node / 'qcom,use-erm').exists()
print('vmax-mv:', voltage, 'use-erm present:', erm)
if voltage != 1300 or erm:
    raise SystemExit('Thor haptics override is not active')
PY
```

Do not compensate a wrong device tree by raising userspace gain. The separate
kernel bridge fix defers callbacks that may sleep out of atomic context; the
voltage/mode correction alone does not fix that driver bug.

## 3. Enable and check vibration

Open HandheldDash as the desktop user, enable vibration and start with a low
gain. Explicitly trigger one short preview. Verify that it stops promptly;
then test a game with rumble enabled and a controller protocol it supports.
Gain adjustment requires the downstream InputPlumber `RumbleGain` interface;
an unsupported control indicates a software mismatch, not a reason to increase
the hardware voltage.

```sh
sudo journalctl -b -k --no-pager | grep -Ei 'haptic|scheduling while atomic|BUG:|Oops:'
journalctl -b -u inputplumber.service --no-pager -n 40
```

If previews work but one game does not, inspect its controller/protocol and
rumble settings. If no preview works, check the physical gamepad and haptics
devices in `/proc/bus/input/devices` and the logs before changing gain.
Stop testing on kernel warnings, stuck vibration or unexpected heating.

## Limits and rollback

Short physical playback and stop have been exercised on the development Thor.
Android-equivalent waveform fidelity and subjective feel are not established.
Charging and suspend/resume remain separate unresolved platform issues.

To roll back, reinstall the retained compatible InputPlumber package and restart
its service. For kernel/BSP rollback follow the kernel guide; validate the boot
image before rebooting. No Android partition, calibration file or userdata key
needs changing for this procedure.
