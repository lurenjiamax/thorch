// SPDX-License-Identifier: Apache-2.0
// Vendor TZCom entrypoints backed by Qualcomm's public mainline-TEE adaptor.
// Experimental short-lived conversion backend, not a persistent key service.
#include <MinkCom.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include "mink_adaptor.c"
#include "objects/mem_obj.c"

static pthread_mutex_t allocation_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    Object object;
    dev_t device;
    ino_t inode;
} allocations[8];
static size_t allocation_count;
static int transport_prepared;

static void maintain_cache(void *address, size_t size)
{
    unsigned long ctr;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    size_t line = 4UL << ((ctr >> 16) & 15);
    uintptr_t start = (uintptr_t)address & ~(line - 1);
    uintptr_t end = (uintptr_t)address + size;
    for (uintptr_t p = start; p < end; p += line)
        __asm__ volatile("dc civac, %0" :: "r"(p) : "memory");
    __asm__ volatile("dsb sy" ::: "memory");
}

int aynthor_mink_prepare(void)
{
    Object root = Object_NULL, client = Object_NULL;
    int rc = MinkCom_getRootEnvObject(&root);
    if (!rc) rc = MinkCom_getClientEnvObject(root, &client);
    if (!Object_isNull(client)) Object_release(client);
    if (!Object_isNull(root)) Object_release(root);
    transport_prepared = !rc;
    return rc;
}

int aynthor_mink_is_prepared(void) { return transport_prepared; }

void aynthor_mink_wipe_buffers(void)
{
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < allocation_count; i++) {
        void *address = NULL;
        size_t size = 0;
        if (!MinkCom_getMemoryObjectInfo(allocations[i].object, &address, &size)) {
            volatile unsigned char *p = address;
            for (size_t j = 0; j < size; j++) p[j] = 0;
            maintain_cache(address, size);
        }
    }
    pthread_mutex_unlock(&allocation_lock);
}

int probe_allocate(void *self, const void *heap, size_t size,
                   unsigned flags, size_t alignment)
    __asm__("_ZN15BufferAllocator5AllocERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEmjm");

int probe_allocate(void *self, const void *heap, size_t size,
                   unsigned flags, size_t alignment)
{
    (void)self; (void)heap; (void)flags; (void)alignment;
    const char *enabled = getenv("AYNTHOR_TEE_SHM_PROBE");
    if (!enabled || strcmp(enabled, "1") || !size || size > 1024 * 1024)
        return -1;
    pthread_mutex_lock(&allocation_lock);
    int fd = -1;
    Object root = Object_NULL, memory = Object_NULL;
    if (allocation_count == 8 || MinkCom_getRootEnvObject(&root)) goto out;
    if (MinkCom_getMemoryObject(root, size, &memory)) goto out;
    struct qcomtee_memory *shared = MEMORY((struct qcomtee_object *)memory.context);
    fd = dup(shared->fd);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) {
        if (fd >= 0) close(fd);
        fd = -1;
        Object_release(memory);
        goto out;
    }
    allocations[allocation_count].object = memory;
    allocations[allocation_count].device = st.st_dev;
    allocations[allocation_count++].inode = st.st_ino;
    fprintf(stderr, "MINK bridge probe: TEE shared memory allocated size=%zu\n", size);
out:
    if (!Object_isNull(root)) Object_release(root);
    pthread_mutex_unlock(&allocation_lock);
    return fd;
}

int TZCom_getClientEnvObject(Object *client)
{
    Object root = Object_NULL;
    *client = Object_NULL;
    int rc = MinkCom_getRootEnvObject(&root);
    if (rc) return rc;
    rc = MinkCom_getClientEnvObject(root, client);
    Object_release(root);
    fprintf(stderr, "MINK bridge: client environment result=%d\n", rc);
    return rc;
}

int TZCom_getFdObject(int fd, Object *object)
{
    *object = Object_NULL;
    struct stat st;
    if (fstat(fd, &st)) return Object_ERROR_UNAVAIL;
    pthread_mutex_lock(&allocation_lock);
    for (size_t i = 0; i < allocation_count; i++) {
        if (st.st_dev == allocations[i].device && st.st_ino == allocations[i].inode) {
            *object = allocations[i].object;
            Object_retain(*object);
            pthread_mutex_unlock(&allocation_lock);
            return Object_OK;
        }
    }
    pthread_mutex_unlock(&allocation_lock);
    fputs("MINK bridge: vendor DMA-BUF object sharing not yet implemented\n", stderr);
    return Object_ERROR_UNAVAIL;
}

static int probe_sync(unsigned fd)
{
    Object memory = Object_NULL;
    if (TZCom_getFdObject((int)fd, &memory)) return -1;
    void *address = NULL;
    size_t size = 0;
    int rc = MinkCom_getMemoryObjectInfo(memory, &address, &size);
    if (!rc) {
        // TEE SHM FDs are not DMA-BUF FDs. Perform actual cache maintenance
        // on the shared mapping; do not pretend DMA_BUF_IOCTL_SYNC succeeded.
        maintain_cache(address, size);
    }
    Object_release(memory);
    return rc ? -1 : 0;
}

int probe_sync_start(void *self, unsigned fd, int type, const void *callback, void *data)
    __asm__("_ZN15BufferAllocator12CpuSyncStartEj8SyncTypeRKNSt3__18functionIFiiiPvEEES3_");
int probe_sync_end(void *self, unsigned fd, int type, const void *callback, void *data)
    __asm__("_ZN15BufferAllocator10CpuSyncEndEj8SyncTypeRKNSt3__18functionIFiiiPvEEES3_");

int probe_sync_start(void *self, unsigned fd, int type, const void *callback, void *data)
{
    (void)self; (void)type; (void)callback; (void)data;
    return probe_sync(fd);
}

int probe_sync_end(void *self, unsigned fd, int type, const void *callback, void *data)
{
    (void)self; (void)type; (void)callback; (void)data;
    return probe_sync(fd);
}
