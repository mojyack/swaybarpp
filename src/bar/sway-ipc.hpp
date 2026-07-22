#pragma once
#include <optional>

#include "../util/fd.hpp"

// minimal client for the sway / i3 IPC protocol.
// see https://man.archlinux.org/man/sway-ipc.7.en
namespace sway_ipc {
enum Type : uint32_t {
    run_command    = 0,
    get_workspaces = 1,
    subscribe      = 2,
};

constexpr auto event_flag      = uint32_t(1) << 31;
constexpr auto workspace_event = event_flag | 0;

struct Message {
    uint32_t    type;
    std::string payload;
};

auto connect(const char* path) -> std::optional<FileDescriptor>;
auto send(const FileDescriptor& fd, const uint32_t type, const std::string_view payload) -> bool;
auto recv(const FileDescriptor& fd) -> std::optional<Message>;
} // namespace sway_ipc
