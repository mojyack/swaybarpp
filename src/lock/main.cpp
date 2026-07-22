#include <array>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../color-json.hpp"
#include "../macros/unwrap.hpp"
#include "../serde/json/format.hpp"
#include "../util/assert.hpp"
#include "../util/file-io.hpp"
#include "passwd-file.hpp"
#include "window.hpp"

namespace {
auto default_config_path() -> std::string {
    const auto home = std::getenv("HOME");
    return home != nullptr ? std::string(home) + "/.config/swaybarpp/lock.json" : std::string();
}

auto spawn_command(const std::vector<std::string>& argv) -> void {
    if(argv.empty()) {
        return;
    }
    const auto pid = fork();
    if(pid < 0) {
        return;
    }
    if(pid == 0) {
        // double-fork to detach process
        if(fork() == 0) {
            auto args = std::vector<char*>();
            for(const auto& arg : argv) {
                args.push_back(const_cast<char*>(arg.c_str()));
            }
            args.push_back(nullptr);
            execvp(args[0], args.data());
            _exit(127);
        }
        _exit(0);
    }
    waitpid(pid, nullptr, 0);
}

struct LockConfig {
    SerdeFieldsBegin;
    std::optional<std::string>              SerdeField(font);
    std::optional<Color>                    SerdeField(background);
    std::optional<Color>                    SerdeField(foreground);
    std::optional<std::string>              SerdeField(passwd_file);
    std::optional<int>                      SerdeField(timeout);
    std::optional<std::vector<std::string>> SerdeField(suspend_command);
    std::optional<double>                   SerdeField(scale);
    SerdeFieldsEnd;
};
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    constexpr auto error_value = 1;

    // load config
    const auto config_path = argc > 1 ? std::string(argv[1]) : default_config_path();
    auto       config      = json::Object();
    if(const auto content = read_file(config_path.data())) {
        unwrap_v_mut(parsed, json::parse(std::string_view((const char*)content->data(), content->size())));
        config = std::move(parsed);
    }
    auto lock_config = *serde::load<serde::JsonFormat, LockConfig>(config);

    // load hashed pin
    const auto passwd_path = lock_config.passwd_file.value_or(passwd::default_path());
    unwrap_v(passwd_file, passwd::load(passwd_path.data()), "run swaybarpp-lock-passwd first");

    // create window
    const auto font       = pango_font_description_from_string(lock_config.font.value_or("sans 14").data());
    const auto background = lock_config.background.value_or(Color::from_hex(0x1d1f21ff));
    const auto foreground = lock_config.foreground.value_or(Color::from_hex(0xc5c8c6ff));
    const auto scale      = lock_config.scale.value_or(1.0);
    auto       window     = Window(background, foreground, font, passwd_file.pin_len, scale);

    // suspend on inactivity
    const auto timer       = FileDescriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    const auto reset_timer = [&] {
        if(!lock_config.timeout || !lock_config.suspend_command) {
            return;
        }
        const auto its = itimerspec{
            .it_interval = {.tv_sec = 0, .tv_nsec = 0},
            .it_value    = {.tv_sec = *lock_config.timeout, .tv_nsec = 0},
        };
        timerfd_settime(timer.as_handle(), 0, &its, nullptr);
    };

    window.on_submit   = [&](const std::string_view pin) { return passwd::verify(passwd_file, pin); };
    window.on_activity = reset_timer;
    reset_timer();

    // main loop
    const auto epfd = epoll_create1(EPOLL_CLOEXEC);
    ensure_v(epfd >= 0, "failed to create epoll");
    const auto add_fd = [epfd](const int fd) {
        auto event    = epoll_event{.events = EPOLLIN};
        event.data.fd = fd;
        dynamic_assert(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == 0);
    };
    const auto wlfd   = window.get_fd();
    const auto animfd = window.get_anim_fd();
    add_fd(wlfd);
    add_fd(timer.as_handle());
    add_fd(animfd);

    auto events = std::array<epoll_event, 8>();
    while(window.running) {
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
                    window.running = false;
                }
            } else if(fd == timer.as_handle()) {
                auto expirations = uint64_t();
                read(timer.as_handle(), &expirations, sizeof(expirations));
                spawn_command(*lock_config.suspend_command);
                reset_timer();
            } else if(fd == animfd) {
                window.on_anim_tick();
            }
        }
    }
    window.roundtrip(); // deliver a pending unlock before exiting

    pango_font_description_free(font);
    ensure_v(!window.failed, "session lock was denied or lost");
    return 0;
}
