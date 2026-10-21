// SPDX-License-Identifier: Apache-2.0
// Short-lived original-device storage-key conversion test. No key output.
#include <aidl/android/hardware/security/keymint/IKeyMintDevice.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <spawn.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/dm-ioctl.h>
#include <linux/fs.h>
#include <linux/fscrypt.h>
#include <cstddef>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <signal.h>

static volatile sig_atomic_t mount_stop;
static void request_mount_stop(int) { mount_stop = 1; }

namespace km = aidl::android::hardware::security::keymint;
using Bytes = std::vector<uint8_t>;
using Convert = ndk::ScopedAStatus (*)(void*, const Bytes&, Bytes*);

struct Secret {
    Bytes value;
    ~Secret() {
        volatile uint8_t* p = value.data();
        for (size_t i = 0; i < value.size(); ++i) p[i] = 0;
    }
};

static void erase(Bytes& bytes) {
    volatile uint8_t* p = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
}

static uint32_t disk_u32(const Bytes& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static uint64_t disk_u64(const Bytes& bytes, size_t offset) {
    return disk_u32(bytes, offset) | (static_cast<uint64_t>(disk_u32(bytes, offset + 4)) << 32);
}

static uint32_t checkpoint_crc(uint32_t crc, const uint8_t* data, size_t size) {
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return crc;
}

static bool valid_checkpoint_crc(const Bytes& block) {
    // f2fs_checkpoint: 8 node and 8 data log slots; checksum_offset at164.
    uint32_t offset = disk_u32(block, 164);
    if (offset < 192 || offset > 4092) return false;
    uint32_t crc = checkpoint_crc(0xf2f52010U, block.data(), offset);
    if (offset < 4092)
        crc = checkpoint_crc(crc, block.data() + offset + 4, 4096 - offset - 4);
    return crc == disk_u32(block, offset);
}

static bool inspect_checkpoint_readonly(int fd, const Bytes& superblock, uint64_t device_size) {
    // f2fs_super_block begins at1024: log_blocks_per_seg at20, cp_blkaddr at76.
    uint32_t log_blocks = disk_u32(superblock, 1024 + 20);
    uint32_t start = disk_u32(superblock, 1024 + 76);
    if (log_blocks != 9 || !start) return false;
    printf("{\"stage\":\"f2fs_metadata_layout\",\"checkpoint_block\":%u,"
           "\"sit_block\":%u,\"nat_block\":%u,\"ssa_block\":%u,\"main_block\":%u,"
           "\"metadata_prefix_bytes\":%llu}\n", start,
           disk_u32(superblock, 1024 + 80), disk_u32(superblock, 1024 + 84),
           disk_u32(superblock, 1024 + 88), disk_u32(superblock, 1024 + 92),
           static_cast<unsigned long long>(disk_u32(superblock, 1024 + 92)) * 4096ULL);
    const uint64_t blocks_per_segment = 1ULL << log_blocks;
    uint64_t versions[2] {};
    uint32_t flags[2] {};
    bool valid[2] {};
    for (unsigned pack = 0; pack < 2; ++pack) {
        uint64_t block = start + pack * blocks_per_segment;
        Secret first, last;
        first.value.resize(4096); last.value.resize(4096);
        if (block >= device_size / 4096 ||
            pread(fd, first.value.data(), 4096, static_cast<off_t>(block * 4096)) != 4096)
            continue;
        uint32_t length = disk_u32(first.value, 136);
        if (length <= 2 || length > blocks_per_segment ||
            block + length > device_size / 4096) continue;
        if (pread(fd, last.value.data(), 4096, static_cast<off_t>((block + length - 1) * 4096)) != 4096)
            continue;
        versions[pack] = disk_u64(first.value, 0);
        flags[pack] = disk_u32(first.value, 132);
        valid[pack] = valid_checkpoint_crc(first.value) && valid_checkpoint_crc(last.value) &&
                      versions[pack] == disk_u64(last.value, 0);
        printf("{\"stage\":\"readonly_checkpoint_pack\",\"pack\":%u,\"valid_crc_pair\":%s,"
               "\"version\":%llu,\"flags\":%u,\"clean_unmount\":%s,"
               "\"quota_needs_fsck\":%s,\"fsck_requested\":%s,\"checkpoint_error\":%s}\n",
               pack, valid[pack] ? "true" : "false",
               static_cast<unsigned long long>(versions[pack]), flags[pack],
               flags[pack] & 1 ? "true" : "false", flags[pack] & 0x800 ? "true" : "false",
               flags[pack] & 0x10 ? "true" : "false", flags[pack] & 8 ? "true" : "false");
    }
    if (!valid[0] && !valid[1]) return false;
    unsigned selected = !valid[0] || (valid[1] && static_cast<int64_t>(versions[1] - versions[0]) > 0) ? 1 : 0;
    printf("{\"stage\":\"selected_checkpoint\",\"pack\":%u,\"quota_needs_fsck\":%s,"
           "\"disk_writes\":false}\n", selected, flags[selected] & 0x800 ? "true" : "false");
    return true;
}

static size_t valid_sit_entries(const uint8_t* page) {
    size_t valid = 0;
    // F2FS on-disk sit_entry: le16 vblocks, 64-byte map, le64 mtime.
    for (size_t i = 0; i < 55; ++i) {
        const uint8_t* entry = page + i * 74;
        unsigned vblocks = entry[0] | (static_cast<unsigned>(entry[1]) << 8);
        unsigned population = 0;
        for (unsigned j = 0; j < 64; ++j) population += __builtin_popcount(entry[2 + j]);
        if ((vblocks & 1023) <= 512 && (vblocks >> 10) < 6 &&
            (vblocks & 1023) == population) ++valid;
    }
    return valid;
}

static bool inspect_sit_readonly(int fd, const char* node, const Bytes& sb) {
    uint32_t cp_start = disk_u32(sb, 1024 + 76);
    uint32_t sit_start = disk_u32(sb, 1024 + 80);
    uint32_t sit_half = disk_u32(sb, 1024 + 56) * 512 / 2;
    uint32_t cp_payload = disk_u32(sb, 1024 + 1664);
    Secret cp[2];
    for (unsigned i = 0; i < 2; ++i) {
        cp[i].value.resize(4096);
        if (pread(fd, cp[i].value.data(), 4096, static_cast<off_t>(cp_start + 512 * i) * 4096) != 4096 ||
            !valid_checkpoint_crc(cp[i].value)) return false;
    }
    unsigned selected = static_cast<int64_t>(disk_u64(cp[1].value, 0) - disk_u64(cp[0].value, 0)) > 0 ? 1 : 0;
    auto& first = cp[selected].value;
    uint32_t sit_bytes = disk_u32(first, 156), nat_bytes = disk_u32(first, 160);
    uint32_t flags = disk_u32(first, 132);
    if (!sit_bytes || sit_bytes > 1024 * 1024 || !sit_half) return false;
    size_t bitmap_offset = flags & 0x400 ? 196 + nat_bytes : cp_payload ? 4096 : 192;
    size_t extent = ((bitmap_offset + sit_bytes + 4095) / 4096) * 4096;
    if (extent > 1024 * 1024) return false;
    Secret checkpoint; checkpoint.value.resize(extent);
    if (pread(fd, checkpoint.value.data(), extent,
              static_cast<off_t>(cp_start + 512 * selected) * 4096) != static_cast<ssize_t>(extent))
        return false;
    int direct = open(node, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECT);
    void* aligned = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (direct < 0 || aligned == MAP_FAILED) {
        if (direct >= 0) close(direct);
        if (aligned != MAP_FAILED) munmap(aligned, 4096);
        return false;
    }
    printf("{\"stage\":\"sit_bitmap_layout\",\"cp_payload\":%u,\"sit_bytes\":%u,"
           "\"nat_bytes\":%u,\"bitmap_offset\":%zu}\n", cp_payload, sit_bytes, nat_bytes, bitmap_offset);
    bool ok = true;
    for (unsigned index : {0U, 179U, 180U, 181U, 500U, 2000U, 4000U, 8000U}) {
        if (index >= sit_half || index / 8 >= sit_bytes) continue;
        bool bit = checkpoint.value[bitmap_offset + index / 8] & (1U << (7 - index % 8));
        uint64_t address = sit_start + index + (bit ? sit_half : 0);
        Secret buffered, other;
        buffered.value.resize(4096); other.value.resize(4096);
        uint64_t alternate = sit_start + index + (bit ? 0 : sit_half);
        bool read_ok = pread(fd, buffered.value.data(), 4096, static_cast<off_t>(address * 4096)) == 4096 &&
                       pread(fd, other.value.data(), 4096, static_cast<off_t>(alternate * 4096)) == 4096 &&
                       pread(direct, aligned, 4096, static_cast<off_t>(address * 4096)) == 4096;
        bool equal = read_ok && !memcmp(buffered.value.data(), aligned, 4096);
        uint64_t bulk_start = address & ~255ULL;
        Secret bulk; bulk.value.resize(256 * 4096);
        posix_fadvise(fd, static_cast<off_t>(bulk_start * 4096), bulk.value.size(), POSIX_FADV_DONTNEED);
        bool bulk_read = pread(fd, bulk.value.data(), bulk.value.size(),
                               static_cast<off_t>(bulk_start * 4096)) == static_cast<ssize_t>(bulk.value.size());
        const uint8_t* bulk_page = bulk.value.data() + (address - bulk_start) * 4096;
        bool bulk_equal = bulk_read && !memcmp(bulk_page, aligned, 4096);
        printf("{\"stage\":\"sit_read_comparison\",\"page_index\":%u,\"selected_copy\":%u,"
               "\"buffered_equals_direct\":%s,\"selected_valid_entries\":%zu,"
               "\"alternate_valid_entries\":%zu,\"bulk_equals_direct\":%s,"
               "\"bulk_valid_entries\":%zu}\n", index, bit ? 1 : 0,
               equal ? "true" : "false", read_ok ? valid_sit_entries(buffered.value.data()) : 0,
               read_ok ? valid_sit_entries(other.value.data()) : 0,
               bulk_equal ? "true" : "false", bulk_read ? valid_sit_entries(bulk_page) : 0);
        if (!equal || !bulk_equal) ok = false;
    }
    volatile uint8_t* bytes = static_cast<volatile uint8_t*>(aligned);
    for (size_t i = 0; i < 4096; ++i) bytes[i] = 0;
    munmap(aligned, 4096); close(direct);
    return ok;
}

static bool inspect_fsck_readonly(const char* node, bool trace, bool direct, bool repair, bool original) {
    char report[256];
    snprintf(report, sizeof(report), "/var/lib/aynthor/android/keymint-private/fsck-%s-%d.log",
             original ? "original-repair" : repair ? "snapshot-repair" : "readonly", getpid());
    char journal_env[320];
    snprintf(journal_env, sizeof(journal_env),
             "AYNTHOR_FSCK_WRITE_JOURNAL=/var/lib/aynthor/android/keymint-private/fsck-beforeimages-%d.raw", getpid());
    int log = open(report, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (log < 0 || input < 0) {
        if (log >= 0) close(log);
        if (input >= 0) close(input);
        return false;
    }
    posix_spawn_file_actions_t actions;
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc) { close(log); close(input); return false; }
    rc = posix_spawn_file_actions_adddup2(&actions, log, STDOUT_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, log, STDERR_FILENO);
    if (!rc) rc = posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
    char* args[] = {const_cast<char*>("/usr/bin/fsck.f2fs"),
                    const_cast<char*>(repair ? "-y" : "--dry-run"), const_cast<char*>("--no-kernel-check"),
                    const_cast<char*>("-f"), const_cast<char*>("-q"), const_cast<char*>("1"),
                    const_cast<char*>("-N"),
                    const_cast<char*>(node), nullptr};
    // The child is glibc: never pass Bionic LD_PRELOAD/library paths or key bytes.
    char* env[] = {const_cast<char*>("PATH=/usr/bin:/bin"), const_cast<char*>("LANG=C"),
                   trace ? const_cast<char*>("LD_PRELOAD=/var/lib/aynthor/android/libfsck-read-probe.so") : nullptr,
                    direct ? const_cast<char*>("AYNTHOR_FSCK_DIRECT_READ=1") : nullptr,
                    original ? journal_env : nullptr,
                   nullptr};
    pid_t child = -1;
    if (!rc) rc = posix_spawn(&child, args[0], &actions, nullptr, args, env);
    posix_spawn_file_actions_destroy(&actions);
    close(log); close(input);
    if (rc) {
        printf("{\"stage\":\"readonly_fsck_spawn\",\"error\":%d}\n", rc);
        return false;
    }
    int status = 0;
    pid_t result;
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    int code = result == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    printf("{\"stage\":\"fsck_diagnostic\",\"exit_code\":%d,"
           "\"report\":\"%s\",\"mapper_readonly\":%s,\"snapshot_writes\":%s,\"original_repair\":%s}\n",
           code, report, repair ? "false" : "true", repair && !original ? "true" : "false",
           original ? "true" : "false");
    // A nonzero filesystem-check result is diagnostic evidence, not a reason
    // to skip mapper cleanup. Signal/launch failures are distinct.
    return repair ? code == 0 : code >= 0;
}

static bool convert_private_blob(Convert convert, void* device, const char* path,
                                 Secret& ephemeral) {
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st {};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 077) ||
        st.st_size <= 0 || st.st_size > 4096) { close(fd); return false; }
    Secret blob;
    blob.value.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < blob.value.size()) {
        ssize_t count = read(fd, blob.value.data() + done, blob.value.size() - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return false; }
        done += static_cast<size_t>(count);
    }
    close(fd);
    auto status = convert(device, blob.value, &ephemeral.value);
    return status.get() && status.isOk() && ephemeral.value.size() == 100;
}

