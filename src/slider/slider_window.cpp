#include <algorithm>
#include <bit>
#include <cmath>
#include <format>

#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>

#include "../cairo_util.hpp"
#include "../macros/assert.hpp"
#include "slider_window.hpp"

namespace {
namespace theme {
constexpr auto width         = 90.0;
constexpr auto height_button = 320.0; // with a bottom button
constexpr auto height_plain  = 260.0; // slider only
constexpr auto pad           = 16.0;
constexpr auto label_h       = 26.0; // top percentage label area
constexpr auto button_h      = 44.0; // bottom button height
constexpr auto track_w       = 16.0; // slider track width
constexpr auto gap           = 12.0; // gap between track and button
constexpr auto step          = 0.02; // scroll step

constexpr auto panel_color     = Color{0x1d / 255.0, 0x1f / 255.0, 0x21 / 255.0, 0.95};
constexpr auto track_color     = Color{0x3a / 255.0, 0x3d / 255.0, 0x41 / 255.0};
constexpr auto fill_color      = Color{0x81 / 255.0, 0xa2 / 255.0, 0xbe / 255.0};
constexpr auto fill_off_color  = Color{0x55 / 255.0, 0x57 / 255.0, 0x59 / 255.0};
constexpr auto text_color      = Color{0xc5 / 255.0, 0xc8 / 255.0, 0xc6 / 255.0};
constexpr auto text_dim_color  = Color{0x70 / 255.0, 0x72 / 255.0, 0x74 / 255.0};
constexpr auto button_color    = Color{0x28 / 255.0, 0x2a / 255.0, 0x2e / 255.0};
constexpr auto button_on_color = Color{0xcc / 255.0, 0x66 / 255.0, 0x66 / 255.0};
}; // namespace theme
} // namespace

auto SliderWindow::track_top() const -> double {
    return theme::pad + theme::label_h;
}

auto SliderWindow::track_bottom() const -> double {
    return has_button ? logical_height - theme::pad - theme::button_h - theme::gap : logical_height - theme::pad;
}

auto SliderWindow::on_button() const -> bool {
    if(!has_button) {
        return false;
    }
    const auto x0 = theme::pad;
    const auto x1 = logical_width - theme::pad;
    const auto y0 = logical_height - theme::pad - theme::button_h;
    const auto y1 = logical_height - theme::pad;
    return pointer_x >= x0 && pointer_x <= x1 && pointer_y >= y0 && pointer_y <= y1;
}

auto SliderWindow::in_track() const -> bool {
    return pointer_x >= 0 && pointer_x <= double(logical_width) && pointer_y >= track_top() - 8 && pointer_y <= track_bottom() + 8;
}

auto SliderWindow::acquire_buffer() -> Buffer* {
    const auto pixel_width  = size_t(logical_width * scale);
    const auto pixel_height = size_t(logical_height * scale);
    if(pixel_width == 0 || pixel_height == 0) {
        return nullptr;
    }
    for(const auto& buffer : buffers) {
        if(!buffer->busy && buffer->width == pixel_width && buffer->height == pixel_height) {
            return buffer.get();
        }
    }
    for(auto& buffer : buffers) {
        if(!buffer->busy) {
            buffer = std::make_unique<Buffer>(shm, pixel_width, pixel_height);
            return buffer.get();
        }
    }
    buffers.push_back(std::make_unique<Buffer>(shm, pixel_width, pixel_height));
    return buffers.back().get();
}

auto SliderWindow::draw(cairo_t* const cairo) -> void {
    const auto font = pango_font_description_from_string("sans 11");

    const auto W      = double(logical_width);
    const auto H      = double(logical_height);
    const auto active = model.button_active();
    const auto value  = std::clamp(model.fraction(), 0.0, 1.0);

    // panel
    rounded_rect(cairo, 0.5, 0.5, W - 1, H - 1, 14);
    set_color(cairo, theme::panel_color);
    cairo_fill(cairo);

    // percentage label
    const auto label = std::format("{}%", int(std::lround(value * 100)));
    draw_text(cairo, font, label, W / 2, theme::pad + theme::label_h / 2, active ? theme::text_dim_color : theme::text_color);

    // track
    const auto tx = (W - theme::track_w) / 2;
    const auto ty = track_top();
    const auto th = track_bottom() - track_top();
    rounded_rect(cairo, tx, ty, theme::track_w, th, theme::track_w / 2);
    set_color(cairo, theme::track_color);
    cairo_fill_preserve(cairo);
    // lower portion
    cairo_clip(cairo);
    const auto fill_h = value * th;
    cairo_rectangle(cairo, tx, ty + th - fill_h, theme::track_w, fill_h);
    set_color(cairo, active ? theme::fill_off_color : theme::fill_color);
    cairo_fill(cairo);
    cairo_reset_clip(cairo);

    // optional bottom button
    if(has_button) {
        const auto bx = theme::pad;
        const auto by = H - theme::pad - theme::button_h;
        const auto bw = W - 2 * theme::pad;
        rounded_rect(cairo, bx, by, bw, theme::button_h, 8);
        set_color(cairo, active ? theme::button_on_color : theme::button_color);
        cairo_fill(cairo);
        draw_text(cairo, font, model.button_label(), W / 2, by + theme::button_h / 2, active ? Color{1, 1, 1} : theme::text_color);
    }

    pango_font_description_free(font);
}

