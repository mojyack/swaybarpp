#include <array>
#include <bit>
#include <numbers>

#include <linux/input-event-codes.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "../macros/assert.hpp"
#include "window.hpp"

namespace {
namespace theme {
constexpr auto button_r  = 38.0; // numpad button radius
constexpr auto pitch     = 96.0; // distance between button centers
constexpr auto grid_off  = 40.0; // grid center offset from screen center
constexpr auto dots_gap  = 78.0; // distance from top button row to the dots
constexpr auto dot_r     = 6.0;
constexpr auto dot_pitch = 28.0;

constexpr auto button_alpha = 0.12;
constexpr auto flash_alpha  = 0.35; // extra fill added to a freshly-pressed button
constexpr auto flash_decay  = 0.12; // fade-out per 16ms animation tick
constexpr auto error_color  = Color{0xbf / 255.0, 0x61 / 255.0, 0x6a / 255.0};
} // namespace theme

// numpad cells: 0..8 = digits 1..9, 9 = empty, 10 = digit 0, 11 = backspace
constexpr auto backspace_cell = 11;

auto cell_digit(const int index) -> char {
    if(index >= 0 && index < 9) {
        return char('1' + index);
    }
    if(index == 10) {
        return '0';
    }
    return 0;
}

auto digit_cell(const char digit) -> int {
    if(digit == '0') {
        return 10;
    }
    return digit - '1';
}

auto key_digit(const uint32_t key) -> char {
    switch(key) {
    case KEY_1:
    case KEY_KP1:
        return '1';
    case KEY_2:
    case KEY_KP2:
        return '2';
    case KEY_3:
    case KEY_KP3:
        return '3';
    case KEY_4:
    case KEY_KP4:
        return '4';
    case KEY_5:
    case KEY_KP5:
        return '5';
    case KEY_6:
    case KEY_KP6:
        return '6';
    case KEY_7:
    case KEY_KP7:
        return '7';
    case KEY_8:
    case KEY_KP8:
        return '8';
    case KEY_9:
    case KEY_KP9:
        return '9';
    case KEY_0:
    case KEY_KP0:
        return '0';
    default:
        return 0;
    }
}
} // namespace

// lock surface

LockSurface::LockSurface(Window& app, wl_output* const output)
    : app(app),
      output(output) {
    surface = app.compositor->create_surface();
    surface.init(this);

    lock_surface = app.lock.create_lock_surface(surface.native(), output);
    lock_surface.init(this);
    // no commit here: a lock surface must not be committed with a buffer before the first configure
}

auto LockSurface::on_wl_surface_preferred_buffer_scale(const int32_t factor) -> void {
    if(factor <= 0 || factor == scale) {
        return;
    }
    scale = factor;
    redraw();
}

auto LockSurface::on_ext_session_lock_surface_configure(const uint32_t width, const uint32_t height) -> void {
    logical_width  = width;
    logical_height = height;
    configured     = true;
    redraw();
}

auto LockSurface::cell_center(const int index) const -> std::pair<double, double> {
    const auto col = index % 3;
    const auto row = index / 3;
    const auto cx  = logical_width / 2.0 + (col - 1) * theme::pitch;
    const auto cy  = logical_height / 2.0 + theme::grid_off + (row - 1.5) * theme::pitch;
    return {cx, cy};
}

auto LockSurface::hit_test(const double x, const double y) const -> int {
    for(auto i = 0; i < 12; i += 1) {
        if(i == 9) {
            continue;
        }
        const auto [cx, cy] = cell_center(i);
        const auto dx       = x - cx;
        const auto dy       = y - cy;
        if(dx * dx + dy * dy <= theme::button_r * theme::button_r) {
            return i;
        }
    }
    return -1;
}

auto LockSurface::acquire_buffer() -> Buffer* {
    const auto pixel_width  = logical_width * scale;
    const auto pixel_height = logical_height * scale;
    if(pixel_width == 0 || pixel_height == 0) {
        return nullptr;
    }
    // reuse a free buffer that already has the right size
    for(const auto& buffer : buffers) {
        if(!buffer->busy && buffer->width == pixel_width && buffer->height == pixel_height) {
            return buffer.get();
        }
    }
    // otherwise repurpose a free slot, recreating it at the new size
    for(auto& buffer : buffers) {
        if(!buffer->busy) {
            buffer = std::make_unique<Buffer>(app.shm, pixel_width, pixel_height);
            return buffer.get();
        }
    }
    // every buffer is still held by the compositor; add another
    buffers.push_back(std::make_unique<Buffer>(app.shm, pixel_width, pixel_height));
    return buffers.back().get();
}

