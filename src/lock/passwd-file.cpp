#include <cstring>
#include <filesystem>

#include <fcntl.h>
#include <unistd.h>

#include "../crypto/sha.hpp"
#include "../macros/unwrap.hpp"
#include "../util/fd.hpp"
#include "passwd-file.hpp"

namespace {
constexpr auto magic      = std::array<uint8_t, 4>{'S', 'B', 'P', 'W'};
constexpr auto version    = uint8_t(1);
constexpr auto iterations = uint32_t(100000);

auto compute_hash(const std::array<uint8_t, 16>& salt, const uint32_t iterations, const std::string_view pin) -> std::optional<std::array<uint8_t, 32>> {
    auto buffer = std::vector<std::byte>(salt.size() + pin.size());
    std::memcpy(buffer.data(), salt.data(), salt.size());
    std::memcpy(buffer.data() + salt.size(), pin.data(), pin.size());
    unwrap_mut(hash, crypto::sha::calc_sha256(buffer));
    buffer.resize(salt.size() + hash.size());
    for(auto i = uint32_t(1); i < iterations; i += 1) {
        std::memcpy(buffer.data() + salt.size(), hash.data(), hash.size());
        unwrap(next, crypto::sha::calc_sha256(buffer));
        hash = next;
    }
    return std::bit_cast<std::array<uint8_t, 32>>(hash);
}
} // namespace

namespace passwd {
auto default_path() -> std::string {
    if(const auto data_home = std::getenv("XDG_DATA_HOME")) {
        return std::string(data_home) + "/swaybarpp/passwd";
    }
    const auto home = std::getenv("HOME");
    return home != nullptr ? std::string(home) + "/.local/share/swaybarpp/passwd" : std::string();
}

auto generate(const std::string_view pin) -> std::optional<PasswdFile> {
    auto file    = PasswdFile();
    file.pin_len = uint8_t(pin.size());

    const auto urandom = FileDescriptor(open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    ensure(urandom.as_handle() >= 0 && urandom.read(file.salt.data(), file.salt.size()));

    file.iterations = iterations;
    unwrap(hash, compute_hash(file.salt, file.iterations, pin));
    file.hash = hash;
    return file;
}

auto verify(const PasswdFile& file, const std::string_view pin) -> bool {
    unwrap(hash, compute_hash(file.salt, file.iterations, pin));
    auto diff = 0;
    for(auto i = 0uz; i < hash.size(); i += 1) {
        diff |= hash[i] ^ file.hash[i];
    }
    return diff == 0;
}

auto save(const char* const path, const PasswdFile& file) -> bool {
    auto error_code = std::error_code();
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error_code);

    const auto fd = FileDescriptor(open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600));
    ensure(fd.as_handle() >= 0, "failed to open {}", path);
    ensure(fd.write(magic.data(), magic.size()));
    ensure(fd.write(version));
    ensure(fd.write(file.pin_len));
    ensure(fd.write(file.iterations));
    ensure(fd.write(file.salt.data(), file.salt.size()));
    ensure(fd.write(file.hash.data(), file.hash.size()));
    return true;
}

auto load(const char* const path) -> std::optional<PasswdFile> {
    const auto fd = FileDescriptor(open(path, O_RDONLY | O_CLOEXEC));
    ensure(fd.as_handle() >= 0, "failed to open {}", path);

    auto file_magic = std::array<uint8_t, 4>();
    ensure(fd.read(file_magic.data(), file_magic.size()) && file_magic == magic, "not a passwd file: {}", path);
    unwrap(file_version, fd.read<uint8_t>());
    ensure(file_version == version, "unsupported passwd file version: {}", int(file_version));

    auto file = PasswdFile();
    unwrap(pin_len, fd.read<uint8_t>());
    unwrap(iters, fd.read<uint32_t>());
    file.pin_len    = pin_len;
    file.iterations = iters;
    ensure(fd.read(file.salt.data(), file.salt.size()));
    ensure(fd.read(file.hash.data(), file.hash.size()));
    return file;
}
} // namespace passwd
