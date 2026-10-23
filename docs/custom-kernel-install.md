# Experimental AYN Thor SM8550 Kernel Package

## Supported scope

- Target: **AYN Thor with Qualcomm SM8550**, aarch64 Thorch/Arch Linux ARM.
- Package: **linux-thorch 7.1.2-30**, kernel release **7.1.2-thorch30**.
- Requires compatible **thorch-bsp >= 1-40**, mkinitcpio and the machine-local
  **thorch-boot-bootstrap-ready >= 1-1** marker established by update bootstrap.
- Hardware validation reached **7.1.2-thorch28** with TME package release 29.
  The new kernel30 / TME31 pair still needs on-device acceptance.
  Charging stability and suspend/reset remain unresolved.
- Not supported on other SM8550 devices, other handhelds, stock Android,
  or arbitrary Arch Linux installations. No Android partition flashing.

CI checks out the public kernel and TME commits in `source-checkouts.conf`,
compiles those maintained sources without rewriting them, and imports
the ROCKNIX boot template from the latest SM8550 nightly available at build
time. The official adjacent SHA-256 is mandatory: download or verification
failure stops the build without falling back to another version. PROVENANCE
records the resolved tag, download URL and verified image hash. The kernel
and TME source commits stay pinned independently. Review this provenance and
validate boot behavior on hardware; newer templates are not hardware-tested
merely because CI compiled them successfully.

## Installation prerequisites

The installed system does not need to match the build's ROCKNIX version or
source revision. These packages target Thorch/Arch Linux ARM, not a stock
ROCKNIX installation. ROCKNIX supplies build-time template/firmware inputs;
the release records those inputs. TME modules must match the packaged kernel
exactly; device firmware and Android KeyMint material have separate compatibility
requirements. Never substitute another device's firmware or private keys.

Read [update safety](update-safety.md) and retain tested SD recovery media.
Back up the existing boot image, initramfs and matching modules. Automatic
bootloader fallback is not guaranteed. Have enough space on /boot for retained
and candidate files. Close and cleanly stop any Android userdata mount/view
before updating the kernel. Do not remove dependency checks or use --nodeps
on the target device.

Older installations must first obtain a compatible Thorch BSP through their
existing trusted package source, then run as root:

```bash
thorch-update-bootstrap
```

Confirm bootstrap readiness and run `thorch-check-boot` before a kernel
transaction. This release includes the required BSP package but does not
provide a public pacman update feed. Do not install if bootstrap requirements
cannot be satisfied.

### Existing initramfs mismatch

Older BSP hooks missed system-library-only mkinitcpio rebuilds. If the checker
reports only an embedded-ramdisk mismatch, preserve `/boot/KERNEL`, both
initramfs files and matching kernel packages outside `/boot` first. Check that
the running kernel matches the installed package and the current boot image.
Then use the existing tools:

```bash
sudo thorch-rebuild-abl-kernel --source-kernel /boot/KERNEL
sudo thorch-check-boot
```

The repacker retains `KERNEL.previous` and validates before atomic replacement.
Stop on any other validation error or unknown kernel identity; do not disable
transaction hooks. Install the BSP fix before further system updates. Its
hooks cover mkinitcpio's firmware, initcpio, system-library and module triggers.
For manual initramfs regeneration outside pacman, rebuild and check `KERNEL`
with these tools before rebooting as well.

## Package installation

Download the package, INSTALL.md, PROVENANCE and SHA256SUMS from the same
release. Review the included provenance and verify checksums:

```bash
sha256sum -c SHA256SUMS
```

Checksums detect corruption; they are not package signatures. This experimental
CI package is unsigned. Trust in the GitHub release is required; do not disable
system-wide signature policy to install it. A policy requiring local package
signatures needs an independently reviewed/signed package instead.

On a supported installation, install the three companion packages together.
Older systems must first resolve any existing boot mismatch as described above.
Keep the normal full-system update policy; do not use dependency bypasses or
partial repository upgrades.

```bash
pacman -U ./thorch-bsp-1-40-aarch64.pkg.tar.zst \
  ./linux-thorch-7.1.2-30-aarch64.pkg.tar.zst \
  ./thorch-tme-modules-7.1.2-31-aarch64.pkg.tar.zst
```

This local-package procedure is an experimental exception, not a supported
public update channel. Stop if dependencies cannot be resolved or boot hooks
fail. Review hook output and run `thorch-check-boot` successfully before
rebooting. Package rollback may still be required even if hooks restore files.

## Android userdata compatibility

The companion `thorch-tme-modules` package supplies QMP, TME broadcast and
Thor-only overlay modules built against the same CI kernel. Install both
packages in one `pacman -U` transaction. It depends on the exact linux-thorch
package version; no automatic loading or unlocking is enabled. Modules are
installed under `/usr/lib/modules/7.1.2-thorch30/updates/aynthor`, and the overlay
under `/usr/lib/firmware/aynthor/tme-overlay.dtbo`. Audit the external helper's
module paths and version guard before using it. KeyMint libraries, keys and
an unlock daemon are not distributed. Suspend/resume with unlocked userdata
is not validated. Source revision and input hashes are in TME-PROVENANCE.
The packaged boot-generation path must also retain the required opt-in
wrapped-key arguments. Verify the resulting command line and boot image before
using the experimental backend. See android-userdata-mount.md for limitations.

After reboot verify `uname -r` is 7.1.2-thorch30, review kernel/storage errors,
and validate a dedicated file before normal use. CPU7 interrupt placement is
not enabled by this package. Performance evidence and upstream caveats are
documented in custom-kernel-performance.md.
