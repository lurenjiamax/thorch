// SPDX-License-Identifier: Apache-2.0
// Probe-only companion: the captured properties are supplied by the diagnostic
// property shim, not by Android's missing shared-memory property service.
#include <string>
#include <chrono>

namespace android::base {
bool WaitForPropertyCreation(const std::string& property, std::chrono::milliseconds) {
    return property == "ro.build.version.release" ||
           property == "ro.build.version.security_patch" ||
           property == "ro.vendor.build.security_patch";
}
}
