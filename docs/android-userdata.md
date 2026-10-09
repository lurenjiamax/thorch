# Android shared storage adaptation

## Current status (2026-10-09)

The sections below retain chronological bring-up observations; statements
about missing support or unperformed mounts describe earlier stages, not the
current tested prototype. Patches 0231–0235 enabled original-key metadata and
user0 file access on manually deployed thorch19. Read/write mounting, clean
unmount and Android readback of a dedicated Linux-created file succeeded.
Patch 0236 adds the experimental UFS Gear 5 correction. A production unlock
daemon and redistributable runtime are not included. See the current
[mount procedure](android-userdata-mount.md) and
[performance report](custom-kernel-performance.md) for prerequisites and limits.

Work branch: `feat/android-userdata-fbe`.

The target is the existing Android `/sdcard`, backed by `/data/media/0` on
userdata, rather than Android system/vendor partitions. The first hardware
milestone is a read-only mount that returns actual file names and contents.

## Observed device

The Thor runs Android 13 with vendor kernel
`5.15.123-android13-8-gafd857749d1f`. Android's built-in script runner executes
each line independently through a root service (`u:r:pservice:s0`); it is not a
whole-file shell interpreter. A single-command launcher invoking a separate
shell script successfully collected a root report and confirmed access to
`/data/media/0`. No encryption key contents were collected.

The vendor fstab specifies F2FS with `inlinecrypt` and:

```
fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0
keydirectory=/metadata/vold/metadata_encryption
metadata_encryption=aes-256-xts:wrappedkey_v0
```

Android exposes `/dev/smcinvoke`, QTI Keymaster/KeyMint services, and their vendor
libraries. A general-purpose `su`, `dmsetup`, `fscryptctl`, and `keyctl` were not
available in the diagnostic shell. This does not mean the built-in runner lacks
root privileges.

## Implemented kernel prerequisites

The kernel fragment enables device mapper, dm-crypt, fscrypt, inline encryption,
F2FS security attributes, and QCOMTEE. Kernel package release 17 keeps these
experimental artifacts distinct from installed release 16.

Patch `0230-fscrypt-android-wrappedkey-v0-identifiers.patch` adds an explicit
legacy identifier ABI behind `CONFIG_FS_ENCRYPTION_HW_WRAPPED_V0`. Android 13
uses HKDF context 1 for hardware-wrapped key identifiers; upstream uses its
separate hardware-wrapped-key context. The compatibility flag selects the old
identifier calculation while keeping upstream's default behavior.

Downstream `FS_IOC_ADD_ENCRYPTION_KEY` flags:

| flags | meaning |
| --- | --- |
| 0 | existing raw-key behavior |
| 1 | existing upstream hardware-wrapped-key behavior |
| 2 | Android wrappedkey_v0 identifier; requires CAP_SYS_ADMIN |

Flags cannot be combined. Unsupported flags and disabled legacy support return
EINVAL. Existing master keys remember their key type across removal, rejecting
attempts to reuse an identifier as a different type with EKEYREJECTED.

This ABI is downstream and provisional. TWRP's original flag 1 must be translated
to flag 2 by the Thor legacy adapter; an unmodified TWRP binary must not be
assumed compatible. This patch only preserves the identifier derivation. It
neither unwraps a storage key nor makes the hardware accept its legacy encoding.

## Preparation and compilation

Use the normal pinned ROCKNIX source sync first. Source/config preparation can
now run without downloading firmware or a boot-image template:

```bash
scripts/build-thorch-kernel.sh --prepare-only --reuse-build-dir
```

For a prepared source tree, actual ARM64 object compilation can verify the
relevant kernel code without writing any target partitions:

```bash
make -C build/kernel-src O="$PWD/build/kernel-build" ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- -j12 \
  fs/crypto/ drivers/tee/qcomtee/ drivers/md/dm-crypt.o
```

A full Image/modules/Thor DTB build is also required before a boot experiment.
The normal full build still requires an imported ROCKNIX boot template and
firmware; `--prepare-only` does not fabricate package or boot provenance.

## Initial build result