auto LockSurface::redraw() -> void {
    if(!configured || closed) {
        return;
    }

    const auto buffer = acquire_buffer();
    if(buffer == nullptr) {
        return;
    }

    const auto cairo = cairo_create(buffer->cairo_surface);

    // background
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo, app.background.r, app.background.g, app.background.b, 1.0);
    cairo_paint(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
    cairo_scale(cairo, scale, scale);

    // pin dots
    const auto n      = int(app.pin_len);
    const auto dots_y = logical_height / 2.0 + theme::grid_off - 1.5 * theme::pitch - theme::dots_gap;
    const auto dots_x = logical_width / 2.0 - (n - 1) * theme::dot_pitch / 2.0;
    const auto dot_fg = app.error ? theme::error_color : app.foreground;
    for(auto i = 0; i < n; i += 1) {
        cairo_arc(cairo, dots_x + i * theme::dot_pitch, dots_y, theme::dot_r, 0, 2 * std::numbers::pi);
        set_color(cairo, dot_fg);
        if(i < int(app.entered.size()) || app.error) {
            cairo_fill(cairo);
        } else {
            cairo_set_line_width(cairo, 1.5);
            cairo_stroke(cairo);
        }
    }

    // numpad
    for(auto i = 0; i < 12; i += 1) {
        if(i == 9) {
            continue;
        }
        const auto [cx, cy] = cell_center(i);
        const auto flash    = i == app.flash_cell ? app.flash_phase : 0.0;
        if(i == backspace_cell) {
            if(flash > 0.0) {
                cairo_arc(cairo, cx, cy, theme::button_r, 0, 2 * std::numbers::pi);
                cairo_set_source_rgba(cairo, app.foreground.r, app.foreground.g, app.foreground.b, flash * theme::flash_alpha);
                cairo_fill(cairo);
            }
            if(!app.entered.empty()) {
                draw_text(cairo, app.font, "⌫", cx, cy, app.foreground);
            }
            continue;
        }
        cairo_arc(cairo, cx, cy, theme::button_r, 0, 2 * std::numbers::pi);
        cairo_set_source_rgba(cairo, app.foreground.r, app.foreground.g, app.foreground.b, theme::button_alpha + flash * theme::flash_alpha);
        cairo_fill(cairo);
        const auto label = std::array{cell_digit(i), '\0'};
        draw_text(cairo, app.digit_font, label.data(), cx, cy, app.foreground);
    }

    cairo_destroy(cairo);
    cairo_surface_flush(buffer->cairo_surface);

    surface.attach(buffer->buffer.native(), 0, 0);
    surface.set_buffer_scale(scale);
    surface.damage(0, 0, buffer->width, buffer->height);
    surface.commit();
    buffer->busy = true;
}

// lock window

auto Window::add_surface(wl_output* const output) -> void {
    surfaces.push_back(std::make_unique<LockSurface>(*this, output));
}

auto Window::surface_for(wl_surface* const surface) -> LockSurface* {
    for(const auto& s : surfaces) {
        if(s->surface.native() == surface) {
            return s.get();
        }
    }
    return nullptr;
}

auto Window::prune() -> void {
    if(focused != nullptr && focused->closed) {
        focused = nullptr;
    }
    std::erase_if(surfaces, [](const auto& s) { return s->closed; });
}

auto Window::arm_anim_timer(const bool on) -> void {
    const auto tick = on ? 16'000'000L : 0L; // ~60fps
    const auto its  = itimerspec{
         .it_interval = {.tv_sec = 0, .tv_nsec = tick},
         .it_value    = {.tv_sec = 0, .tv_nsec = tick},
    };
    timerfd_settime(anim_timer.as_handle(), 0, &its, nullptr);
}

auto Window::start_flash(const int index) -> void {
    flash_cell  = index;
    flash_phase = 1.0;
    arm_anim_timer(true);
    // the caller's press_* redraws with the fresh highlight
}

auto Window::press_digit(const char digit) -> void {
    error = false;
    if(entered.size() < pin_len) {
        entered.push_back(digit);
    }
    if(entered.size() >= pin_len) {
        if(on_submit && on_submit(entered)) {
            unlock();
            return;
        }
        entered.clear();
        error = true;
    }
    if(on_activity) {
        on_activity();
    }
    redraw();
}

auto Window::press_backspace() -> void {
    error = false;
    if(!entered.empty()) {
        entered.pop_back();
    }
    if(on_activity) {
        on_activity();
    }
    redraw();
}

auto Window::press_cell(const int index) -> void {
    if(const auto digit = cell_digit(index)) {
        start_flash(index);
        press_digit(digit);
    } else if(index == backspace_cell) {
        if(!entered.empty()) {
            start_flash(index);
        }
        press_backspace();
    } else if(on_activity) {
        on_activity();
    }
}

auto Window::handle_press() -> void {
    if(focused == nullptr) {
        return;
    }
    press_cell(focused->hit_test(pointer_x, pointer_y));
}

