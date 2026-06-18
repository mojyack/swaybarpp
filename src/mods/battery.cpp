#include "../macros/unwrap.hpp"
#include "../mod.hpp"
#include "../util/charconv.hpp"

namespace {
enum class Style {
    Simple,
    Graph,
    Count,
};

constexpr auto color_charging    = Color::from_hex(0xF2CC33FF);
constexpr auto color_discharging = Color::from_hex(0xFFFFFFFFF);
constexpr auto color_idle        = Color::from_hex(0x4DCC59FF);

auto state_color(const char sign) -> Color {
    switch(sign) {
    case '+':
        return color_charging;
    case '-':
        return color_discharging;
    default:
        return color_idle;
    }
}

auto draw_graph(RenderTarget& target, Rect& available, const uint32_t percent, const char sign, const double size) -> Rect {
    constexpr auto padding = 6.0;

    const auto body_h  = available.h * size;
    const auto body_w  = body_h * 1.9;
    const auto term_w  = body_h * 0.12;
    const auto term_h  = body_h * 0.45;
    const auto total_w = body_w + term_w + padding * 2;

    const auto block = Rect{
        .x = target.align == Align::Left ? available.x : available.x + available.w - total_w,
        .y = available.y,
        .w = total_w,
        .h = available.h,
    };
    if(target.align == Align::Left) {
        available.x += total_w;
    }
    available.w -= total_w;

    const auto cairo  = target.cairo;
    const auto lw     = std::max(1.5, body_h * 0.08);
    const auto color  = state_color(sign);
    const auto body_x = block.x + padding;
    const auto body_y = block.y + (block.h - body_h) / 2;

    // body outline
    rounded_rect(cairo, body_x, body_y, body_w, body_h, body_h * 0.22);
    cairo_set_source_rgba(cairo, target.foreground.r, target.foreground.g, target.foreground.b, target.foreground.a);
    cairo_set_line_width(cairo, lw);
    cairo_stroke(cairo);

    // positive terminal
    const auto term_y = body_y + (body_h - term_h) / 2;
    rounded_rect(cairo, body_x + body_w, term_y, term_w * 2, term_h, term_w * 0.6);
    cairo_fill(cairo);

    // charge fill
    const auto gap     = lw;
    const auto fill_x  = body_x + lw + gap;
    const auto fill_y  = body_y + lw + gap;
    const auto fill_wf = body_w - 2 * (lw + gap);
    const auto fill_hf = body_h - 2 * (lw + gap);
    const auto fill_w  = fill_wf * percent / 100.0;
    if(fill_w > 0) {
        rounded_rect(cairo, fill_x, fill_y, fill_w, fill_hf, std::min(fill_w, fill_hf) * 0.18);
        cairo_set_source_rgba(cairo, color.r, color.g, color.b, color.a);
        cairo_fill(cairo);
    }

    return block;
}

struct Battery : Module {
    std::string sysfs;
    std::string now_file;
    std::string prefix     = "BAT";
    Style       style      = Style::Simple;
    double      graph_size = 0.5;
    uint32_t    full;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(name, config.find<json::String>("name"));
        const auto charge = config_string(config, "charge", "charge");

        sysfs    = std::format("/sys/class/power_supply/{}/", name.value);
        now_file = std::format("{}_now", charge);
        unwrap(full_str, read_pseudo_file((sysfs + charge + "_full").data()));
        unwrap(full_num, from_chars<uint32_t>(full_str));
        full   = full_num;
        prefix = config_string(config, "prefix", prefix);
        style  = config_string(config, "style", "simple") == "graph" ? Style::Graph : Style::Simple;
        if(const auto size = config.find<json::Number>("size"); size != nullptr) {
            graph_size = size->value;
        }
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(now_str, read_pseudo_file((sysfs + now_file).data()));
        unwrap(now_num, from_chars<uint32_t>(now_str));
        unwrap(status, read_pseudo_file((sysfs + "status").data()));

        auto sign = '=';
        if(status.starts_with("Discharging")) {
            sign = '-';
        } else if(status.starts_with("Charging")) {
            sign = '+';
        }
        const auto percent = uint32_t(100.0 * now_num / full);
        if(style == Style::Graph) {
            draw_graph(target, available, percent, sign, graph_size);
        } else {
            draw_block(target, available, apply_prefix(prefix, std::format("{}{}%", percent, sign)));
        }
    }

    auto click(double /*x*/, double /*y*/, uint32_t /*button*/) -> bool override {
        style = Style(((int)style + 1) % (int)Style::Count);
        return true;
    }
};
} // namespace

REGISTER_MODULE("battery", Battery)
