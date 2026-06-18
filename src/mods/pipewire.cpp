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

    std::string prefix       = "VOL";  // shown before the volume percentage
    std::string prefix_muted = "MUTE"; // shown alone when the sink is muted

    auto init(const int epfd, const json::Object& config) -> bool override {
        prefix       = config_string(config, "prefix", prefix);
        prefix_muted = config_string(config, "prefix_muted", prefix_muted);

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
        const auto text = vc.muted() ? prefix_muted : apply_prefix(prefix, std::format("{}%", int(std::lround(vc.display_volume() * 100))));
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
