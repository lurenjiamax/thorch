# Experimental Android Userdata Mounting on Thor

## Status and safety boundary

This branch supplies kernel support, **not a complete unlock application**.
The steps below describe the already-provisioned experimental Thor setup used
for validation. A fresh install cannot follow them until the separate backend,
original device keyblobs and matching TME transport modules are provisioned.
Those components are not packaged or redistributed by this branch.

Original Android keys unlocked metadata and user0 files on the tested device;
a dedicated 158-byte Linux-created file was subsequently read successfully in
Android. This is not a general password bypass or a production security claim.
The backend still has legacy-listener, readiness and secure-memory lifecycle
limitations. Do not enable unattended boot-time unlocking.

- Back up important data externally first. A metadata-prefix backup is not a
  complete userdata backup. Keep an independently recoverable boot image.
- Never format userdata, create replacement storage keys, run automatic fsck
  repair, or copy another device's keyblob. Do not flash Android recovery.
- Never print dm-default-key tables, passwords or ephemeral key bytes. Keep
  original keyblobs private; ephemeral conversions must not be persisted.
- Do not run repair or competing mount/unlock processes concurrently.
- Verify partition identities on your device; the tested Thor used userdata
  /dev/sda17 and metadata /dev/sda13. Do not assume these names universally.

## 1. Build and boot the kernel

Follow [the build documentation](build.md) using this branch's normal source
build, which applies the configured ROCKNIX patch directories followed by
packages/linux-thorch/patches. Preserve a working boot image and recovery
method before installation. The package release is 20; the manually deployed
hardware-tested prototype was 7.1.2-thorch19.

Verify dm-default-key and quota_v2 modules match the running kernel. The
experimental legacy hardware-wrapped path requires both boot arguments:

```text
qcom_ice.use_wrapped_keys=1 qcom_ice.android_legacy_hwkm=1
```

Use the project's boot-image tooling rather than blindly appending arguments:
Thor's boot command-line field has a strict size limit. Installing a regular
package can replace a manually repacked image and its experimental arguments.

## 2. Provision the separate backend

The validated setup used this private layout (names are prerequisites, not
files included in the repository):

```text
/var/lib/aynthor/android/
  test-thor-metadata-unlock.sh
  tme-modules/<running-kernel-release>/
    aynthor_qmp.ko
    tme_overlay_loader.ko
    aynthor_tme.ko
  keymint-private/
    metadata.keyblob
    <original user0 DE and CE keyblobs expected by the backend>
  keymint-runtime/
    bin/linker64
    bin/native-convert
    lib64/<device-specific libraries and compatibility bridges>
```

The helper loads the transport modules, dm-default-key and quota_v2, then
invokes the Bionic KeyMint converter with the private metadata keyblob. Its
validated runtime uses conversion-compat.so and libaynthor-mink-bridge.so.
Vendor library acquisition, credential handling and secure provisioning are
separate work; there is no generic download/install command here. Confirm
legal redistribution rights and private permissions. Do not commit this tree.

The tested helper checks for exactly 7.1.2-thorch19. For a thorch20 rebuild,
rebuild transport modules and audit the helper's release check rather than
renaming old modules or bypassing ABI checks. This combination is unverified.

## 3. Start with read-only validation

On the already-provisioned tested setup, as root:

```bash
bash /var/lib/aynthor/android/test-thor-metadata-unlock.sh --test-metadata-readonly
bash /var/lib/aynthor/android/test-thor-metadata-unlock.sh --test-user0-readonly
```

These are temporary probes, not a persistent read-only mount. They must
validate the decrypted filesystem and clean up mapper/key state before
continuing. If they fail, stop and investigate; do not repair or generate keys.
Buffered block-device reads previously produced a misleading decrypted view;
filesystem diagnostics require the separately validated direct-I/O path.

If a stale probe device or mountpoint remains after an interrupted process,
inspect its ownership, type and mount/mapper state first. Do not recursively
delete the mountpoint, which could contain mounted personal data.

## 4. Explicit read/write mount

Only after verifying partition identity, backup, clean probe state and absence
of fsck. The tested userdata block device is used below:

```bash
blockdev --setrw /dev/sda17
systemd-run --unit=aynthor-userdata-mount \
  --property=TimeoutStopSec=90 --property=LimitCORE=0 \
  /bin/bash /var/lib/aynthor/android/test-thor-metadata-unlock.sh --mount-user0-rw
systemctl status aynthor-userdata-mount.service
findmnt -M /mnt/android-data
```

Wait for successful startup before accessing /mnt/android-data/media/0.
The long-running backend holds per-boot keys. The mount alone does not prove
that user0 file contents are unlocked. Verify only an explicitly designated
test file; do not publish personal filenames or contents. Keep F2FS fsync_mode
and barriers unchanged. Do not copy Android's nobarrier setting for a score.

## 5. Clean shutdown before switching systems

Close applications and shells using the mount, then:

```bash
systemctl stop aynthor-userdata-mount.service
findmnt -M /mnt/android-data
dmsetup ls --target default-key
```

Expect no mount and no experimental default-key mapper. The helper's SIGTERM
path removes keys, unmounts and cleans up the mapper. If stopping fails or a
mount remains, investigate busy users; do not force-kill the holder or reboot
as a substitute for cleanup. Only after successful cleanup on this device:

```bash
blockdev --setro /dev/sda17
sync
```

This is a transient service, not an enabled boot service. Rebooting requires
explicit provisioning/unlocking again. Verify a dedicated test file from
Android after switching systems, without modifying unrelated files.
