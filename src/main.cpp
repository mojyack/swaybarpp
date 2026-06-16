#include <array>
#include <memory>
#include <string>
#include <vector>

#include <linux/input-event-codes.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include "macros/unwrap.hpp"
#include "util/charconv.hpp"
#include "window.hpp"

// serde support

namespace serde {
struct JsonFormat;
}

namespace serde::json {
auto serialize_element(JsonFormat& /*format*/, ::json::Value& /*value*/, const Color& /*data*/) -> bool {
    return false;
}

auto deserialize_element(JsonFormat& /*format*/, const ::json::Value& value, Color& data) -> bool {
    unwrap(node, value.get<::json::String>());
    auto str = std::string_view(node.value);
    if(str.starts_with('#')) {
        str.remove_prefix(1);
    }
    auto hex = std::string(str);
    if(hex.size() == 6) {
        hex += "ff"; // default to opaque
    }
    ensure(hex.size() == 8);
    unwrap(num, from_chars<uint32_t>(hex, 16));
    data = Color::from_hex(num);
    return true;
}
} // namespace serde::json

#include "serde/json/format.hpp"

namespace {
auto default_config_path() -> std::string {
    const auto home = std::getenv("HOME");
    return home != nullptr ? std::string(home) + "/.config/swaybarpp/config.json" : std::string();
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

struct CommonConfig {
    SerdeFieldsBegin;
    std::optional<int>         SerdeField(height);
    std::optional<std::string> SerdeField(position);
    std::optional<std::string> SerdeField(font);
    std::optional<Color>       SerdeField(foreground);
    std::optional<Color>       SerdeField(background);
    SerdeFieldsEnd;
};

auto load_modules(const int epfd, const json::Object& config, const std::string_view key) -> std::optional<std::vector<std::unique_ptr<Module>>> {
    auto       modules = std::vector<std::unique_ptr<Module>>();
    const auto mods    = config.find<json::Object>(key);
    if(!mods) {
        return modules;
    }
    for(const auto& entry : mods->children) {
        auto module = create_module(entry.key);
        ensure(module, "unknown module: {}", entry.key);
        const auto  module_config = entry.value.get<json::Object>();
        const auto  empty         = json::Object();
        const auto& cfg           = module_config != nullptr ? *module_config : empty;
        ensure(module->init(epfd, cfg), "failed to initialize module: {}", entry.key);
        if(const auto array = cfg.find<json::Array>("on_click")) {
            for(const auto& value : array->value) {
                if(const auto str = value.get<json::String>()) {
                    module->on_click.push_back(str->value);
                }
            }
        }
        modules.push_back(std::move(module));
    }
    return modules;
};

} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    // load config
    const auto config_path = argc > 1 ? std::string(argv[1]) : default_config_path();
    auto       config      = json::Object();
    if(const auto content = read_pseudo_file(config_path.c_str())) {
        unwrap_mut(parsed, json::parse(*content));
        config = std::move(parsed);
    }
    auto common_config = *serde::load<serde::JsonFormat, CommonConfig>(config);

    // setup epoll
    const auto epfd = epoll_create1(EPOLL_CLOEXEC);
    ensure(epfd >= 0, "failed to create epoll");

    // load modules
    unwrap(modules_left, load_modules(epfd, config, "mods_left"));
    unwrap(modules_right, load_modules(epfd, config, "mods_right"));

    const auto for_each_module = [&](auto&& fn) {
        for(auto& module : modules_left) {
            fn(module);
        }
        for(auto& module : modules_right) {
            fn(module);
        }
    };

    // create window
    const auto font       = pango_font_description_from_string(common_config.font.value_or("monospace 11").data());
    auto       window     = Window(common_config.height.value_or(24), common_config.position == "bottom", "swaybarpp");
    const auto background = common_config.background.value_or(Color::from_hex(0x1d1f21ff));
    const auto foreground = common_config.foreground.value_or(Color::from_hex(0xc5c8c6ff));

    window.background = background;
    window.on_draw    = [&](cairo_t* const cairo, const int w, const int h, const std::string_view output) {
        auto target    = RenderTarget{cairo, font, foreground, background};
        target.output  = output;
        auto available = Rect{0, 0, double(w), double(h)};

        target.align = Align::Left;
        for(auto& module : modules_left) {
            const auto before = available;
            module->draw(target, available);
            module->click_x0 = before.x;
            module->click_x1 = available.x;
        }

        target.align = Align::Right;
        for(auto& module : modules_right) {
            const auto before = available;
            module->draw(target, available);
            module->click_x0 = available.x + available.w;
            module->click_x1 = before.x + before.w;
        }
    };
    window.on_click = [&](const double x, const double y, const uint32_t button) {
        auto dirty = false;
        for_each_module([&](auto& module) {
            if(x < module->click_x0 || x >= module->click_x1) {
                return;
            }
            if(!module->on_click.empty()) {
                if(button == BTN_LEFT) {
                    spawn_command(module->on_click);
                }
            } else {
                dirty |= module->click(x, y, button);
            }
        });
        if(dirty) {
            window.redraw();
        }
    };
    window.on_scroll = [&](const double x, const double y, const double dx, const double dy) {
        auto dirty = false;
        for_each_module([&](auto& module) {
            if(x < module->click_x0 || x >= module->click_x1) {
                return;
            }
            dirty |= module->scroll(x, y, dx, dy);
        });
        if(dirty) {
            window.redraw();
        }
    };
    window.redraw();

    // 1 second redraw timer
    const auto timer = FileDescriptor(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK));
    ensure(timer.as_handle() >= 0, "failed to create timer");
    const auto interval = itimerspec{
        .it_interval = {.tv_sec = 1, .tv_nsec = 0},
        .it_value    = {.tv_sec = 1, .tv_nsec = 0},
    };
    timerfd_settime(timer.as_handle(), 0, &interval, nullptr);

    // register our own fds
    static auto wayland_tag = bool();
    static auto timer_tag   = bool();
    const auto  add_fd      = [epfd](const int fd, void* const tag) {
        auto event     = epoll_event{.events = EPOLLIN};
        event.data.ptr = tag;
        dynamic_assert(epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) == 0);
    };
    add_fd(window.get_fd(), &wayland_tag);
    add_fd(timer.as_handle(), &timer_tag);

    // main loop
    auto events = std::array<epoll_event, 16>();
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
            const auto tag = events[i].data.ptr;
            if(tag == &wayland_tag) {
                if(!window.dispatch()) {
                    window.running = false;
                }
            } else if(tag == &timer_tag) {
                auto expirations = uint64_t();
                read(timer.as_handle(), &expirations, sizeof(expirations));
                window.redraw();
            } else {
                // a module fd became readable
                if(std::bit_cast<Module*>(tag)->read()) {
                    window.redraw();
                }
            }
        }
    }

    pango_font_description_free(font);
    return 0;
}
