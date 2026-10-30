# Install the Thor kernel

Experimental support for **AYN Thor / SM8550 on Thorch/Arch Linux ARM**.
Not for stock Android, stock ROCKNIX or other handhelds. Charging stability
and suspend/reset remain unresolved; a successful build does not prove hardware
acceptance. Stop Android userdata mounts before updating.

## Requirements

- Download BSP, kernel and TME module packages from the **same Release**.
  Pacman enforces their exact dependencies; do not use `--nodeps`.
- Complete boot initialization with `sudo thorch-update-bootstrap` on older
  Thorch installations. The machine-local readiness package must be present.
- `/boot` must be the real mounted boot filesystem, with space for both the
  current and candidate images. Run `sudo thorch-check-boot` before installing.
- Preserve the current kernel packages, modules and boot files outside `/boot`.
  Keep tested SD recovery media; `KERNEL.previous` has no proven automatic
  bootloader fallback.

The installed system does **not** need the build's ROCKNIX version or source
revision. Builds use a checksum-verified ROCKNIX SM8550 boot template and record
its actual source in `PROVENANCE`. The maintained kernel and TME sources are
selected separately. Android KeyMint libraries and keys must still match their
original device and firmware; they are not included in these packages.

## Install

Download all three packages, `SHA256SUMS`, `INSTALL.md`, `PROVENANCE` and
`TME-PROVENANCE` into an otherwise empty directory. In Bash:

```bash
sha256sum -c SHA256SUMS
packages=(./thorch-bsp-*.pkg.tar.zst ./linux-thorch-*.pkg.tar.zst ./thorch-tme-modules-*.pkg.tar.zst)
[[ ${#packages[@]} == 3 ]] || { echo 'Use one package of each kind from one Release'; exit 1; }
pacman -Qp "${packages[@]}"
sudo pacman -U "${packages[@]}"
sudo thorch-check-boot
```

Packages are unsigned. Checksums detect corruption and do not replace trust in
the release publisher. Do not weaken system-wide signature policy. If local
policy requires signatures, use independently reviewed and signed packages.
Keep normal full-system upgrade policy; avoid partial repository upgrades.

Read all transaction-hook output: pacman can finish installing packages even
if a post-transaction hook fails. **Do not reboot after any hook or boot-check
failure.** The hooks retain the old boot files and modules, rebuild the new
initramfs and `KERNEL`, and publish only a validated image. Recovery of boot
files does not roll back pacman's package database; reinstall matching retained
packages when necessary.

## Verify after reboot

After installation and boot validation succeed, run `sudo systemctl reboot`.
Then compare the running release with the installed package's module directory:

```bash
uname -r
pacman -Q thorch-bsp linux-thorch thorch-tme-modules
pacman -Qlq linux-thorch | grep '/Image$'
sudo thorch-check-boot
modinfo -F vermagic aynthor_tme
systemctl --failed
sudo journalctl -b -k -p warning --no-pager
```

The kernel release in `uname -r`, the installed Image path and module vermagic
must agree. Confirm display, input, networking and the features you actually
use. Retain rollback materials until acceptance is complete. Never force-load
old modules under a new kernel.

Android userdata access is separate and never enabled automatically. Follow
[collect, import and mount](android-userdata-mount.md); kernel installation alone
does not provision keys or a vendor runtime.

## Existing initramfs mismatch

Older BSP hooks missed some system-library-only mkinitcpio rebuilds. If the
only error is an embedded-ramdisk mismatch, first back up `/boot/KERNEL`, both
initramfs files and matching packages. Confirm that the boot image, running
kernel and installed Image match, then use the existing tools:

```bash
sudo thorch-rebuild-abl-kernel --source-kernel /boot/KERNEL
sudo thorch-check-boot
```

The repacker validates before atomic replacement and retains `KERNEL.previous`.
Stop for any other error or unknown kernel identity; do not disable hooks.
Install the companion BSP before further system updates. Manual initramfs
rebuilds outside pacman also require repacking and checking `KERNEL` before reboot.

## Persistent suspend diagnostics (opt-in)

The Thor DT allocates ramoops memory without selecting a fixed physical address,
following the stock firmware's allocation approach. This provides storage for
panic/oops dumps, console output, pmsg markers and filtered function traces.
Allocation does not prove retention: validate a marker across a warm reboot
before relying on it. Power removal or firmware clearing RAM can lose records.

For a diagnostic session, set `THORCH_BOOT_DEBUG=1` in
`/etc/thorch/boot-debug.conf`, enable `thorch-pm-debug.service`, then rebuild
and check the boot image using the existing repacker before rebooting. Debug
boots remove the normal quiet/watchdog-disable arguments, keep the console active during suspend and
increase the printk/pstore log budgets. The service enables bounded function
filters for sync, ICE and TME/QMP plus per-filesystem sync logging. It refuses
to overwrite existing function tracing. Lockup/hung-task detection reports
stacks without automatically forcing panic. The buddy hard-lockup detector
depends on another CPU making progress and cannot diagnose every firmware hang.

Check `systemctl status thorch-pm-debug.service`, ramoops registration in
`dmesg`, `/sys/kernel/debug/pstore/record_ftrace`, and
`/sys/kernel/tracing/set_ftrace_filter`. Records from the previous boot appear
in `/sys/fs/pstore` or are archived by `systemd-pstore.service` under
`/var/lib/systemd/pstore`. Normal tracefs buffers are not persistent. Function
tracing and an active console can change timing; compare with diagnostics off
after locating the fault. Logs may contain private device information; do not
publish them unreviewed.

`thorch-pm-debug.service` also starts `thorch-pm-log.service`. The latter
reads `/dev/kmsg` and a dedicated PM/UFS trace instance, synchronously writing
each batch to `/var/lib/thorch/pm-logs/<boot-id>/`. It reports ready only after
both inputs and output files are open. Each stream rotates at 16 MiB, retaining
one previous file; boot directories are retained for manual archival. Check
free space before extended diagnostic sessions. These files do not depend on
ramoops retention or journal flushing. Synchronous writes alter timing and
cannot capture anything after scheduling/storage stops. The logger is in
`system.slice`, but all userspace capture still stops during the kernel's
system-wide task freeze. An empty log is not proof that a stage was never run.
Stop `thorch-pm-log.service` as well when disabling diagnostics. Ramoops
retention on Thor remains unverified; do not treat its registration as proof
of crash capture.

To disable, stop/disable `thorch-pm-debug.service`, set `THORCH_BOOT_DEBUG=0`,
repack/check and reboot. Detector runtime settings reset on reboot. A controlled
panic is a separate destructive validation step: save work and cleanly unmount
userdata first; do not trigger it as part of ordinary installation.