// Match our kernel uapi (flags at offset48), NOT NDK's older __flags at76.
struct AndroidAddKey {
    fscrypt_key_specifier key_spec;
    uint32_t raw_size;
    uint32_t key_id;
    uint32_t flags;
    uint32_t reserved[7];
};
static_assert(sizeof(AndroidAddKey) == 80 && offsetof(AndroidAddKey, flags) == 48,
              "Android wrappedkey_v0 ioctl layout");

static bool install_storage_key(int root, const Bytes& ephemeral,
                                fscrypt_key_specifier* spec) {
    Secret request;
    request.value.resize(sizeof(AndroidAddKey) + ephemeral.size());
    auto* arg = reinterpret_cast<AndroidAddKey*>(request.value.data());
    arg->key_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
    arg->raw_size = static_cast<uint32_t>(ephemeral.size());
    arg->flags = 2; // Downstream FSCRYPT_ADD_KEY_FLAG_HW_WRAPPED_V0.
    memcpy(request.value.data() + sizeof(*arg), ephemeral.data(), ephemeral.size());
    int rc = ioctl(root, _IOWR('f', 23, AndroidAddKey), arg);
    printf("{\"stage\":\"fscrypt_add_wrapped_v0\",\"ok\":%s,\"errno\":%d}\n",
           rc ? "false" : "true", rc ? errno : 0);
    if (!rc) *spec = arg->key_spec;
    return !rc;
}