The complete `7.1.2-thorch17` Image, Thor DTB, and configured modules were
cross-compiled successfully from the pinned ROCKNIX-patched Linux 7.1.2 source.
The Image's embedded config passed the repository's `boot_image.py check-config`
against the full feature fragment. In Linux 7.1, SHA acceleration is selected
through `CRYPTO_LIB_SHA256_ARCH` and `CRYPTO_LIB_SHA512_ARCH`; the older Android
`CRYPTO_SHA2_ARM64_CE` symbol no longer exists and must not be copied blindly.

No unit tests were run (KUNIT is disabled). The legacy patch passed checkpatch
style checks with DCO/sign-off checking disabled for this local work branch.
No boot, actual key installation, or userdata mount has been verified yet. A
successful compile does not establish decryption or read/write correctness.

## Remaining decryption chain

1. Validate the new Android dm-default-key port at runtime, including
   skip-default-key bio behavior. The source now builds against the current
   block-layer API; dm-crypt does not substitute for this target when filesystem
   keys use inline encryption.
2. Validate legacy wrapped-key programming and software-secret derivation
   against the Thor's ICE/TrustZone firmware. A successful QCOMTEE build does
   not demonstrate that a secure-world call succeeds on this device.
3. Reuse TWRP/vold key retrieval and credential handling through compatible
   KeyMint services. Evaluate Qualcomm's open MINK/QCOMTEE implementation as
   the transport rather than writing a new secure-world protocol. Proprietary
   Android vendor libraries require a compatible runtime; copying them alone
   does not make them usable by glibc.
4. Create the userdata mapper, mount F2FS read-only without recovery, install
   the user-0 fscrypt key, and expose `/data/media/0` through the existing
   hardware daemon. Only then enable an application mount action.

Do not change Android fstab encryption flags to fit Linux: those flags define
the existing disk format. Do not treat an Android ephemeral key capture as a
reusable key after reboot. Passwords and unwrapped key material must not enter
persistent logs. Read/write mode remains unimplemented until compatibility,
checkpoint behavior, ownership, and Android reboot/readback are demonstrated.

## First Thor boot verification

The experimental kernel `7.1.2-thorch17` booted successfully on Thor.
The hardware daemon and persistent user application service started.
TEE version ioctl reports implementation 4 (Qualcomm QTEE), and the kernel
reports QTEE 5.2.0, ICE 4.0.1 and HWKM v2. Device mapper exposes crypt and
linear targets; default-key is not implemented yet.

The ICE driver reports that the secure firmware does not expose the complete
wrapped-key SCM interface expected by mainline. Therefore enabling
`qcom_ice.use_wrapped_keys=1` alone cannot provide the Android legacy path.
Legacy SCM and KeyMint compatibility still require adaptation. No userdata
write or decrypted `/sdcard` mount has been performed.

This is a manually installed development kernel, with modules beside release16;
the installed pacman kernel package and its preset still describe release16.
A normal kernel package update can replace this experimental boot image.
The original image is retained as `/boot/KERNEL.thorch16`; automatic bootloader
fallback is not established. Boot logs use `loglevel=7 ignore_loglevel` and
systemd status output, with `quiet` removed.

## Modular adaptation, release 18 (historical)

The chosen approach is a small core compatibility patch, a loadable
`dm-default-key.ko`, Qualcomm transport adaptation, and a separate userspace
unlock service. It does not import the complete Android kernel or replace
Arch with an Android runtime. Kernel release18 is distinct from the running
release17; no release18 image has been installed at this stage.

Patch `0231-dm-default-key-android-metadata-encryption.patch`:

- Ports the Android 13 target and its table syntax, including `wrappedkey_v0`.
  Uses the newer Android mainline fallback-aware submission and Linux 7.1
  ioctl callback signature.
- Adds the bio skip marker, preserves it through clone and software-fallback
  writes, and prevents mixing different skip states in fscrypt/block merges.
- Preserves the marker during F2FS encrypted-page garbage collection. This is
  necessary for read/write operation, not just mounting an idle filesystem.
- Exports only the three additional blk-crypto entry points needed by the
  loadable target. Configuration uses `CONFIG_DM_DEFAULT_KEY=m` and packaging
  requires the resulting module.
- Never prints table key material. Unsupported wrapped-key hardware remains
  an error; raw-key fallback is not a substitute for a wrapped key.

