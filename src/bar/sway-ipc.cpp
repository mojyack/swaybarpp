#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <sys/un.h>

#include "../macros/assert.hpp"
#include "sway-ipc.hpp"

namespace sway_ipc {
auto connect(const char* const path) -> std::optional<FileDescriptor> {
    auto fd = FileDescriptor(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ensure(fd.as_handle() >= 0);
    auto address = sockaddr_un{.sun_family = AF_UNIX, .sun_path = {}};
    std::strncpy(address.sun_path, path, sizeof(address.sun_path) - 1);
    ensure(::connect(fd.as_handle(), std::bit_cast<sockaddr*>(&address), sizeof(address)) >= 0);
    return fd;
}

auto send(const FileDescriptor& fd, const uint32_t type, const std::string_view payload) -> bool {
    auto buffer = std::string();
    buffer.resize(14 + payload.size());
    std::memcpy(buffer.data(), "i3-ipc", 6);
    const auto length = uint32_t(payload.size());
    std::memcpy(buffer.data() + 6, &length, 4);
    std::memcpy(buffer.data() + 10, &type, 4);
    std::memcpy(buffer.data() + 14, payload.data(), payload.size());
    ensure(fd.write(buffer.data(), buffer.size()));
    return true;
}

auto recv(const FileDescriptor& fd) -> std::optional<Message> {
    auto header = std::array<char, 14>();
    ensure(fd.read(header.data(), header.size()));
    auto length = uint32_t();
    auto type   = uint32_t();
    std::memcpy(&length, header.data() + 6, 4);
    std::memcpy(&type, header.data() + 10, 4);

    auto payload = std::string(length, '\0');
    ensure(length == 0 || fd.read(payload.data(), length));
    return Message{.type = type, .payload = std::move(payload)};
}
} // namespace sway_ipc
