#include <cerrno>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

#include "../macros/unwrap.hpp"
#include "../util/charconv.hpp"
#include "../util/fd.hpp"
#include "slider_app.hpp"
#include "slider_window.hpp"

namespace slider {
namespace {
constexpr auto timeout_sec = 2;

auto fill_addr(sockaddr_un& addr, const char* const path) -> void {
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
}
} // namespace

auto apply_command(SliderModel& model, std::string_view cmd) -> void {
    while(!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) {
        cmd.remove_suffix(1);
    }
    if(cmd.empty() || cmd == "show") {
        return;
    }
    if(cmd[0] == '+' || cmd[0] == '-') {
        const auto sign = cmd[0] == '+' ? 1.0 : -1.0;
        unwrap(num, from_chars<int>(cmd.substr(1)));
        model.add_fraction(sign * num / 100.0);
    } else {
        model.command(cmd);
    }
}

auto socket_path(const std::string_view name) -> std::string {
    const auto runtime = std::getenv("XDG_RUNTIME_DIR");
    const auto dir     = runtime != nullptr ? runtime : "/tmp";
    return std::format("{}/{}.sock", dir, name);
}

auto forward(const char* const path, const char* const cmd) -> bool {
    const auto fd = FileDescriptor(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if(fd.as_handle() < 0) {
        return false;
    }
    auto addr = sockaddr_un();
    fill_addr(addr, path);
    if(connect(fd.as_handle(), std::bit_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        ::write(fd.as_handle(), cmd, strlen(cmd));
        return true;
    } else {
        return false;
    }
}

auto run_server(const char* const path, SliderModel& model, const char* const initial_cmd) -> int {
    ::unlink(path); // remove a stale socket file

    const auto sfd = FileDescriptor(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    ensure(sfd.as_handle() >= 0);

    auto addr = sockaddr_un();
    fill_addr(addr, path);
    ensure(bind(sfd.as_handle(), std::bit_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ensure(listen(sfd.as_handle(), 8) == 0);

    const auto timer       = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    const auto reset_timer = [&] {
        const auto its = itimerspec{
            .it_interval = {.tv_sec = 0, .tv_nsec = 0},
            .it_value    = {.tv_sec = timeout_sec, .tv_nsec = 0},
        };
        timerfd_settime(timer, 0, &its, nullptr);
    };

    auto window = SliderWindow(model, [&] { reset_timer(); });

    apply_command(model, initial_cmd);
    window.redraw();
    reset_timer();

    const auto epfd = epoll_create1(EPOLL_CLOEXEC);
    const auto add  = [&](const int fd) {
        auto event    = epoll_event{.events = EPOLLIN};
        event.data.fd = fd;
        epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    };
    const auto wlfd    = window.get_fd();
    const auto modelfd = model.fd();
    add(wlfd);
    add(sfd.as_handle());
    add(timer);
    if(modelfd >= 0) {
        add(modelfd);
    }

    auto running = true;
    auto events  = std::array<epoll_event, 8>();
    while(running && window.running) {
        window.flush();
        const auto count = epoll_wait(epfd, events.data(), events.size(), -1);
        if(count < 0) {
            if(errno == EINTR) {
                continue;
            }
            break;
        }
        for(auto i = 0; i < count; i += 1) {
            const auto fd = events[i].data.fd;
            if(fd == wlfd) {
                if(!window.dispatch()) {
                    running = false;
                }
            } else if(modelfd >= 0 && fd == modelfd) {
                if(model.dispatch()) {
                    window.redraw();
                }
            } else if(fd == sfd.as_handle()) {
                const auto cfd = accept4(sfd.as_handle(), nullptr, nullptr, SOCK_CLOEXEC);
                if(cfd >= 0) {
                    auto       buffer = std::array<char, 64>();
                    const auto len    = ::read(cfd, buffer.data(), buffer.size());
                    ::close(cfd);
                    if(len > 0) {
                        apply_command(model, std::string_view(buffer.data(), len));
                        window.redraw();
                        reset_timer();
                    }
                }
            } else if(fd == timer) {
                running = false;
            }
        }
    }

    ::unlink(path);
    return 0;
}
} // namespace slider
