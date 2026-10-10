# Experimental AYN Thor SM8550 Kernel Package

## Supported scope

- Target: **AYN Thor with Qualcomm SM8550**, aarch64 Thorch/Arch Linux ARM.
- Package: **linux-thorch 7.1.2-20**, kernel release **7.1.2-thorch20**.
- Requires compatible **thorch-bsp >= 1-27**, mkinitcpio and the machine-local
  **thorch-boot-bootstrap-ready >= 1-1** marker established by update bootstrap.
- Hardware evidence applies only to AYN Thor/SM8550, manually deployed
  **7.1.2-thorch19**. The newly packaged thorch20 release is experimental and
  has not yet passed an on-device pacman installation/reboot test.
- Not supported on other SM8550 devices, other handhelds, stock Android,
  or arbitrary Arch Linux installations. No Android partition flashing.

CI retains the committed Linux source/config/patch-stack revision but imports
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
pacman -U ./linux-thorch-7.1.2-20-aarch64.pkg.tar.zst
```

This local-package procedure is an experimental exception, not a supported
public update channel. Stop if dependencies cannot be resolved or boot hooks
fail. Review hook output and run `thorch-check-boot` successfully before
rebooting. Package rollback may still be required even if hooks restore files.

## Android userdata compatibility

The package does not distribute KeyMint libraries, keys, TME transport modules
or an unlock daemon. Rebuild external transport modules for thorch20 and audit
the unlock helper's version check; thorch19 modules cannot simply be renamed.
The packaged boot-generation path must also retain the required opt-in
wrapped-key arguments. Verify the resulting command line and boot image before
using the experimental backend. See android-userdata-mount.md for limitations.

After reboot verify `uname -r` is 7.1.2-thorch20, review kernel/storage errors,
and validate a dedicated file before normal use. CPU7 interrupt placement is
not enabled by this package. Performance evidence and upstream caveats are
documented in custom-kernel-performance.md.
