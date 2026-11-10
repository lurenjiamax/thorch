# Motion and firmware release, 2026-11-10

- `thorch-motion 0.5.0-11`: service-only repair retaining the installed 0.5.0-10
  runtime binaries and licenses. The supervisor waits for FastRPC/runtime inputs
  in the background. Type=exec does not hold boot waiting for SSC samples.
- `thorch-motion-firmware 1-1`: independent proprietary DSP runtime/configuration
  seeds, with provenance, bundle SHA256 and copyright notice. No installation
  hook or automatic service enablement.
- `thorch-firmware-rocknix 1-15`: kernel-loaded reference ADSP segments, including
  the matching descriptor, plus the standard public firmware inputs and notice.
  This companion package owns the live firmware paths; there are no overlapping
  paths with thorch-motion-firmware.

The firmware source bundle is checksum-pinned in source-checkouts.conf. No device
keys, serial numbers, factory calibration, persisted registry caches or dumps
are release inputs. Read-only vendor seeds are not a configured runtime; the
current importer still requires device calibration. Neutral calibration has not
been validated. Copyright attribution is not a grant of redistribution rights.

The service-only package was installed with pacman on the development Thor.
Two fresh boots with unchanged GCC40 kernel/ABL/firmware/calibration returned
accelerometer and gyroscope samples; one boot had temporary syscall tracing and
one did not. Current calibration/axis fidelity and suspend recovery are separate
acceptance items. The other firmware packages require package-content and
dependency validation before publication and are not evidence of new-device
automatic provisioning.
