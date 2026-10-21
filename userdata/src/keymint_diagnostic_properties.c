// SPDX-License-Identifier: Apache-2.0
// DIAGNOSTIC ONLY. Bypass Android qseecomd's readiness property to observe
// initialization failures. This does NOT implement/register QSEE listeners.
// Never preload this library into an actual key-unlock operation.
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

int __android_log_print(int priority, const char *tag, const char *format, ...)
{
    (void)priority;
    fprintf(stderr, "Probe log [%s]: ", tag);
    va_list args;
    va_start(args, format);
    int result = vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return result;
}

int property_get(const char *key, char *value, const char *fallback)
{
    if (!strcmp(key, "vendor.sys.listeners.registered")) {
        fputs("Diagnostic probe: bypassing Android listener readiness wait; listeners NOT registered\n", stderr);
        memcpy(value, "true", 5);
        return 4;
    }
    // Values previously read from this Thor's Android build. Used only in
    // this no-key diagnostic process; not a generic or fabricated patchlevel.
    const char *captured = NULL;
    if (!strcmp(key, "ro.build.version.release")) captured = "13";
    if (!strcmp(key, "ro.build.version.security_patch") ||
        !strcmp(key, "ro.vendor.build.security_patch")) captured = "2024-01-01";
    if (captured) {
        strcpy(value, captured);
        return (int)strlen(captured);
    }
    static unsigned remaining_logs = 12;
    if (remaining_logs) {
        remaining_logs--;
        fprintf(stderr, "Diagnostic property request: %s\n", key);
    }
    typedef int (*GetProperty)(const char *, char *, const char *);
    GetProperty original = (GetProperty)dlsym(RTLD_NEXT, "property_get");
    return original ? original(key, value, fallback) : 0;
}

int __system_property_get(const char *key, char *value)
{
    if (!strcmp(key, "ro.build.version.release")) { strcpy(value, "13"); return 2; }
    if (!strcmp(key, "ro.build.version.security_patch") ||
        !strcmp(key, "ro.vendor.build.security_patch")) {
        strcpy(value, "2024-01-01"); return 10;
    }
    typedef int (*GetProperty)(const char *, char *);
    GetProperty original = (GetProperty)dlsym(RTLD_NEXT, "__system_property_get");
    return original ? original(key, value) : 0;
}
