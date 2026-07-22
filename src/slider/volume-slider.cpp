#include "../macros/assert.hpp"
#include "../volume.hpp"
#include "app.hpp"
#include "model.hpp"

namespace {
struct VolumeModel : SliderModel {
    VolumeControl vc;

    auto init() -> bool {
        return vc.init();
    }

    auto fd() -> int override {
        return vc.fd();
    }

    auto dispatch() -> bool override {
        return vc.dispatch();
    }

    auto available() const -> bool override {
        return vc.available();
    }

    auto fraction() const -> double override {
        return vc.display_volume();
    }

    auto set_fraction(const double value) -> void override {
        vc.set_perceptual(value);
    }

    auto add_fraction(const double delta) -> void override {
        vc.add_perceptual(delta);
    }

    auto has_button() const -> bool override {
        return true;
    }

    auto button_active() const -> bool override {
        return vc.muted();
    }

    auto button_label() const -> const char* override {
        return "MUTE";
    }

    auto button_press() -> void override {
        vc.toggle_mute();
    }

    auto command(const std::string_view cmd) -> void override {
        if(cmd == "mute") {
            vc.set_mute(true);
        } else if(cmd == "unmute") {
            vc.set_mute(false);
        } else if(cmd == "toggle") {
            vc.toggle_mute();
        }
    }
};
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    const auto cmd  = argc > 1 ? argv[1] : "show";
    const auto path = slider::socket_path("swaybarpp-volume");
    if(slider::forward(path.data(), cmd)) {
        return 0;
    }

    auto model = VolumeModel();
    ensure(model.init());
    return slider::run_server(path.data(), model, cmd);
}
