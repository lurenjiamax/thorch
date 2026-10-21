#!/usr/bin/env bash
# Build open compatibility components against the recipient's local Android libs.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
runtime="${1:?Usage: build-userdata-backend.sh /path/to/keymint-runtime}"
ndk="${ANDROID_NDK_ROOT:-$HOME/Android/Sdk/ndk/27.1.12297006}"
toolchain="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
src="$root/userdata/src"
deps="$root/userdata/third_party"
work="$root/build/userdata-backend"
generated="$root/build/android-collector/generated/include"
mkdir -p "$work/include/linux"
export TMPDIR="$work"
test -f "$runtime/lib64/libc++.so"
test -f "$runtime/lib64/libbinder_ndk.so"
test -d "$generated"
cp "$root/kernel-src/include/uapi/linux/tee.h" "$work/include/linux/tee.h"
cc="$toolchain/aarch64-linux-android33-clang"
cxx="$toolchain/aarch64-linux-android33-clang++"
"$cc" -O2 -Wall -Wextra -Werror -fPIC -c "$src/keymint_conversion_compat.c" -o "$work/compat.o"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -include "$src/vendor_libcpp_config.h"
       -nostdlib++ -L"$runtime/lib64" -Wl,--allow-shlib-undefined)
"$cxx" "${flags[@]}" -shared -fPIC "$src/keymint_diagnostic_wait.cpp" \
  "$work/compat.o" -lc++ -ldl -o "$work/conversion-compat.so"
"$cxx" "${flags[@]}" -I"$generated" -I"$deps/binder-headers" \
  "$src/keymint_native_convert.cpp" -lc++ -lbinder_ndk -ldl -o "$work/native-convert"
"$cc" -O2 -fPIC -shared -DUSE_QCBOR -D_GNU_SOURCE \
  -I"$work/include" -I"$deps/qcbor/inc" -I"$deps/quic-teec/libqcomtee/include" \
  -I"$deps/quic-teec/libqcomtee/src" -I"$deps/minkipc/libminkadaptor/include" \
  -I"$deps/minkipc/libminkadaptor/src" "$src/mink_vendor_bridge.c" \
  "$deps/qcbor/src/UsefulBuf.c" "$deps/qcbor/src/qcbor_encode.c" \
  "$deps/qcbor/src/qcbor_decode.c" "$deps/qcbor/src/qcbor_err_to_str.c" \
  "$deps/qcbor/src/ieee754.c" "$deps/quic-teec/libqcomtee/src/qcomtee_object.c" \
  "$deps/quic-teec/libqcomtee/src/objects/credentials_obj.c" \
  "$deps/minkipc/libminkadaptor/src/supplicant.c" \
  "$deps/minkipc/libminkadaptor/src/syscall.S" -lm -Wl,-z,defs \
  -Wl,--version-script="$src/mink_vendor_bridge.map" -o "$work/libaynthor-mink-bridge.so"
echo "Built native-convert and compatibility libraries in $work (not installed)"
