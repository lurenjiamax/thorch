#!/usr/bin/env bash
# Build only public Android collector code; never use device-private libraries.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
sdk="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
ndk="${ANDROID_NDK_ROOT:-$sdk/ndk/27.1.12297006}"
aidl="${AIDL:-$sdk/build-tools/35.0.0/aidl}"
out="$root/build/android-collector"
compiler="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang++"
mkdir -p "$out/generated/include" "$out/generated/src" "$out/package"
export TMPDIR="$out"
api="$root/userdata/third_party/keymint-v2"
mapfile -t inputs < <(find "$api" -name '*.aidl' | sort)
(( ${#inputs[@]} )) || { echo 'Missing frozen KeyMint API' >&2; exit 1; }
"$aidl" --lang=ndk --structured --stability=vintf --version=2 --min_sdk_version=33 \
  -I"$api" -h"$out/generated/include" -o"$out/generated/src" "${inputs[@]}"
mapfile -t generated < <(find "$out/generated/src" -name '*.cpp' | sort)
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -fPIE -pie \
  -I"$out/generated/include" -I"$root/userdata/third_party/binder-headers" \
  "$root/userdata/src/keymint_client.cpp" "$root/userdata/src/binder_platform_compat.cpp" "${generated[@]}" \
  -lbinder_ndk -ldl -o "$out/package/aynthor-keymint-client"
cp "$ndk/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" \
  "$out/package/aynthor-keymint-libc++_shared.so"
cp "$root/userdata/android/aynthor-userdata-collect.sh" \
  "$root/userdata/android/aynthor-userdata-collect-main.sh" "$out/package/"
(cd "$out/package"; sha256sum aynthor-* > SHA256SUMS)
echo "Collector bundle: $out/package (no private materials included)"
