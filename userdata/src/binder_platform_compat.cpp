// SPDX-License-Identifier: Apache-2.0
// Resolve the platform Binder API at runtime; do not ship vendor libraries.
#include <android/binder_parcel.h>
#include <dlfcn.h>
#include <cstdlib>

extern "C" void AParcel_markSensitive(AParcel* parcel) {
    static void* library = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
    using Mark = void (*)(AParcel*);
    static Mark mark = library ? reinterpret_cast<Mark>(
        dlsym(library, "AParcel_markSensitive")) : nullptr;
    // Refuse to perform a sensitive transaction without platform support.
    if (!mark) std::abort();
    mark(parcel);
}
