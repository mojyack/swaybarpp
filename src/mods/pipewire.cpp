#include <cmath>
#include <format>
#include <string>

#include <sys/epoll.h>

#include <linux/input-event-codes.h>

#include "../macros/assert.hpp"
#include "../mod.hpp"
#include "../volume.hpp"

namespace {
constexpr auto step = 0.02;

struct Pipewire : Module {
    VolumeControl vc;

    std::string format       = "VOL {}%"; // {} is the volume percentage
    std::string format_muted = "MUTE";   // shown alone when the sink is muted

    auto init(const int epfd, const json::Object& config) -> bool override {
        format       = config_string(config, "format", format);
        format_muted = config_string(config, "format_muted", format_muted);

        ensure(vc.init());

        auto event     = epoll_event{.events = EPOLLIN};
        event.data.ptr = this;
        ensure(epoll_ctl(epfd, EPOLL_CTL_ADD, vc.fd(), &event) == 0);
        return true;
    }

    auto read() -> bool override {
        return vc.dispatch();
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        if(!vc.available()) {
            return;
        }
        const auto text = apply_format(vc.muted() ? format_muted : format, std::format("{}", int(std::lround(vc.display_volume() * 100))));
        draw_block(target, available, text);
    }

    auto click(const double /*x*/, const double /*y*/, const uint32_t button) -> bool override {
        if(button != BTN_LEFT) {
            return false;
        }
        vc.toggle_mute();
        return true;
    }

    auto scroll(const double /*x*/, const double /*y*/, const double /*dx*/, const double dy) -> bool override {
        if(!vc.available() || dy == 0) {
            return false;
        }
        vc.add_perceptual(dy > 0 ? step : -step);
        return true;
    }
};
} // namespace

REGISTER_MODULE("pipewire", Pipewire)
