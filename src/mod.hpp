#pragma once
#include <pango/pangocairo.h>

#include "json/json.hpp"

struct Color {
    double r;
    double g;
    double b;
    double a;

    static constexpr auto from_hex(const uint32_t hex) -> Color {
        return Color{
            .r = ((hex >> 24) & 0xff) / 255.0,
            .g = ((hex >> 16) & 0xff) / 255.0,
            .b = ((hex >> 8) & 0xff) / 255.0,
            .a = ((hex >> 0) & 0xff) / 255.0,
        };
    }
};

struct Rect {
    double x;
    double y;
    double w;
    double h;
};

enum class Align {
    Left,
    Right,
};

struct RenderTarget {
    cairo_t*              cairo;
    PangoFontDescription* font;
    Color                 foreground;
    Color                 background;
    Align                 align = Align::Right;
    std::string_view      output; // e.g. "DP-1"
};

auto draw_block(RenderTarget& target, Rect& available, std::string_view text) -> Rect;
auto config_string(const json::Object& config, std::string_view key, std::string_view fallback) -> std::string;
auto apply_prefix(const std::string_view prefix, const std::string_view value) -> std::string;
auto read_pseudo_file(const char* path) -> std::optional<std::string>;

struct Module {
    const char*              name;
    std::vector<std::string> on_click;

    // for hit-test, updated on draw
    double click_x0 = 0;
    double click_x1 = 0;

    // initialize module. register fd to given epoll fd if needed.
    // modules must set "epoll_event.data.ptr = this" for the main loop.
    // return false on failure.
    virtual auto init(int epfd, const json::Object& config) -> bool = 0;

    // called when one of the module's registered fds becomes readable.
    // return true if the screen should be redrawn.
    virtual auto read() -> bool {
        return false;
    }

    // draw to "target" within "available". update it for following modules.
    virtual auto draw(RenderTarget& target, Rect& available) -> void = 0;

    // called on pointer button press.
    // return true if the screen should be redrawn.
    virtual auto click(double /*x*/, double /*y*/, uint32_t /*button*/) -> bool {
        return false;
    }

    // called on pointer scroll.
    // dx/dy are wheel steps; dy > 0 means scrolling up.
    // return true if the screen should be redrawn.
    virtual auto scroll(double /*x*/, double /*y*/, double /*dx*/, double /*dy*/) -> bool {
        return false;
    }

    virtual ~Module() {}
};

auto create_module(std::string_view name) -> std::unique_ptr<Module>;

// for module implementions

using ModuleFactory = std::unique_ptr<Module> (*)();

// called via REGISTER_MODULE
auto register_module(std::string_view name, ModuleFactory factory) -> bool;

// place at file scope in a module's .cpp to register it. the factory also stamps
// Module::name, so modules no longer need a constructor just to set it.
#define REGISTER_MODULE(key, type)                                            \
    namespace {                                                               \
    [[maybe_unused]] const auto registered_##type = register_module(key, [] { \
        auto module  = std::unique_ptr<Module>(std::make_unique<type>());     \
        module->name = key;                                                   \
        return module;                                                        \
    });                                                                       \
    }
