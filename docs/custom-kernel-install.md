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