Patch `0232-qcom-scm-android-legacy-ice-interfaces.patch`:

- Reuses Qualcomm's public SM8550 SCM ABI for commands 0x05 (program) and
  0x06 (clear), including an explicit crypto-engine selector.
- Checks command availability; uses the existing TrustZone shared-memory
  allocator and erases temporary key buffers after a call.
- Reports a capability mask at ICE probe: bit0 program, bit1 clear, bit2
  derive-secret (0x07). It does not switch ICE into wrapped-key mode or call
  the key programming helpers yet.

The public SM8550 sources contain both `crypto-qti-tz` and `crypto-qti-hwkm`
paths with different slot/CE semantics. Read-only extraction of Thor's own
`vendor_boot_a` confirms `crypto-qti-hwkm.ko`, `hwkm.ko`, `qcom-scm.ko` and
`ufshcd-crypto-qti.ko` in its early vendor ramdisk. Relocation-aware ARM64
disassembly confirms programming uses `slot * 2` with cipher=0,
data_unit=0 and CE=0, and clearing uses `slot * 2`, CE=0. This matches
the public HWKM-v2 path, not the TZ path's CE=10. The boot image is GKI;
its embedded configuration alone does not describe these vendor modules.
Images, extracted modules and disassembly are retained under the parent
workspace's `artifacts/android-userdata/`. Boot and vendor_boot were read,
not written. The vendor/vendor_dlkm inspection mounts were read-only and
have been unmounted.
Mainline's failed *complete* wrapped-key capability check includes generate,
prepare and import commands, so it does not establish whether legacy
program/clear/derive calls are available.

Object compilation passed for dm-default-key, block, fscrypt, F2FS, qcom_scm
and ICE. Both persisted patches passed reverse dry-run against the prepared
source. The release18 Image/modules/DTB build has been started; final module
linking and boot behavior have not yet been verified. No unit tests were run.

Userspace still needs a verified KeyMint service protocol/runtime, credential
handling, and per-boot ephemeral key conversion. Qualcomm MINK IPC supplies
the transport, not an automatic implementation of Thor's proprietary KeyMint
protocol. Do not substitute a newly generated key for an existing storage key.

Completion requires real filenames/content, an explicit read/write mount,
write/fsync/readback of a dedicated new file, clean unmount, and Android
readback after switching systems. Neither object compilation nor module
loading establishes these results. The original userdata has not been written.

### Reuse references

- [Android mainline default-key](https://android.googlesource.com/kernel/common/+/refs/heads/android-mainline/drivers/md/dm-default-key.c), reviewed snapshot retained in `research/android-userdata/upstream/` in the parent workspace.
- [Eric Biggers' complete metadata-encryption RFC](https://lwn.net/Articles/994764/), documents why filesystem and block-layer changes accompany the target.
- [Qualcomm SM8550 SCM source](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8550/blob/c462ef8ffab7a58e035ee04705b16cdfced494b1/drivers/firmware/qcom_scm.c).
- [Qualcomm SM8550 HWKM path](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8550/blob/c462ef8ffab7a58e035ee04705b16cdfced494b1/drivers/soc/qcom/crypto-qti-hwkm.c).
- [Qualcomm SM8550 TZ path](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8550/blob/c462ef8ffab7a58e035ee04705b16cdfced494b1/drivers/soc/qcom/crypto-qti-tz.c).

## Earlier sources

- [TWRP vold](https://github.com/TeamWin/android_system_vold/tree/a164ba05c5fef288059774a776b2e6e1119957cf), android-12.1.
- [Android 13 fscrypt keyring](https://android.googlesource.com/kernel/common/+/d2101e7384ec08fe13cc9faf5eccaa6a84048ac9/fs/crypto/keyring.c).
- [Android 13 dm-default-key](https://android.googlesource.com/kernel/common/+/d2101e7384ec08fe13cc9faf5eccaa6a84048ac9/drivers/md/dm-default-key.c).
- [Qualcomm MINK IPC](https://github.com/qualcomm/minkipc/tree/e5e4e7ca1c39505076a43d66afb2f8da31f56c24).
- [Android hardware-wrapped-key design](https://source.android.com/docs/security/features/encryption/hw-wrapped-keys).
