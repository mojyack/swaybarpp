#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../cairo_util.hpp"
#include "../shm_buffer.hpp"
#include "../towl/towl.hpp"

class LockWindow;

// one session-lock surface, bound to a single output
struct LockSurface : towl::SurfaceCallbacks, towl::SessionLockSurfaceCallbacks {
    LockWindow&              app;
    wl_output*               output;
    towl::Surface            surface;
    towl::SessionLockSurface lock_surface;

    std::vector<std::unique_ptr<Buffer>> buffers;

    size_t logical_width  = 0;
    size_t logical_height = 0;
    int    scale          = 1;
    bool   configured     = false;
    bool   closed         = false;

    auto on_wl_surface_preferred_buffer_scale(int32_t factor) -> void override;
    auto on_ext_session_lock_surface_configure(uint32_t width, uint32_t height) -> void override;

    // numpad cell index = row * 3 + col, -1 = no hit
    auto cell_center(int index) const -> std::pair<double, double>;
    auto hit_test(double x, double y) const -> int;

    auto acquire_buffer() -> Buffer*;
    auto redraw() -> void;

    LockSurface(LockWindow& app, wl_output* output);
};

// manages one LockSurface per output and the shared wayland state
class LockWindow : towl::OutputCallbacks,
                   towl::KeyboardCallbacks,
                   towl::PointerCallbacks,
                   towl::TouchCallbacks,
                   towl::SessionLockCallbacks {
    friend struct LockSurface;

  private:
    towl::Display                  display;
    towl::Registry                 registry;
    towl::CompositorBinder         compositor_binder   = {6};
    towl::ShmBinder                shm_binder          = {1};
    towl::SeatBinder               seat_binder         = {5, this, this, this};
    towl::OutputBinder             output_binder       = {4, this};
    towl::SessionLockManagerBinder lock_manager_binder = {1};

    towl::Compositor*         compositor   = nullptr;
    towl::Shm*                shm          = nullptr;
    towl::SessionLockManager* lock_manager = nullptr;
    towl::SessionLock         lock;

    Color                 background;
    Color                 foreground;
    PangoFontDescription* font;
    PangoFontDescription* digit_font = nullptr;
    size_t                pin_len;

    std::vector<wl_output*>                   outputs; // live outputs
    std::vector<std::unique_ptr<LockSurface>> surfaces;
    bool                                      initialized = false;
    bool                                      locked      = false;
    LockSurface*                              focused     = nullptr;
    double                                    pointer_x   = 0;
    double                                    pointer_y   = 0;
    uint32_t                                  touch_id    = -1;

    std::string entered;
    bool        error = false;

    auto add_surface(wl_output* output) -> void;
    auto surface_for(wl_surface* surface) -> LockSurface*;
    auto prune() -> void;

    auto press_digit(char digit) -> void;
    auto press_backspace() -> void;
    auto press_cell(int index) -> void;
    auto handle_press() -> void;
    auto unlock() -> void;

    auto on_wl_output_created(wl_output* output) -> void override;
    auto on_wl_output_removed(wl_output* output) -> void override;
    auto on_wl_keyboard_keymap(uint32_t format, int32_t fd, uint32_t size) -> void override;
    auto on_wl_keyboard_key(uint32_t key, uint32_t state) -> void override;
    auto on_wl_pointer_enter(wl_surface* surface, double x, double y) -> void override;
    auto on_wl_pointer_leave(wl_surface* surface) -> void override;
    auto on_wl_pointer_motion(double x, double y) -> void override;
    auto on_wl_pointer_button(uint32_t button, uint32_t state) -> void override;
    auto on_wl_touch_down(wl_surface* surface, uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_motion(uint32_t id, double x, double y) -> void override;
    auto on_wl_touch_up(uint32_t id) -> void override;
    auto on_ext_session_lock_locked() -> void override;
    auto on_ext_session_lock_finished() -> void override;

  public:
    bool running = true;
    bool failed  = false; // lock was denied or lost

    std::function<bool(std::string_view)> on_submit; // return true to unlock
    std::function<void()>                 on_activity;

    auto get_fd() -> int;
    auto flush() -> void;
    auto dispatch() -> bool;
    auto roundtrip() -> void;
    auto redraw() -> void;

    LockWindow(Color background, Color foreground, PangoFontDescription* font, size_t pin_len);
    ~LockWindow();
};
