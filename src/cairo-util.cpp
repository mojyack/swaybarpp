#include <numbers>

#include "cairo-util.hpp"

auto set_color(cairo_t* const cairo, const Color color) -> void {
    cairo_set_source_rgba(cairo, color.r, color.g, color.b, color.a);
}

auto rounded_rect(cairo_t* const cairo, const double x, const double y, const double w, const double h, const double r) -> void {
    constexpr auto deg = std::numbers::pi / 180.0;
    cairo_new_sub_path(cairo);
    cairo_arc(cairo, x + w - r, y + r, r, -90 * deg, 0);
    cairo_arc(cairo, x + w - r, y + h - r, r, 0, 90 * deg);
    cairo_arc(cairo, x + r, y + h - r, r, 90 * deg, 180 * deg);
    cairo_arc(cairo, x + r, y + r, r, 180 * deg, 270 * deg);
    cairo_close_path(cairo);
}

auto draw_text(cairo_t* const cairo, PangoFontDescription* const font, const std::string_view text, const double cx, const double cy, const Color color) -> void {
    const auto layout = pango_cairo_create_layout(cairo);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.data(), text.size());
    auto tw = 0;
    auto th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    set_color(cairo, color);
    cairo_move_to(cairo, cx - tw / 2.0, cy - th / 2.0);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);
}
