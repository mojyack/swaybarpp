#pragma once
#include <cstdint>
#include <string_view>

#include <pango/pangocairo.h>

struct Color {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 1;

    static constexpr auto from_hex(const uint32_t hex) -> Color {
        return Color{
            .r = ((hex >> 24) & 0xff) / 255.0,
            .g = ((hex >> 16) & 0xff) / 255.0,
            .b = ((hex >> 8) & 0xff) / 255.0,
            .a = ((hex >> 0) & 0xff) / 255.0,
        };
    }
};

auto set_color(cairo_t* cairo, Color color) -> void;
auto rounded_rect(cairo_t* cairo, double x, double y, double w, double h, double r) -> void;
auto draw_text(cairo_t* cairo, PangoFontDescription* font, std::string_view text, double cx, double cy, Color color) -> void;
