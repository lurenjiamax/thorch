# AYN Thor accelerometer and gyroscope setup

For **AYN Thor / SM8550 with SENODIA SH5001 on Thorch (Arch Linux ARM)**.
The path is **kernel FastRPC/ADSP → private device runtime → SSC → InputPlumber
→ motion-capable virtual controller → application**. Installing a kernel alone
does not provision sensor firmware or calibration. Vibration setup is documented
[separately](thor-vibration.md).

## Repositories and required software

| Component | Repository | Required installation |
| --- | --- | --- |
| Kernel/BSP | [Thorch custom-kernel branch](https://github.com/lurenjiamax/thorch/tree/feat/thor-custom-kernel), [linux-thorch](https://github.com/lurenjiamax/linux-thorch) | Matching BSP/kernel/TME packages; kernel includes Thor ADSP heap and SensorsPD routing |
| Sensor runtime | [thorch-motion](https://github.com/lurenjiamax/thorch-motion) | `thorch-motion`: importer, supervisor, patched HexagonRPC, private corrected libssc and capture CLI |
| Runtime dependencies | [HexagonRPC Thor branch](https://github.com/lurenjiamax/hexagonrpc/tree/feature/thor-ssc-runtime), [libssc Thor branch](https://github.com/lurenjiamax/libssc/tree/feature/thor-private-runtime) | Build inputs already pinned/patched by the runtime recipe; no separate manual daemon installation |
| Controller integration | [InputPlumber Thor branch](https://github.com/lurenjiamax/InputPlumber/tree/feature/thor-native-ssc) | `thorch-inputplumber` with optional SSC source and the AYN FastRPC device configuration |
| Optional inspection | [thorch-motion](https://github.com/lurenjiamax/thorch-motion/tree/main/apps/motion-lab) | `thorch-motion-lab`, including GTK3 and SDL dependencies; launch `thor-motion-lab` as the desktop user |
| Optional control UI | [HandheldDash](https://github.com/lurenjiamax/HandheldDash) | Select a controller output; it does not supply the SSC runtime or calibration |

Use the standalone **thorch-motion repository** for runtime and Motion Lab
packaging. Do not depend on local experimental, untracked package directories in
someone else's Thorch workspace. The upstream libraries are
[linux-msm/hexagonrpc](https://github.com/linux-msm/hexagonrpc) and
[DylanVanAssche/libssc](https://codeberg.org/DylanVanAssche/libssc).

## 1. Install public software

Follow [kernel installation](custom-kernel-install.md), including the reboot and
boot validation. Confirm `/dev/fastrpc-adsp` exists. Install reviewed aarch64
`thorch-inputplumber` and `thorch-motion` packages, plus optional
`thorch-motion-lab`, using `sudo pacman -U` with their actual downloaded paths.
Verify the supplied checksums first. Do not assume similarly named upstream
packages include these downstream changes.

If binaries are unavailable, the runtime's Arch recipes are under
`packaging/arch/thorch-motion/` and `packaging/arch/thorch-motion-lab/` in the
standalone repository. Build as a normal user on an appropriately provisioned
Arch ARM system using `makepkg -s`, then install the resulting package with
pacman. Use a complete checkout: payload symlinks refer to the repository's
maintained runtime/application source. Any transferred package-input bundle
must preserve those sources or dereference the links. The recipes declare the
build/runtime dependencies; missing source/patch inputs must be resolved before
building, never bypassed with checksum skips.

At the time this guide was checked, `thorch-motion` had no published GitHub
Release packages. Its public recipe also predates local runtime fixes under
development. The available route is the published source recipe or a separately
reviewed package supplied by a maintainer; a development machine's installed
version is not proof that the same binary is publicly downloadable.

InputPlumber's Thorch packaging additionally requires a separately built artifact
and checksum manifest; see [vibration setup](thor-vibration.md). These documents
do not imply a complete binary release exists for every repository revision.

Do not overwrite system libssc, add a global `LD_LIBRARY_PATH`, or run multiple
HexagonRPC sensor daemons. The corrected library is intentionally private to
`/usr/lib/thorch-motion/ssc`.

## 2. Prepare this device's private materials

**Required before starting the service:** a read-only extraction/backup from
the same Thor's matching Android firmware and original sensor calibration.
Root access may be needed to read Android's persist calibration; ordinary ADB
directory listing does not establish permission to read it. Keep the backup
private and retain the original unmodified files.

Arrange the existing extracted material as follows:

```text
PRIVATE_SOURCE/
  dsp/adsp/                 matching ADSP-side runtime files
  socinfo/                 matching source's SoC metadata tree
  sensors/config/          original sensor configuration files
  sensors/sns_reg.conf
  sensors/sns_reg_version   version matching sns_reg.conf
  sensors/registry/         original device-owned *.fac_cal groups
```

`PRIVATE_SOURCE` is a user-selected directory, not an Android partition or a
download URL. Firmware layout varies; the importer expects an **already
extracted** tree and does not collect Android files. There is no turnkey Android
sensor collector supplied by this procedure. Do not use the userdata-key
collector as a substitute. If the materials are unavailable, stop here; public
source packages alone cannot complete sensor provisioning.

Do not use another person's persist/calibration, generate substitute factory
values, copy a complete stale registry cache, or publish these private files.
External symlinks must be materialized within the extracted source tree.

Import once, selecting the actual source directory:

```sh
read -r -p 'Extracted private runtime directory: ' PRIVATE_SOURCE
sudo thorch-motion-import \
  --source "$PRIVATE_SOURCE" \
  --destination /var/lib/aynthor/motion/runtime-root \
  --calibration-directory sensors/registry
```

The destination must not exist. If calibration is nested elsewhere in the
source, pass its real relative directory instead. The importer copies original
`.fac_cal` seeds, leaves the source untouched and excludes unrelated cache
files. Do not delete an existing runtime to make a repeated import succeed.

## 3. Start manually and verify real samples

```sh
sudo systemctl start thorch-motion.service
systemctl status thorch-motion.service --no-pager
journalctl -b -u thorch-motion.service --no-pager -n 60
thorch-motion-capture --sensor accelerometer --sample-rate 100 --timeout 10
thorch-motion-capture --sensor gyroscope --sample-rate 100 --timeout 10
```

Look for finite XYZ samples that change when the **lower body** moves. Units
are m/s² and rad/s. Requested rate is not guaranteed hardware rate, and callback
timestamps are host receipt times. Process liveness alone is insufficient.
The readiness-aware service waits for samples from both sensor types.

First-use registry bootstrap on the development Thor required a controlled
reboot after the first daemon run. If initial discovery fails after registry
generation, retain that generated runtime, stop the service, reboot once and
start it manually again. Do not repeatedly reimport, erase registry state or
hot-restart ADSP while audio is active. If it still fails, inspect the service,
FastRPC/QRTR and kernel logs rather than enabling a failing boot service.

Only after manual samples and a controlled reboot succeed on this device,
optionally enable subsequent startup with `sudo systemctl enable thorch-motion.service`.

Inspect `systemctl cat thorch-motion.service` before relying on boot or resume
behavior. The currently published unit has `Conflicts=suspend.target`: entering
sleep can stop it, and automatic resume restart must not be assumed. Later
service-lifetime changes under development are not part of that published unit.

## 4. Expose motion to games

1. Restart InputPlumber after installing its Thor package. The AYN configuration
   must include the `/dev/fastrpc-adsp` SSC source, alongside the physical gamepad.
2. In Motion Lab, explicitly select and apply **DualSense / `ds5`** if advertised
   by InputPlumber. Switching outputs recreates controller handles: close games
   first. Xbox-style output such as `xb360` does not expose SDL motion.
3. Open Motion Lab as the normal desktop user and start inspection. Check
   **SSC/body** and **SDL/controller** separately. SSC samples without SDL
   samples indicate an input-routing/target problem, not necessarily bad firmware.
4. Select that controller in the game/emulator and enable its supported gyro
   input. SDL motion does not automatically supply a DSU server or configure
   every emulator. Game support must be verified independently.

## Calibration, optional rotation and rollback

Motion Lab calibration requires explicit stationary/six-pose collection and
saving. It writes only user-local configuration under
`${XDG_CONFIG_HOME:-~/.config}/thorch-motion/`; corrected preview does not
calibrate firmware, change raw streams or apply a global axis transform.
Physical mounting, scale and independent calibration acceptance remain limited.

Desktop auto-rotation is optional and separate from game motion. The
[SensorProxy bridge](https://github.com/lurenjiamax/thorch-motion/tree/main/apps/sensorproxy)
needs an explicitly verified physical frame and can conflict with another
`net.hadess.SensorProxy` provider. Do not enable it with guessed axes or assume
one orientation applies correctly to both Thor panels.

To stop motion: `sudo systemctl disable --now thorch-motion.service`.
Retain the private runtime and calibration backup. Restore retained compatible
InputPlumber/kernel packages if necessary; follow the kernel guide before any
boot rollback. Full suspend/resume remains unresolved and enabling motion does
not fix it. Actual SSC and SDL sample delivery have been exercised on the
development Thor; that does not certify every firmware revision or game.