static bool read_media_sample(int directory, unsigned depth, size_t* visited,
                              size_t* bytes_read) {
    int duplicate = dup(directory);
    if (duplicate < 0) return false;
    DIR* entries = fdopendir(duplicate);
    if (!entries) { close(duplicate); return false; }
    bool found = false;
    while (*visited < 4096 && !found) {
        auto* entry = readdir(entries);
        if (!entry) break;
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        ++*visited;
        int fd = openat(directory, entry->d_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (fd < 0) continue;
        struct stat st {};
        if (!fstat(fd, &st)) {
            if (S_ISREG(st.st_mode) && st.st_size > 0) {
                Secret sample; sample.value.resize(4096);
                ssize_t count = read(fd, sample.value.data(), sample.value.size());
                if (count > 0) { *bytes_read = static_cast<size_t>(count); found = true; }
            } else if (S_ISDIR(st.st_mode) && depth < 4) {
                found = read_media_sample(fd, depth + 1, visited, bytes_read);
            }
        }
        close(fd);
    }
    closedir(entries);
    return found;
}

static bool test_media_file(int media, bool create, size_t* bytes_read) {
    const char* name = ".aynthor-native-storage-test-20261008.txt";
    const char payload[] = "AYN Thor native Linux storage validation\n"
                           "Original Android KeyMint keys; HWKM-v2; wrappedkey_v0.\n"
                           "Created for fsync, remount and Android readback verification.\n";
    if (create) {
        int fd = openat(media, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (fd < 0) {
            printf("{\"stage\":\"exclusive_test_file_create\",\"errno\":%d}\n", errno);
            return false;
        }
        size_t done = 0;
        bool ok = true;
        while (done < sizeof(payload) - 1) {
            ssize_t count = write(fd, payload + done, sizeof(payload) - 1 - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { ok = false; break; }
            done += static_cast<size_t>(count);
        }
        if (fsync(fd)) ok = false;
        if (close(fd)) ok = false;
        if (fsync(media)) ok = false;
        printf("{\"stage\":\"test_file_write_fsync\",\"ok\":%s,\"bytes\":%zu}\n",
               ok ? "true" : "false", done);
        // Leave any newly-created partial test file visible for diagnosis;
        // never overwrite/delete an existing file on retries.
        if (!ok) return false;
    }
    int fd = openat(media, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return false;
    Secret readback;
    readback.value.resize(sizeof(payload));
    size_t done = 0;
    bool ok = true;
    while (done < readback.value.size()) {
        ssize_t count = read(fd, readback.value.data() + done, readback.value.size() - done);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = false; break; }
        if (!count) break;
        done += static_cast<size_t>(count);
    }
    struct stat st {};
    ok = ok && !fstat(fd, &st) && S_ISREG(st.st_mode) &&
         st.st_size == static_cast<off_t>(sizeof(payload) - 1) &&
         done == sizeof(payload) - 1 && !memcmp(readback.value.data(), payload, done);
    close(fd);
    *bytes_read = done;
    printf("{\"stage\":\"dedicated_test_file_readback\",\"ok\":%s,\"bytes\":%zu}\n",
           ok ? "true" : "false", done);
    return ok;
}

static bool test_user0(int root, Convert convert, void* device, int file_test) {
    fscrypt_key_specifier installed[2] {};
    size_t added = 0;
    bool ok = true;
    for (const char* name : {"user0-de", "user0-ce"}) {
        char path[256];
        snprintf(path, sizeof(path), "/var/lib/aynthor/android/keymint-private/%s.keyblob", name);
        Secret ephemeral;
        bool converted = convert_private_blob(convert, device, path, ephemeral);
        printf("{\"stage\":\"user0_conversion\",\"key_name\":\"%s\",\"ok\":%s}\n",
               name, converted ? "true" : "false");
        if (!converted || !install_storage_key(root, ephemeral.value, &installed[added])) {
            ok = false; break;
        }
        ++added;
    }
    if (ok) {
        int media = openat(root, "media/0", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool policy_matches = false;
        size_t visited = 0, bytes_read = 0;
        bool read_ok = false;
        if (media >= 0) {
            fscrypt_get_policy_ex_arg policy {};
            policy.policy_size = sizeof(policy.policy);
            if (!ioctl(media, FS_IOC_GET_ENCRYPTION_POLICY_EX, &policy) &&
                policy.policy.version == FSCRYPT_POLICY_V2) {
                policy_matches = !memcmp(policy.policy.v2.master_key_identifier,
                                         installed[1].u.identifier, FSCRYPT_KEY_IDENTIFIER_SIZE);
            }
            if (policy_matches) {
                if (file_test) read_ok = test_media_file(media, file_test == 1, &bytes_read);
                else read_ok = read_media_sample(media, 0, &visited, &bytes_read);
            }
            close(media);
        }
        printf("{\"stage\":\"media0_ce_policy\",\"matches_original_key\":%s}\n",
               policy_matches ? "true" : "false");
        printf("{\"stage\":\"media0_file_read\",\"ok\":%s,\"bytes_read\":%zu,"
               "\"content_exported\":false,\"names_exported\":false}\n",
               read_ok ? "true" : "false", bytes_read);
        ok = policy_matches && read_ok;
    }
    if (ok && file_test == 10) {
        signal(SIGTERM, request_mount_stop);
        signal(SIGINT, request_mount_stop);
        puts("{\"stage\":\"persistent_userdata_ready\",\"mount\":\"/mnt/android-data\",\"user0_unlocked\":true,\"readonly\":false}");
        while (!mount_stop) sleep(1);
    }
    for (size_t i = 0; i < added; ++i) {
        fscrypt_remove_key_arg remove {};
        remove.key_spec = installed[i];
        int rc = ioctl(root, FS_IOC_REMOVE_ENCRYPTION_KEY, &remove);
        printf("{\"stage\":\"fscrypt_remove_probe_key\",\"ok\":%s,\"errno\":%d,"
               "\"removal_status\":%u}\n", rc ? "false" : "true", rc ? errno : 0,
               remove.removal_status_flags);
        if (rc) ok = false;
    }
    return ok;
}

static bool test_userdata_mount(const char* node, bool* retain_node,
                                Convert convert, void* device, bool user0_test,
                                int file_test) {
    const char* directory = file_test == 10 ? "/mnt/android-data" :
        "/var/lib/aynthor/android/keymint-private/userdata-readonly-probe";
    struct stat directory_stat {};
    if (mkdir(directory, 0700) &&
        (errno != EEXIST || lstat(directory, &directory_stat) ||
         !S_ISDIR(directory_stat.st_mode) || directory_stat.st_uid != 0)) {
        printf("{\"stage\":\"readonly_mount_directory\",\"errno\":%d}\n", errno);
        return false;
    }
    const bool writable = file_test == 1 || file_test == 10;
    int rc = mount(node, directory, "f2fs",
                   (writable ? 0 : MS_RDONLY) | MS_NOSUID | MS_NODEV | MS_NOEXEC,
                   writable ? "disable_roll_forward,inlinecrypt" : "norecovery,inlinecrypt");
    int error = rc ? errno : 0;
    printf("{\"stage\":\"f2fs_probe_mount\",\"readonly\":%s,\"ok\":%s,\"errno\":%d}\n",
           writable ? "false" : "true", rc ? "false" : "true", error);
    bool inspected = false;
    if (!rc) {
        DIR* root = opendir(directory);
        size_t count = 0;
        if (root) {
            errno = 0;
            while (auto* entry = readdir(root)) {
                if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) ++count;
            }
            inspected = !errno && count > 0;
            if (inspected && user0_test) inspected = test_user0(dirfd(root), convert, device, file_test);
            closedir(root);
        }
        printf("{\"stage\":\"userdata_root_read\",\"ok\":%s,\"entry_count\":%zu,"
               "\"names_exported\":false}\n", inspected ? "true" : "false", count);
        rc = umount(directory);
        printf("{\"stage\":\"f2fs_probe_unmount\",\"ok\":%s,\"errno\":%d}\n",
               rc ? "false" : "true", rc ? errno : 0);
    }
    if (rc && !error) {
        *retain_node = true; // Retain backing node/directory if unmount failed.
        return false;
    }
    if (file_test != 10) rmdir(directory);
    return inspected && !rc;
}

static int test_metadata_readonly(const Bytes& key, bool mount_test,
                                  Convert convert, void* device, bool user0_test,
                                  int file_test) {
    bool snapshot = getenv("AYNTHOR_RECOVERY_SNAPSHOT") &&
                    !strcmp(getenv("AYNTHOR_RECOVERY_SNAPSHOT"), "1");
    if (file_test == 8 && !snapshot) return 1;
    if (file_test == 9 && (snapshot || !getenv("AYNTHOR_AUTHORIZE_ORIGINAL_REPAIR") ||
                          strcmp(getenv("AYNTHOR_AUTHORIZE_ORIGINAL_REPAIR"), "1"))) return 1;
    const char* backing = snapshot ? "/dev/mapper/aynthor-recovery-cow" : "/dev/sda17";
    if (snapshot) {
        int origin = open("/dev/sda17", O_RDONLY | O_CLOEXEC);
        int readonly = 0;
        bool protected_origin = origin >= 0 && !ioctl(origin, BLKROGET, &readonly) && readonly;
        if (origin >= 0) close(origin);
        if (!protected_origin) return 1;
    }
    int source = open(backing, O_RDONLY | O_CLOEXEC);
    uint64_t size = 0;
    if (source < 0) return 1;
    int rc = ioctl(source, BLKGETSIZE64, &size);
    close(source);
    if (rc || !size || size % 4096) return 1;
    int control = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
    if (control < 0) return 1;
    Secret buffer;
    buffer.value.resize(4096);
    char name[DM_NAME_LEN];
    snprintf(name, sizeof(name), "aynthor-metadata-probe-%d", getpid());
    auto reset = [&]() {
        erase(buffer.value);
        auto* header = reinterpret_cast<dm_ioctl*>(buffer.value.data());
        header->version[0] = DM_VERSION_MAJOR;
        header->version[1] = DM_VERSION_MINOR;
        header->version[2] = DM_VERSION_PATCHLEVEL;
        header->data_size = static_cast<uint32_t>(buffer.value.size());
        header->data_start = sizeof(dm_ioctl);
        header->flags = (file_test == 1 || file_test >= 8 ? 0 : DM_READONLY_FLAG) | DM_SECURE_DATA_FLAG;
        snprintf(header->name, sizeof(header->name), "%s", name);
        return header;
    };
    reset();
    if (ioctl(control, DM_DEV_CREATE, buffer.value.data())) {
        printf("{\"stage\":\"readonly_mapper_create\",\"errno\":%d}\n", errno);
        close(control); return 1;
    }
    auto* header = reset();
    header->target_count = 1;
    auto* target = reinterpret_cast<dm_target_spec*>(buffer.value.data() + sizeof(dm_ioctl));
    target->length = size / 512;
    snprintf(target->target_type, sizeof(target->target_type), "default-key");
    char* params = reinterpret_cast<char*>(target + 1);
    char* cursor = params;
    const char cipher[] = "aes-xts-plain64 ";
    memcpy(cursor, cipher, sizeof(cipher) - 1); cursor += sizeof(cipher) - 1;
    const char hex[] = "0123456789abcdef";
    for (uint8_t byte : key) { *cursor++ = hex[byte >> 4]; *cursor++ = hex[byte & 15]; }
    snprintf(cursor, 256, " 0 %s 0 3 sector_size:4096 iv_large_sectors wrappedkey_v0", backing);
    target->next = static_cast<uint32_t>((sizeof(dm_target_spec) + strlen(params) + 1 + 7) & ~7UL);
    rc = ioctl(control, DM_TABLE_LOAD, buffer.value.data());
    int error = rc ? errno : 0;
    printf("{\"stage\":\"default_key_probe_table\",\"readonly\":%s,\"ok\":%s,\"errno\":%d}\n",
           file_test == 1 || file_test >= 8 ? "false" : "true", rc ? "false" : "true", error);
    bool magic_ok = false;
    bool mount_ok = !mount_test;
    bool checkpoint_ok = file_test != 3;
    bool fsck_ok = file_test != 4 && file_test != 6 && file_test != 7 && file_test != 8 && file_test != 9;
    bool sit_ok = file_test != 5;
    if (!rc) {
        header = reset();
        rc = ioctl(control, DM_DEV_SUSPEND, buffer.value.data());
        if (!rc) {
            // Runtime nodes must not survive a reboot or collide after an interrupted mount.
            char node_directory[] = "/dev/aynthor-userdata-XXXXXX";
            char node[128];
            bool directory_created = mkdtemp(node_directory) != nullptr;
            snprintf(node, sizeof(node), "%s/device", node_directory);
            if (directory_created && !mknod(node, S_IFBLK | 0600, static_cast<dev_t>(header->dev))) {
                int block = open(node, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
                Secret sector; sector.value.resize(4096);
                if (block >= 0) {
                    int direct = open(node, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECT);
                    void* page = mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                    ssize_t count = direct >= 0 && page != MAP_FAILED ? pread(direct, page, 4096, 0) : -1;
                    printf("{\"stage\":\"superblock_direct_read\",\"bytes\":%lld,\"errno\":%d}\n",
                           static_cast<long long>(count), count < 0 ? errno : 0);
                    if (count == 4096) memcpy(sector.value.data(), page, 4096);
                    if (page != MAP_FAILED) {
                        volatile uint8_t* bytes = static_cast<volatile uint8_t*>(page);
                        for (size_t i = 0; i < 4096; ++i) bytes[i] = 0;
                        munmap(page, 4096);
                    }
                    if (direct >= 0) close(direct);
                    if (count == 4096) {
                        uint32_t magic = 0;
                        memcpy(&magic, sector.value.data() + 1024, sizeof(magic));
                        magic_ok = magic == 0xf2f52010;
                        if (magic_ok && file_test == 3)
                            checkpoint_ok = inspect_checkpoint_readonly(block, sector.value, size);
                        if (magic_ok && file_test == 5)
                            sit_ok = inspect_sit_readonly(block, node, sector.value);
                    }
                    close(block);
                } else {
                    printf("{\"stage\":\"probe_node_open\",\"ok\":false,\"errno\":%d}\n", errno);
                }
                bool retain_node = false;
                if (magic_ok && mount_test)
                    mount_ok = test_userdata_mount(node, &retain_node, convert, device, user0_test, file_test);
                if (magic_ok && (file_test == 4 || (file_test >= 6 && file_test <= 9)))
                    fsck_ok = inspect_fsck_readonly(node, file_test != 4, file_test >= 7,
                                                  file_test >= 8, file_test == 9);
                // A failed unmount must retain its backing node for recovery.
                if (!retain_node) { unlink(node); rmdir(node_directory); }
            } else {
                printf("{\"stage\":\"probe_node_create\",\"ok\":false,\"errno\":%d,"
                       "\"superblock_read_attempted\":false}\n", errno);
                if (directory_created) rmdir(node_directory);
            }
        }
        printf("{\"stage\":\"decrypted_f2fs_superblock\",\"magic_valid\":%s}\n",
               magic_ok ? "true" : "false");
    }
    reset();
    int remove = ioctl(control, DM_DEV_REMOVE, buffer.value.data());
    printf("{\"stage\":\"probe_mapper_cleanup\",\"ok\":%s}\n", remove ? "false" : "true");
    close(control);
    return magic_ok && mount_ok && checkpoint_ok && fsck_ok && sit_ok && !remove ? 0 : 1;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const bool read_test = argc == 3 && !strcmp(argv[2], "--test-metadata-readonly");
    const bool mount_test = argc == 3 && !strcmp(argv[2], "--test-userdata-mount-readonly");
    const bool user0_test = argc == 3 && !strcmp(argv[2], "--test-user0-readonly");
    const bool write_test = argc == 3 && !strcmp(argv[2], "--create-user0-test-file-rw");
    const bool verify_test = argc == 3 && !strcmp(argv[2], "--verify-user0-test-file-readonly");
    const bool checkpoint_test = argc == 3 && !strcmp(argv[2], "--inspect-checkpoint-readonly");
    const bool fsck_test = argc == 3 && !strcmp(argv[2], "--inspect-fsck-readonly");
    const bool sit_test = argc == 3 && !strcmp(argv[2], "--inspect-sit-readonly");
    const bool trace_test = argc == 3 && !strcmp(argv[2], "--inspect-fsck-trace-readonly");
    const bool direct_test = argc == 3 && !strcmp(argv[2], "--inspect-fsck-direct-readonly");
    const bool repair_test = argc == 3 && !strcmp(argv[2], "--repair-fsck-snapshot");
    const bool original_test = argc == 3 && !strcmp(argv[2], "--repair-fsck-original-with-journal");
    const bool persistent_test = argc == 3 && !strcmp(argv[2], "--mount-user0-rw");
    if ((argc != 2 && !read_test && !mount_test && !user0_test && !write_test && !verify_test && !checkpoint_test && !fsck_test && !sit_test && !trace_test && !direct_test && !repair_test && !original_test && !persistent_test) ||
        geteuid()) return 2;
    if ((write_test || persistent_test) && access("/sys/module/quota_v2", F_OK)) {
        puts("{\"error\":\"quota_format_module_required_before_rw\"}");
        return 1;
    }
    const rlimit no_core {0, 0}, unlimited {RLIM_INFINITY, RLIM_INFINITY};
    if (setrlimit(RLIMIT_CORE, &no_core) || prctl(PR_SET_DUMPABLE, 0) ||
        setrlimit(RLIMIT_MEMLOCK, &unlimited) || mlockall(MCL_CURRENT | MCL_FUTURE)) {
        puts("{\"error\":\"memory_protection_failed\"}");
        return 1;
    }
    auto prepare = reinterpret_cast<int (*)()>(dlsym(RTLD_DEFAULT, "aynthor_mink_prepare"));
    auto wipe = reinterpret_cast<void (*)()>(dlsym(RTLD_DEFAULT, "aynthor_mink_wipe_buffers"));
    if (!prepare || !wipe || prepare()) {
        puts("{\"error\":\"transport_unavailable\"}");
        return 1;
    }
    struct WipeBuffers { void (*call)(); ~WipeBuffers() { call(); } } guard {wipe};
    void* library = dlopen("libqtikeymint.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { puts("{\"error\":\"vendor_library_unavailable\"}"); return 1; }
    using Factory = void* (*)(km::SecurityLevel);
    auto factory = reinterpret_cast<Factory>(dlsym(library,
        "_ZN4aidl7android8hardware8security7keymint19CreateKeyMintDeviceENS3_13SecurityLevelE"));
    auto convert = reinterpret_cast<Convert>(dlsym(library,
        "_ZN4aidl7android8hardware8security7keymint20AndroidKeyMintDevice28convertStorageKeyToEphemeralERKNSt3__16vectorIhNS5_9allocatorIhEEEEPS9_"));
    if (!factory || !convert) return 1;
    void* device = factory(km::SecurityLevel::TRUSTED_ENVIRONMENT);
    if (!device) return 1;
    // Only open the persistent blob after transport/vendor initialization.
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { puts("{\"error\":\"keyblob_open_failed\"}"); return 1; }
    struct stat st {};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 077) ||
        st.st_size <= 0 || st.st_size > 4096) {
        close(fd); puts("{\"error\":\"invalid_private_keyblob\"}"); return 1;
    }
    Secret blob, ephemeral;
    blob.value.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < blob.value.size()) {
        ssize_t count = read(fd, blob.value.data() + done, blob.value.size() - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return 1; }
        done += static_cast<size_t>(count);
    }
    close(fd);
    auto status = convert(device, blob.value, &ephemeral.value);
    if (!status.get()) { puts("{\"error\":\"vendor_status_abi_mismatch\"}"); return 1; }
    printf("{\"stage\":\"native_storage_key_conversion\",\"ok\":%s,"
           "\"exception\":%d,\"keymint_error\":%d,\"ephemeral_size\":%zu,"
           "\"key_bytes_exported\":false}\n", status.isOk() ? "true" : "false",
           status.getExceptionCode(), status.getServiceSpecificError(), ephemeral.value.size());
    if (!status.isOk() || ephemeral.value.empty() || ephemeral.value.size() > 128) return 1;
    return read_test || mount_test || user0_test || write_test || verify_test || checkpoint_test || fsck_test || sit_test || trace_test || direct_test || repair_test || original_test || persistent_test ?
        test_metadata_readonly(ephemeral.value, mount_test || user0_test || write_test || verify_test || persistent_test,
                               convert, device, user0_test || write_test || verify_test || persistent_test,
                               write_test ? 1 : verify_test ? 2 : checkpoint_test ? 3 : fsck_test ? 4 : sit_test ? 5 : trace_test ? 6 : direct_test ? 7 : repair_test ? 8 : original_test ? 9 : persistent_test ? 10 : 0) : 0;
}
