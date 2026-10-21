// SPDX-License-Identifier: Apache-2.0
// Vold key directory format/hash and GCM flow follow AOSP/TWRP KeyStorage.cpp.
// This client never generates/upgrades/deletes storage keys or persists raw
// keys/ephemeral keys. Authorized long-term blobs are exported RSA-OAEP sealed.
#include <aidl/android/hardware/security/keymint/IKeyMintDevice.h>
#include <aidl/android/hardware/security/keymint/BeginResult.h>
#include <android/binder_manager.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace km = aidl::android::hardware::security::keymint;
using Bytes = std::vector<uint8_t>;

struct bio_st;
struct evp_pkey_st;
struct evp_pkey_ctx_st;
struct evp_md_st;

template<typename Function>
static Function crypto_symbol(void* library, const char* name) {
    auto function = reinterpret_cast<Function>(dlsym(library, name));
    if (!function) throw std::runtime_error("crypto_function_unavailable");
    return function;
}

static void erase(Bytes& bytes) {
    volatile uint8_t* p = bytes.data();
    for (size_t i = 0; i < bytes.size(); ++i) p[i] = 0;
}

struct Secret {
    Bytes bytes;
    ~Secret() { erase(bytes); }
};

struct File {
    int fd;
    explicit File(int value) : fd(value) {
        if (fd < 0) throw std::runtime_error("file_open_failed");
    }
    ~File() { close(fd); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

static void read_file(int dir, const char* name, Secret& result, bool optional = false) {
    int fd = openat(dir, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && optional && errno == ENOENT) return;
    File file(fd);
    struct stat st {};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        st.st_size > 1024 * 1024) throw std::runtime_error("invalid_key_file");
    result.bytes.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < result.bytes.size()) {
        ssize_t n = read(fd, result.bytes.data() + done, result.bytes.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("key_file_read_failed");
        done += static_cast<size_t>(n);
    }
}

static bool equals(const Bytes& bytes, const char* text) {
    return bytes.size() == strlen(text) && !memcmp(bytes.data(), text, bytes.size());
}

static void check(const ndk::ScopedAStatus& status, const char* stage) {
    if (status.isOk()) return;
    // Avoid vendor-provided messages, which may contain request contents.
    fprintf(stderr, "{\"stage\":\"%s\",\"exception\":%d,\"keymint_error\":%d}\n",
            stage, status.getExceptionCode(), status.getServiceSpecificError());
    throw std::runtime_error("keymint_call_failed");
}

static void application_id(int dir, Secret& app_id) {
    Secret secdiscardable, input;
    read_file(dir, "secdiscardable", secdiscardable, true);
    if (secdiscardable.bytes.empty()) return;
    // Vold personalizes SHA512 with a zero-padded 128-byte block.
    const char prefix[] = "Android secdiscardable SHA512";
    input.bytes.resize(128 + secdiscardable.bytes.size());
    memcpy(input.bytes.data(), prefix, sizeof(prefix) - 1);
    memcpy(input.bytes.data() + 128, secdiscardable.bytes.data(), secdiscardable.bytes.size());
    void* library = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error("crypto_library_unavailable");
    using Sha512 = unsigned char* (*)(const unsigned char*, size_t, unsigned char*);
    auto sha512 = reinterpret_cast<Sha512>(dlsym(library, "SHA512"));
    app_id.bytes.resize(64);
    bool success = sha512 && sha512(input.bytes.data(), input.bytes.size(), app_id.bytes.data());
    dlclose(library);
    if (!success) throw std::runtime_error("sha512_failed");
}

static void unwrap(km::IKeyMintDevice& device, const char* directory, Secret& storage_key) {
    File dir(open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    Secret version, stretching, blob, encrypted, app_id;
    read_file(dir.fd, "version", version);
    if (!equals(version.bytes, "1")) throw std::runtime_error("unsupported_vold_version");
    read_file(dir.fd, "stretching", stretching, true);
    // Android13 vold stores nopassword for empty authentication. Newer vold
    // removes this file; none is valid with an explicitly empty auth secret.
    if (!stretching.bytes.empty() && !equals(stretching.bytes, "none") &&
        !equals(stretching.bytes, "nopassword"))
        throw std::runtime_error("credential_path_required");
    read_file(dir.fd, "keymaster_key_blob", blob);
    read_file(dir.fd, "encrypted_key", encrypted);
    if (blob.bytes.empty() || encrypted.bytes.size() < 28)
        throw std::runtime_error("invalid_vold_key_directory");
    application_id(dir.fd, app_id);
    Secret nonce, body;
    nonce.bytes.assign(encrypted.bytes.begin(), encrypted.bytes.begin() + 12);
    body.bytes.assign(encrypted.bytes.begin() + 12, encrypted.bytes.end());
    using Value = km::KeyParameterValue;
    std::vector<km::KeyParameter> params {
        { km::Tag::BLOCK_MODE, Value::make<Value::Tag::blockMode>(km::BlockMode::GCM) },
        { km::Tag::PADDING, Value::make<Value::Tag::paddingMode>(km::PaddingMode::NONE) },
        { km::Tag::MAC_LENGTH, Value::make<Value::Tag::integer>(128) },
        { km::Tag::APPLICATION_ID, Value::make<Value::Tag::blob>(app_id.bytes) },
        { km::Tag::NONCE, Value::make<Value::Tag::blob>(nonce.bytes) },
    };
    struct ParamsWiper {
        std::vector<km::KeyParameter>& params;
        ~ParamsWiper() {
            for (auto& p : params)
                if (p.value.getTag() == Value::Tag::blob) erase(p.value.get<Value::Tag::blob>());
        }
    } wiper { params };
    km::BeginResult result;
    check(device.begin(km::KeyPurpose::DECRYPT, blob.bytes, params, std::nullopt, &result), "begin");
    if (!result.operation) throw std::runtime_error("missing_keymint_operation");
    struct OperationGuard {
        std::shared_ptr<km::IKeyMintOperation> operation;
        bool finished = false;
        ~OperationGuard() { if (!finished) operation->abort(); }
    } guard { result.operation };
    Secret plaintext, final;
    check(result.operation->update(body.bytes, std::nullopt, std::nullopt, &plaintext.bytes), "update");
    check(result.operation->finish(std::nullopt, std::nullopt, std::nullopt,
                                  std::nullopt, std::nullopt, &final.bytes), "finish");
    guard.finished = true;
    // Never release unauthenticated GCM plaintext before finish succeeds.
    plaintext.bytes.insert(plaintext.bytes.end(), final.bytes.begin(), final.bytes.end());
    if (plaintext.bytes.empty()) throw std::runtime_error("empty_storage_key");
    storage_key.bytes.swap(plaintext.bytes);
}

static void output_key(int fd, const Bytes& key) {
    struct stat st {};
    // Only a caller-supplied private pipe/socket may receive ephemeral bytes.
    if (fd <= STDERR_FILENO || fstat(fd, &st) ||
        !(S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode)))
        throw std::runtime_error("output_must_be_private_ipc_fd");
    size_t done = 0;
    while (done < key.size()) {
        ssize_t n = write(fd, key.data() + done, key.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("key_ipc_write_failed");
        done += static_cast<size_t>(n);
    }
}

static void sealed_export(const Bytes& storage_key, const char* recipient,
                          const char* output) {
    // Recipient is a public key. The private key never leaves the Linux host.
    Secret pem;
    File public_file(open(recipient, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    struct stat st {};
    if (fstat(public_file.fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > 16384)
        throw std::runtime_error("invalid_recipient_public_key");
    pem.bytes.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < pem.bytes.size()) {
        ssize_t n = read(public_file.fd, pem.bytes.data() + done, pem.bytes.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("recipient_read_failed");
        done += static_cast<size_t>(n);
    }
    void* library = dlopen("libcrypto.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) throw std::runtime_error("crypto_library_unavailable");
    struct LibraryGuard { void* value; ~LibraryGuard() { dlclose(value); } } library_guard { library };
    auto bio_new = crypto_symbol<bio_st* (*)(const void*, int)>(library, "BIO_new_mem_buf");
    auto bio_free = crypto_symbol<int (*)(bio_st*)>(library, "BIO_free");
    using PasswordCallback = int (*)(char*, int, int, void*);
    auto read_public = crypto_symbol<evp_pkey_st* (*)(bio_st*, evp_pkey_st**,
                                   PasswordCallback, void*)>(library, "PEM_read_bio_PUBKEY");
    auto key_free = crypto_symbol<void (*)(evp_pkey_st*)>(library, "EVP_PKEY_free");
    auto key_bits = crypto_symbol<int (*)(const evp_pkey_st*)>(library, "EVP_PKEY_bits");
    auto ctx_new = crypto_symbol<evp_pkey_ctx_st* (*)(evp_pkey_st*, void*)>(library, "EVP_PKEY_CTX_new");
    auto ctx_free = crypto_symbol<void (*)(evp_pkey_ctx_st*)>(library, "EVP_PKEY_CTX_free");
    auto init = crypto_symbol<int (*)(evp_pkey_ctx_st*)>(library, "EVP_PKEY_encrypt_init");
    auto padding = crypto_symbol<int (*)(evp_pkey_ctx_st*, int)>(library, "EVP_PKEY_CTX_set_rsa_padding");
    auto sha256 = crypto_symbol<const evp_md_st* (*)()>(library, "EVP_sha256");
    auto oaep_md = crypto_symbol<int (*)(evp_pkey_ctx_st*, const evp_md_st*)>(library, "EVP_PKEY_CTX_set_rsa_oaep_md");
    auto mgf_md = crypto_symbol<int (*)(evp_pkey_ctx_st*, const evp_md_st*)>(library, "EVP_PKEY_CTX_set_rsa_mgf1_md");
    auto encrypt = crypto_symbol<int (*)(evp_pkey_ctx_st*, uint8_t*, size_t*,
                                        const uint8_t*, size_t)>(library, "EVP_PKEY_encrypt");
    bio_st* bio = bio_new(pem.bytes.data(), static_cast<int>(pem.bytes.size()));
    if (!bio) throw std::runtime_error("recipient_parse_failed");
    evp_pkey_st* key = read_public(bio, nullptr, nullptr, nullptr);
    bio_free(bio);
    if (!key) throw std::runtime_error("recipient_parse_failed");
    struct KeyGuard {
        evp_pkey_st* value; void (*free_fn)(evp_pkey_st*);
        ~KeyGuard() { free_fn(value); }
    } key_guard { key, key_free };
    if (key_bits(key) < 4096) throw std::runtime_error("recipient_requires_rsa4096");
    evp_pkey_ctx_st* ctx = ctx_new(key, nullptr);
    if (!ctx) throw std::runtime_error("export_context_failed");
    struct ContextGuard {
        evp_pkey_ctx_st* value; void (*free_fn)(evp_pkey_ctx_st*);
        ~ContextGuard() { free_fn(value); }
    } context_guard { ctx, ctx_free };
    // Public OpenSSL/BoringSSL RSA_PKCS1_OAEP_PADDING constant is 4.
    if (init(ctx) != 1 || padding(ctx, 4) != 1 ||
        oaep_md(ctx, sha256()) != 1 || mgf_md(ctx, sha256()) != 1)
        throw std::runtime_error("export_encryption_setup_failed");
    size_t size = 0;
    if (encrypt(ctx, nullptr, &size, storage_key.data(), storage_key.size()) != 1 ||
        size == 0 || size > 16384) throw std::runtime_error("export_encryption_failed");
    Bytes ciphertext(size);
    // For RSA4096/SHA256 the blob must fit within 446 bytes. Reject larger
    // formats explicitly instead of falling back to unsafe raw-key output.
    if (encrypt(ctx, ciphertext.data(), &size, storage_key.data(), storage_key.size()) != 1)
        throw std::runtime_error("storage_blob_does_not_fit_rsa_oaep");
    ciphertext.resize(size);
    File destination(open(output, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    done = 0;
    while (done < ciphertext.size()) {
        ssize_t n = write(destination.fd, ciphertext.data() + done, ciphertext.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("sealed_export_write_failed");
        done += static_cast<size_t>(n);
    }
    if (fsync(destination.fd)) throw std::runtime_error("sealed_export_sync_failed");
}

int main(int argc, char** argv) {
    const bool probe = argc == 2 && !strcmp(argv[1], "--probe");
    const bool verify = argc == 3 && !strcmp(argv[1], "--verify-key-dir");
    const bool unlock = argc == 5 && !strcmp(argv[1], "--unlock-key-dir") &&
                        !strcmp(argv[3], "--output-fd");
    const bool export_blob = argc == 7 && !strcmp(argv[1], "--export-key-dir") &&
                             !strcmp(argv[3], "--recipient-pem") &&
                             !strcmp(argv[5], "--encrypted-output");
    if (!probe && !verify && !unlock && !export_blob) {
        fprintf(stderr, "Usage: keymint-client --probe | --verify-key-dir DIR | "
                        "--unlock-key-dir DIR --output-fd FD | "
                        "--export-key-dir DIR --recipient-pem PEM --encrypted-output FILE\n");
        return 2;
    }
    struct rlimit no_core { 0, 0 };
    setrlimit(RLIMIT_CORE, &no_core);
    if (!probe && (geteuid() != 0 || mlockall(MCL_CURRENT | MCL_FUTURE))) {
        fprintf(stderr, "{\"error\":\"root_and_locked_memory_required\"}\n");
        return 1;
    }
    try {
        // Non-blocking lookup: a missing Android service is not an endless wait.
        auto check_service = reinterpret_cast<AIBinder* (*)(const char*)>(
            dlsym(RTLD_DEFAULT, "AServiceManager_checkService"));
        if (!check_service) throw std::runtime_error("binder_service_lookup_unavailable");
        ndk::SpAIBinder binder(check_service(
            "android.hardware.security.keymint.IKeyMintDevice/default"));
        auto device = km::IKeyMintDevice::fromBinder(binder);
        if (!device) throw std::runtime_error("keymint_service_unavailable");
        km::KeyMintHardwareInfo info;
        check(device->getHardwareInfo(&info), "hardware_info");
        if (info.securityLevel != km::SecurityLevel::TRUSTED_ENVIRONMENT)
            throw std::runtime_error("hardware_keymint_required");
        if (probe) {
            printf("{\"keymint\":true,\"version\":%d,\"security_level\":%d,"
                   "\"timestamp_required\":%s}\n", info.versionNumber,
                   static_cast<int>(info.securityLevel), info.timestampTokenRequired ? "true" : "false");
            return 0;
        }
        Secret storage_key, ephemeral;
        unwrap(*device, argv[2], storage_key);
        check(device->convertStorageKeyToEphemeral(storage_key.bytes, &ephemeral.bytes), "convert");
        if (ephemeral.bytes.empty() || ephemeral.bytes.size() > 128)
            throw std::runtime_error("invalid_ephemeral_key_size");
        if (unlock) {
            char* end = nullptr;
            long fd = strtol(argv[4], &end, 10);
            if (!end || *end || fd <= 2 || fd > INT32_MAX)
                throw std::runtime_error("invalid_output_fd");
            output_key(static_cast<int>(fd), ephemeral.bytes);
        }
        if (export_blob) sealed_export(storage_key.bytes, argv[4], argv[6]);
        printf("{\"key_retrieved\":true,\"ephemeral_converted\":true,"
               "\"ephemeral_size\":%zu,\"exported_to_ipc\":%s,\"sealed_longterm_export\":%s}\n",
               ephemeral.bytes.size(), unlock ? "true" : "false", export_blob ? "true" : "false");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "{\"error\":\"%s\"}\n", e.what());
        return 1;
    }
}
