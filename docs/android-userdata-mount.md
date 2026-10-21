# Android userdata: collect, import, mount

Experimental, AYN Thor / SM8550 with Android 13 and user 0 only. The collector
currently supports empty-authentication vold keys; unlocking the screen does
not make password-protected key directories compatible. Keep backups and a
working boot image. Suspend/reset and charging stability remain unresolved.
Do not enable automatic mounting or suspend while using this workflow.

## 1. Collect in rooted Android

On Linux, install Python `cryptography` and generate a recipient:

```bash
python3 userdata/src/keyblob_bundle.py init-recipient "$HOME/.local/share/aynthor-recipient"
```

Download the **aynthor-userdata-android-collector** artifact from the
**Android Userdata Collector** GitHub Actions workflow. Extract its four files
into Android `/sdcard/Download/aynthor-userdata/`. Copy only
`recipient-public.pem` from the Linux recipient directory into that folder.
Keep `recipient-private.pem` on Linux, private and backed up.

In an Android root shell, run this single entry point:

```sh
sh /sdcard/Download/aynthor-userdata/aynthor-userdata-collect.sh
```

The entry calls its adjacent main script. Output is
`/sdcard/Download/aynthor-userdata-<timestamp>.tar.gz`; private staging stays in
`/data/local/aynthor-userdata-<timestamp>`. The archive contains sealed,
device-bound metadata/DE/CE blobs and this firmware's library pool. It does not
modify Android keys or userdata. A collection failure is a stop condition;
do not change credentials or generate replacement keys to work around it.
Never upload this archive or the recipient private key.

## 2. Place materials on Linux

Extract your own archive into a private directory, then import on the Thor:

```bash
mkdir -m 700 "$HOME/aynthor-collection"
tar -xzf /path/to/aynthor-userdata-TIMESTAMP.tar.gz -C "$HOME/aynthor-collection"
sudo python3 userdata/import-materials.py "$HOME/aynthor-collection" \
  --recipient-private "$HOME/.local/share/aynthor-recipient/recipient-private.pem"
```

Requires `readelf` (binutils) and Python `cryptography`. The importer refuses to
overwrite existing provisioning. Final device-private layout:

```text
/var/lib/aynthor/android/                    root-only
  keymint-private/{metadata,user0-de,user0-ce}.keyblob
  keymint-runtime/properties.env             collected Android versions
  keymint-runtime/bin/{linker64,native-convert}
  keymint-runtime/lib64/                     same-firmware libraries
    conversion-compat.so
    libaynthor-mink-bridge.so
```

The importer supplies the blobs, linker, dependency libraries and properties.
Build the three open backend components on an x86-64 Linux host with Android
NDK 27.1.12297006 and build-tools 35.0.0 (the native backend uses this device's
libraries for linking; transfer them privately if building on another host):

```bash
git submodule update --init --recursive
bash scripts/build-android-collector.sh
bash scripts/build-userdata-backend.sh /path/to/keymint-runtime
```

Install `build/userdata-backend/native-convert` into runtime `bin/`, and
`conversion-compat.so` plus `libaynthor-mink-bridge.so` into runtime `lib64/`.
Use root ownership, directory mode 700 and file mode 600; give both executables
in `bin/` mode 700. Keep all these materials outside Git and public artifacts.

## 3. Validate and mount explicitly

Install matching `linux-thorch` and `thorch-tme-modules` packages using pacman;
never reuse modules from another kernel release. Boot with both arguments:

```text
qcom_ice.use_wrapped_keys=1 qcom_ice.android_legacy_hwkm=1
```

The helper currently accepts only userdata `/dev/sda17`, verified against
`/dev/disk/by-partlabel/userdata`. Stop for any other layout. Close competing
mount and filesystem-check processes. From this source checkout:

```bash
sudo bash userdata/userdata-mount --test-metadata-readonly
sudo bash userdata/userdata-mount --test-user0-readonly
```

These probes clean up after themselves; they do not leave a read-only mount.
Only after both succeed and backups are available, enable writes explicitly:

```bash
sudo blockdev --setrw /dev/disk/by-partlabel/userdata
sudo systemd-run --unit=aynthor-userdata-mount \
  --property=TimeoutStopSec=90 --property=LimitCORE=0 \
  /bin/bash "$PWD/userdata/userdata-mount" --mount-user0-rw
sudo journalctl -u aynthor-userdata-mount -n 20 --no-pager
findmnt -M /mnt/android-data
```

Wait for backend success and verify a designated non-personal test file under
`/mnt/android-data/media/0`. A mounted filesystem alone does not prove user0
contents are unlocked. Never run automatic repair, print mapper keys, or change
F2FS barriers/fsync settings.

## 4. Ordinary-user access

Install `bindfs`, then share only Android shared storage with your Linux user:

```bash
sudo bash userdata/share-userdata "$USER"
findmnt -M /mnt/aynthor-android/userdata
```

Open **`/mnt/aynthor-android/userdata`** as that user. Do not recursively chown
or chmod `/mnt/android-data`; Android ownership must be preserved.

## 5. Stop before rebooting or returning to Android

Close file managers, applications and shells using the shared directory:

```bash
sudo systemctl stop aynthor-userdata-share.service
sudo systemctl stop aynthor-userdata-mount.service
findmnt -M /mnt/aynthor-android/userdata
findmnt -M /mnt/android-data
sudo dmsetup ls --target default-key
```

Both mounts and the experimental mapper must be gone. If cleanup fails,
investigate busy users; do not force-unmount or kill the key holder. After
successful cleanup, `sudo blockdev --setro /dev/disk/by-partlabel/userdata`
and `sync`. Validate only your designated test file after booting Android.

The older on-device backend mounted userdata and passed an Android readback.
This new collection/import workflow still needs end-to-end device acceptance;
a successful build or Actions run does not establish that acceptance.
