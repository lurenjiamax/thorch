// SPDX-License-Identifier: Apache-2.0
// Experimental conversion-process compatibility. Never forward vendor logs.
// The legacy readiness property means our checked MINK transport is prepared;
// this does NOT claim that Android QSEE listener services are implemented.
#include <dlfcn.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

static const char *captured_property(const char *key)
{
    // Values must come from this device's Android collection.
    if (!strcmp(key, "ro.build.version.release"))
        return getenv("AYNTHOR_ANDROID_RELEASE");
    if (!strcmp(key, "ro.build.version.security_patch"))
        return getenv("AYNTHOR_ANDROID_OS_PATCH");
    if (!strcmp(key, "ro.vendor.build.security_patch"))
        return getenv("AYNTHOR_ANDROID_VENDOR_PATCH");
    return NULL;
}

int property_get(const char *key, char *value, const char *fallback)
{
    const char *text = captured_property(key);
    if (!strcmp(key, "vendor.sys.listeners.registered")) {
        int (*ready)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "aynthor_mink_is_prepared");
        text = ready && ready() ? "true" : "false";
    }
    if (!text) text = fallback ? fallback : "";
    size_t size = strlen(text);
    if (size > 91) return 0;
    memcpy(value, text, size + 1);
    return (int)size;
}

int __system_property_get(const char *key, char *value)
{
    return property_get(key, value, "");
}

int __android_log_print(int priority, const char *tag, const char *format, ...)
{ (void)priority; (void)tag; (void)format; return 0; }
int __android_log_vprint(int priority, const char *tag, const char *format, va_list args)
{ (void)priority; (void)tag; (void)format; (void)args; return 0; }
int __android_log_buf_print(int buffer, int priority, const char *tag, const char *format, ...)
{ (void)buffer; (void)priority; (void)tag; (void)format; return 0; }
int __android_log_write(int priority, const char *tag, const char *text)
{ (void)priority; (void)tag; (void)text; return 0; }
int __android_log_buf_write(int buffer, int priority, const char *tag, const char *text)
{ (void)buffer; (void)priority; (void)tag; (void)text; return 0; }
void __android_log_assert(const char *condition, const char *tag, const char *format, ...)
{ (void)condition; (void)tag; (void)format; abort(); }