auto Window::unlock() -> void {
    lock.unlock_and_destroy();
    running = false;
}

auto Window::on_wl_output_created(wl_output* const output) -> void {
    outputs.push_back(output);
    if(initialized) {
        add_surface(output);
    }
}

auto Window::on_wl_output_removed(wl_output* const output) -> void {
    std::erase(outputs, output);
    for(const auto& s : surfaces) {
        if(s->output == output) {
            s->closed = true; // pruned after dispatch
        }
    }
}

auto Window::on_wl_keyboard_keymap(const uint32_t /*format*/, const int32_t fd, const uint32_t /*size*/) -> void {
    close(fd);
}

auto Window::on_wl_keyboard_key(const uint32_t key, const uint32_t state) -> void {
    if(state != WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
    }
    if(const auto digit = key_digit(key)) {
        start_flash(digit_cell(digit));
        press_digit(digit);
    } else if(key == KEY_BACKSPACE) {
        if(!entered.empty()) {
            start_flash(backspace_cell);
        }
        press_backspace();
    } else if(key == KEY_ESC) {
        error = false;
        entered.clear();
        if(on_activity) {
            on_activity();
        }
        redraw();
    }
}

auto Window::on_wl_pointer_enter(wl_surface* const surface, const double x, const double y) -> void {
    focused   = surface_for(surface);
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_pointer_leave(wl_surface* const surface) -> void {
    if(focused != nullptr && focused->surface.native() == surface) {
        focused = nullptr;
    }
}

auto Window::on_wl_pointer_motion(const double x, const double y) -> void {
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_pointer_button(const uint32_t button, const uint32_t state) -> void {
    if(button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }
    handle_press();
}

auto Window::on_wl_touch_down(wl_surface* const surface, const uint32_t id, const double x, const double y) -> void {
    if(touch_id != uint32_t(-1)) {
        return; // ignore extra simultaneous fingers
    }
    touch_id  = id;
    focused   = surface_for(surface);
    pointer_x = x;
    pointer_y = y;
    handle_press();
}

auto Window::on_wl_touch_motion(const uint32_t id, const double x, const double y) -> void {
    if(id != touch_id) {
        return;
    }
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_touch_up(const uint32_t id) -> void {
    if(id != touch_id) {
        return;
    }
    touch_id = -1;
}

auto Window::on_ext_session_lock_locked() -> void {
    locked = true;
}

auto Window::on_ext_session_lock_finished() -> void {
    // the lock was denied (another locker active) or revoked by the compositor
    failed  = true;
    running = false;
}

auto Window::get_fd() -> int {
    return display.get_fd();
}

auto Window::get_anim_fd() -> int {
    return anim_timer.as_handle();
}

auto Window::on_anim_tick() -> void {
    const auto expirations = anim_timer.read<uint64_t>().value_or(1);
    flash_phase -= theme::flash_decay * double(expirations);
    if(flash_phase <= 0.0) {
        flash_phase = 0.0;
        flash_cell  = -1;
        arm_anim_timer(false);
    }
    redraw();
}

auto Window::flush() -> void {
    display.flush();
}

auto Window::dispatch() -> bool {
    const auto ok = display.dispatch();
    prune();
    return ok;
}

auto Window::roundtrip() -> void {
    display.roundtrip();
}

auto Window::redraw() -> void {
    for(const auto& s : surfaces) {
        s->redraw();
    }
    display.flush();
}

Window::Window(const Color background, const Color foreground, PangoFontDescription* const font, const size_t pin_len)
    : registry(display.get_registry()),
      background(background),
      foreground(foreground),
      font(font),
      pin_len(pin_len) {
    registry.set_binders({&compositor_binder, &shm_binder, &seat_binder, &output_binder, &lock_manager_binder});
    display.roundtrip();
    ASSERT(!compositor_binder.interfaces.empty(), "compositor not available");
    ASSERT(!shm_binder.interfaces.empty(), "shm not available");
    ASSERT(!lock_manager_binder.interfaces.empty(), "ext-session-lock-v1 not available");
    compositor   = std::bit_cast<towl::Compositor*>(compositor_binder.interfaces[0].get());
    shm          = std::bit_cast<towl::Shm*>(shm_binder.interfaces[0].get());
    lock_manager = std::bit_cast<towl::SessionLockManager*>(lock_manager_binder.interfaces[0].get());

    digit_font = pango_font_description_copy(font);
    pango_font_description_set_size(digit_font, 26 * PANGO_SCALE);

    anim_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    lock = lock_manager->lock();
    lock.init(this);

    initialized = true;
    for(const auto output : outputs) {
        add_surface(output);
    }

    display.roundtrip();
}

Window::~Window() {
    pango_font_description_free(digit_font);
}
