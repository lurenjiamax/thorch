#!/system/bin/sh
# shellcheck shell=sh
# Root Android collector. Only sealed long-term blobs leave private staging.
set -eu
umask 077
[ "$(id -u)" = 0 ] || { echo 'Run this script as root in Android.' >&2; exit 1; }
[ "$(getprop ro.board.platform)" = kalama ] || {
    echo 'Expected the Thor SM8550/kalama Android platform.' >&2; exit 1;
}
[ "$(getprop ro.build.version.sdk)" = 33 ] || {
    echo 'Only the Android 13 backend ABI is supported by this collector.' >&2; exit 1;
}
bundle=/sdcard/Download/aynthor-userdata
stamp=$(date +%Y%m%d-%H%M%S)
work=/data/local/aynthor-userdata-$stamp
mkdir "$work"
chmod 700 "$work"
mkdir "$work/bin" "$work/collection" "$work/collection/keys"
cp "$bundle/aynthor-keymint-client" "$work/bin/client"
cp "$bundle/aynthor-keymint-libc++_shared.so" "$work/bin/libc++_shared.so"
cp "$bundle/recipient-public.pem" "$work/recipient-public.pem"
chmod 700 "$work/bin/client"
export LD_LIBRARY_PATH="$work/bin"
for name in metadata user0-de user0-ce; do
    case "$name" in
        metadata) keydir=/metadata/vold/metadata_encryption/key ;;
        user0-de) keydir=/data/misc/vold/user_keys/de/0 ;;
        user0-ce) keydir=/data/misc/vold/user_keys/ce/0/current ;;
    esac
    "$work/bin/client" --export-key-dir "$keydir" \
        --recipient-pem "$work/recipient-public.pem" \
        --encrypted-output "$work/collection/keys/$name.rsa-oaep"
done
# A library pool avoids guessing vendor dlopen dependencies on the Android side.
# It contains executable libraries, never Android user files or raw key dirs.
pool="$work/collection/library-pool"
mkdir "$pool"
for source in /vendor/lib64 /system/lib64 /apex/com.android.runtime/lib64 \
    /apex/com.android.vndk.v*/lib64; do
    [ -d "$source" ] || continue
    target="$pool$source"
    mkdir -p "$target"
    cp -RL "$source/." "$target/"
done
mkdir "$work/collection/bin"
cp -L /apex/com.android.runtime/bin/linker64 "$work/collection/bin/linker64"
{
    echo 'format=aynthor-userdata-collection-v1'
    echo 'sealed_blobs=RSA-OAEP-SHA256'
    echo 'saved_ephemeral_keys=no'
    echo "android_sdk=$(getprop ro.build.version.sdk)"
    echo "android_release=$(getprop ro.build.version.release)"
    echo "android_os_patch=$(getprop ro.build.version.security_patch)"
    echo "android_vendor_patch=$(getprop ro.vendor.build.security_patch)"
    echo "firmware=$(getprop ro.build.fingerprint)"
} > "$work/collection/manifest.txt"
archive=/sdcard/Download/aynthor-userdata-$stamp.tar.gz
[ ! -e "$archive" ] || exit 1
tar -czf "$archive" -C "$work/collection" .
echo "Collection complete: $archive"
echo "Keep this archive and its recipient private key private. Do not upload them."
echo "Private staging retained at $work; no Android data/key files were modified."
