#pragma once
#include <functional>
#include <memory>
#include <vector>

#include "../shm_buffer.hpp"
#include "../towl/towl.hpp"
#include "slider_model.hpp"

class SliderWindow : towl::SurfaceCallbacks,
                     towl::LayerSurfaceCallbacks,
                     towl::PointerCallbacks,
                     towl::TouchCallbacks {
  private:
    towl::Display          display;
    towl::Registry         registry;
    towl::CompositorBinder compositor_binder  = {6};
    towl::LayerShellBinder layer_shell_binder = {4};
    towl::ShmBinder        shm_binder         = {1};
    towl::SeatBinder       seat_binder        = {5, nullptr, this, this};

    towl::Compositor* compositor  = nullptr;
    towl::LayerShell* layer_shell = nullptr;
    towl::Shm*        shm         = nullptr;

    towl::Surface      surface;
    towl::LayerSurface layer_surface;

    std::vector<std::unique_ptr<Buffer>> buffers;

    SliderModel&          model;
    std::function<void()> on_activity;

    size_t   logical_width  = 0;
    size_t   logical_height = 0;
    int      scale          = 1;
    uint32_t touch_id       = -1;
    double   pointer_x      = 0;
    double   pointer_y      = 0;
    bool     has_button     = false;
    bool     configured     = false;
    bool     dragging       = false;

    auto acquire_buffer() -> Buffer*;
    auto draw(cairo_t* cairo) -> void;

    auto track_top() const -> double;
    auto track_bottom() const -> double;
    auto on_button() const -> bool;
    auto in_track() const -> bool;

    auto handle_press() -> void;
    auto handle_motion() -> void;
    auto handle_release() -> void;

    auto on_wl_surface_preferred_buffer_scale(int32_t factor) -> void override;
    auto on_zwlr_layer_surface_configure(uint32_t width, uint32_t height) -> void override;
    auto on_zwlr_layer_surface_closed() -> void override;
    auto on_wl_pointer_enter(wl_surface* surface, double x, double y) -> void override;
    auto on_wl_pointer_motion(double x, double y) -> void override;
    auto on_wl_pointer_button(uint32_t button, uint32_t state) -> void override;
    auto on_wl_pointer_axis(uint32_t axis, double value) -> void override;
    auto on_wl_touch_down(wl_surface* surface, uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_motion(uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_up(uint32_t id) -> void override;

  public:
    bool running = true;

    auto get_fd() -> int;
    auto flush() -> void;
    auto dispatch() -> bool;
    auto redraw() -> void;

    SliderWindow(SliderModel& model, std::function<void()> on_activity);
};