auto SliderWindow::redraw() -> void {
    if(!configured) {
        return;
    }

    const auto buffer = acquire_buffer();
    if(buffer == nullptr) {
        return;
    }

    const auto cairo = cairo_create(buffer->cairo_surface);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo, 0, 0, 0, 0);
    cairo_paint(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
    cairo_scale(cairo, scale, scale);
    draw(cairo);
    cairo_destroy(cairo);
    cairo_surface_flush(buffer->cairo_surface);

    surface.attach(buffer->buffer.native(), 0, 0);
    surface.set_buffer_scale(scale);
    surface.damage(0, 0, buffer->width, buffer->height);
    surface.commit();
    buffer->busy = true;
}

auto SliderWindow::handle_press() -> void {
    if(on_button()) {
        model.button_press();
    } else if(in_track()) {
        dragging        = true;
        const auto y    = std::clamp(pointer_y, track_top(), track_bottom());
        const auto frac = (track_bottom() - y) / (track_bottom() - track_top());
        model.set_fraction(frac);
    }
    on_activity();
    redraw();
}

auto SliderWindow::handle_motion() -> void {
    if(!dragging) {
        return;
    }

    const auto y    = std::clamp(pointer_y, track_top(), track_bottom());
    const auto frac = (track_bottom() - y) / (track_bottom() - track_top());
    model.set_fraction(frac);
    on_activity();
    redraw();
}

auto SliderWindow::handle_release() -> void {
    dragging = false;
}

auto SliderWindow::on_wl_surface_preferred_buffer_scale(const int32_t factor) -> void {
    if(factor <= 0 || factor == scale) {
        return;
    }
    scale = factor;
    redraw();
}

auto SliderWindow::on_zwlr_layer_surface_configure(const uint32_t width, const uint32_t height) -> void {
    if(width > 0) {
        logical_width = width;
    }
    if(height > 0) {
        logical_height = height;
    }
    configured = true;
    redraw();
}

auto SliderWindow::on_zwlr_layer_surface_closed() -> void {
    running = false;
}

auto SliderWindow::on_wl_pointer_enter(wl_surface* const /*surface*/, const double x, const double y) -> void {
    pointer_x = x;
    pointer_y = y;
}

auto SliderWindow::on_wl_pointer_motion(const double x, const double y) -> void {
    pointer_x = x;
    pointer_y = y;
    handle_motion();
}

auto SliderWindow::on_wl_pointer_button(const uint32_t button, const uint32_t state) -> void {
    if(button != BTN_LEFT) {
        return;
    }
    if(state == WL_POINTER_BUTTON_STATE_PRESSED) {
        handle_press();
    } else {
        handle_release();
    }
}

auto SliderWindow::on_wl_pointer_axis(const uint32_t axis, const double value) -> void {
    if(axis != WL_POINTER_AXIS_VERTICAL_SCROLL || value == 0 || !model.available()) {
        return;
    }
    model.add_fraction(value < 0 ? theme::step : -theme::step);
    on_activity();
    redraw();
}

auto SliderWindow::on_wl_touch_down(wl_surface* const /*surface*/, const uint32_t id, const double x, const double y) -> void {
    touch_id  = id;
    pointer_x = x;
    pointer_y = y;
    handle_press();
}

auto SliderWindow::on_wl_touch_motion(const uint32_t id, const double x, const double y) -> void {
    if(id != touch_id) {
        return;
    }

    pointer_x = x;
    pointer_y = y;
    handle_motion();
}

auto SliderWindow::on_wl_touch_up(const uint32_t id) -> void {
    if(id != touch_id) {
        return;
    }

    touch_id = -1;
    handle_release();
}

auto SliderWindow::get_fd() -> int {
    return display.get_fd();
}

auto SliderWindow::flush() -> void {
    display.flush();
}

auto SliderWindow::dispatch() -> bool {
    return display.dispatch();
}

SliderWindow::SliderWindow(SliderModel& model, std::function<void()> on_activity)
    : registry(display.get_registry()),
      model(model),
      on_activity(std::move(on_activity)),
      has_button(model.has_button()) {
    registry.set_binders({&compositor_binder, &layer_shell_binder, &shm_binder, &seat_binder});
    display.roundtrip();
    ASSERT(!compositor_binder.interfaces.empty(), "compositor not available");
    ASSERT(!layer_shell_binder.interfaces.empty(), "wlr-layer-shell not available");
    ASSERT(!shm_binder.interfaces.empty(), "shm not available");
    compositor  = std::bit_cast<towl::Compositor*>(compositor_binder.interfaces[0].get());
    layer_shell = std::bit_cast<towl::LayerShell*>(layer_shell_binder.interfaces[0].get());
    shm         = std::bit_cast<towl::Shm*>(shm_binder.interfaces[0].get());

    const auto height = has_button ? theme::height_button : theme::height_plain;
    logical_width     = size_t(theme::width);
    logical_height    = size_t(height);

    surface = compositor->create_surface();
    surface.init(this);

    layer_surface = layer_shell->create_layer_surface(surface.native(), nullptr, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "swaybarpp-slider");
    layer_surface.init(this);
    layer_surface.set_size(uint32_t(theme::width), uint32_t(height)); // no anchor -> compositor centers

    surface.commit();
    display.roundtrip();
}
