#pragma once
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace passwd {
// hashed pin file:
//   magic "SBPW", version u8, pin_len u8, iterations u32, salt[16], hash[32]
struct PasswdFile {
    uint8_t                 pin_len;
    uint32_t                iterations;
    std::array<uint8_t, 16> salt;
    std::array<uint8_t, 32> hash;
};

auto default_path() -> std::string;
auto generate(std::string_view pin) -> std::optional<PasswdFile>;
auto verify(const PasswdFile& file, std::string_view pin) -> bool;
auto save(const char* path, const PasswdFile& file) -> bool;
auto load(const char* path) -> std::optional<PasswdFile>;
} // namespace passwd
