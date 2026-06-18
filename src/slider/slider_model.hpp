#pragma once
#include <string_view>

struct SliderModel {
    // backend fd to poll for external changes.
    virtual auto fd() -> int {
        return -1;
    }

    // process pending backend events.
    virtual auto dispatch() -> bool {
        return false;
    }

    // whether the device/data is present.
    virtual auto available() const -> bool = 0;

    // current value, 0..1
    virtual auto fraction() const -> double = 0;

    // set the value, 0..1
    virtual auto set_fraction(double value) -> void = 0;

    // adjust the value by delta, clamped to 0..1.
    virtual auto add_fraction(double delta) -> void {
        set_fraction(fraction() + delta);
    }

    // optional bottom button (e.g. mute).
    virtual auto has_button() const -> bool {
        return false;
    }

    virtual auto button_active() const -> bool {
        return false;
    }

    virtual auto button_label() const -> const char* {
        return "";
    }

    virtual auto button_press() -> void {
    }

    // apply a named command (verbs beyond "+N"/"-N", e.g. "mute"/"unmute"/"toggle").
    virtual auto command(std::string_view /*cmd*/) -> void {
    }

    virtual ~SliderModel() {
    }
};
