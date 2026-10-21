# Experimental AYN Thor SM8550 Kernel Package

## Supported scope

- Target: **AYN Thor with Qualcomm SM8550**, aarch64 Thorch/Arch Linux ARM.
- Package: **linux-thorch 7.1.2-29**, kernel release **7.1.2-thorch29**.
- Requires compatible **thorch-bsp >= 1-27**, mkinitcpio and the machine-local
  **thorch-boot-bootstrap-ready >= 1-1** marker established by update bootstrap.
- Hardware validation reached **7.1.2-thorch28** with TME package release 29.
  The new CI kernel29 / TME30 pair still needs on-device acceptance.
  Charging stability and suspend/reset remain unresolved.
- Not supported on other SM8550 devices, other handhelds, stock Android,
  or arbitrary Arch Linux installations. No Android partition flashing.

CI checks out the public kernel and TME commits in `source-checkouts.conf`,
compiles those maintained sources without rewriting them, and imports
the ROCKNIX boot template from nightly-20261009, compressed-image SHA256
`98fd3a08a75df9d394888db86f935fccbcd1204d93d88bd9db1b70b42f6a5cc7`.
The historical nightly-20260730 download now returns 404. This explicit CI
input change means the package is not an exact copy of the manually tested
boot image; review included PROVENANCE and validate boot behavior on hardware.

## Installation prerequisites

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

Confirm bootstrap readiness before a kernel transaction. This prerelease
does not provide a public pacman update feed or a replacement BSP package.
Do not install if these prerequisites cannot be satisfied.

## Package installation

Download the package, INSTALL.md, PROVENANCE and SHA256SUMS from the same
release. Inspect the release source commit and verify checksums:

```bash
sha256sum -c SHA256SUMS
```

Checksums detect corruption; they are not package signatures. This experimental
CI package is unsigned. Trust in the GitHub release is required; do not disable
system-wide signature policy to install it. A policy requiring local package
signatures needs an independently reviewed/signed package instead.

On a supported, fully updated installation, use a complete system update first
(`pacman -Syu` through its configured trusted repositories), then the explicit
experimental package transaction:

```bash
pacman -U ./linux-thorch-7.1.2-29-aarch64.pkg.tar.zst \
  ./thorch-tme-modules-7.1.2-30-aarch64.pkg.tar.zst
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
installed under `/usr/lib/modules/7.1.2-thorch29/updates/aynthor`, and the overlay
under `/usr/lib/firmware/aynthor/tme-overlay.dtbo`. Audit the external helper's
module paths and version guard before using it. KeyMint libraries, keys and
an unlock daemon are not distributed. Suspend/resume with unlocked userdata
is not validated. Source revision and input hashes are in TME-PROVENANCE.
The packaged boot-generation path must also retain the required opt-in
wrapped-key arguments. Verify the resulting command line and boot image before
using the experimental backend. See android-userdata-mount.md for limitations.

After reboot verify `uname -r` is 7.1.2-thorch29, review kernel/storage errors,
and validate a dedicated file before normal use. CPU7 interrupt placement is
not enabled by this package. Performance evidence and upstream caveats are
documented in custom-kernel-performance.md.
