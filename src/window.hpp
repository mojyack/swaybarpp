#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <cairo/cairo.h>

#include "mod.hpp"
#include "shm_buffer.hpp"
#include "towl/towl.hpp"

class Window;

// one wlr-layer-shell panel, bound to a single output
struct Bar : towl::SurfaceCallbacks, towl::LayerSurfaceCallbacks {
    Window&            app;
    wl_output*         output;
    towl::Surface      surface;
    towl::LayerSurface layer_surface;

    std::vector<std::unique_ptr<Buffer>> buffers;

    size_t logical_width  = 0;
    size_t logical_height = 0;
    int    scale          = 1;
    bool   closed         = false;
    bool   frame_pending  = false;
    bool   dirty          = false;

    auto on_wl_surface_preferred_buffer_scale(int32_t factor) -> void override;
    auto on_wl_surface_frame() -> void override;
    auto on_zwlr_layer_surface_configure(uint32_t width, uint32_t height) -> void override;
    auto on_zwlr_layer_surface_closed() -> void override;

    auto acquire_buffer() -> Buffer*;
    auto redraw(bool force = false) -> void;

    Bar(Window& app, wl_output* output);
};

// manages one Bar per output and the shared wayland state
class Window : towl::OutputCallbacks, towl::PointerCallbacks, towl::TouchCallbacks {
    friend struct Bar;

  private:
    towl::Display          display;
    towl::Registry         registry;
    towl::CompositorBinder compositor_binder  = {6};
    towl::LayerShellBinder layer_shell_binder = {4};
    towl::ShmBinder        shm_binder         = {1};
    towl::SeatBinder       seat_binder        = {5, nullptr, this, this};
    towl::OutputBinder     output_binder      = {4, this};

    towl::Compositor* compositor  = nullptr;
    towl::LayerShell* layer_shell = nullptr;
    towl::Shm*        shm         = nullptr;

    size_t      height;
    bool        anchor_bottom;
    const char* title;

    std::vector<wl_output*>                     outputs; // live outputs
    std::unordered_map<wl_output*, std::string> output_names;
    std::vector<std::unique_ptr<Bar>>           bars;
    bool                                        initialized = false;
    Bar*                                        focused     = nullptr;
    double                                      pointer_x   = 0;
    double                                      pointer_y   = 0;
    uint32_t                                    touch_id    = -1;

    auto add_bar(wl_output* output) -> void;
    auto bar_for_surface(wl_surface* surface) -> Bar*;
    auto output_name(wl_output* output) -> std::string;
    auto prune() -> void;

    auto on_wl_output_created(wl_output* output) -> void override;
    auto on_wl_output_removed(wl_output* output) -> void override;
    auto on_wl_output_name(wl_output* output, const char* name) -> void override;
    auto on_wl_pointer_enter(wl_surface* surface, double x, double y) -> void override;
    auto on_wl_pointer_leave(wl_surface* surface) -> void override;
    auto on_wl_pointer_motion(double x, double y) -> void override;
    auto on_wl_pointer_button(uint32_t button, uint32_t state) -> void override;
    auto on_wl_pointer_axis(uint32_t axis, double value) -> void override;
    auto on_wl_touch_down(wl_surface* surface, uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_motion(uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_up(uint32_t id) -> void override;

  public:
    bool                                                      running    = true;
    Color                                                     background = {};
    std::function<void(cairo_t*, int, int, std::string_view)> on_draw;
    std::function<void(double, double, uint32_t)>             on_click;
    std::function<void(double, double, double, double)>       on_scroll;

    auto get_fd() -> int;
    auto flush() -> void;
    auto dispatch() -> bool;
    auto redraw() -> void;

    Window(size_t height, bool anchor_bottom, const char* title);
};
