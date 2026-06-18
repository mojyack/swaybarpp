#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "mod.hpp"

auto draw_block(RenderTarget& target, Rect& available, const std::string_view text) -> Rect {
    constexpr auto padding = 6.0;

    const auto layout = pango_cairo_create_layout(target.cairo);
    pango_layout_set_font_description(layout, target.font);
    pango_layout_set_text(layout, text.data(), text.size());

    auto text_width  = 0;
    auto text_height = 0;
    pango_layout_get_pixel_size(layout, &text_width, &text_height);

    const auto block_width = text_width + padding * 2;
    const auto block       = Rect{
              .x = target.align == Align::Left ? available.x : available.x + available.w - block_width,
              .y = available.y,
              .w = double(block_width),
              .h = available.h,
    };
    if(target.align == Align::Left) {
        available.x += block_width;
    }
    available.w -= block_width;

    const auto y = block.y + (block.h - text_height) / 2;
    cairo_set_source_rgba(target.cairo, target.foreground.r, target.foreground.g, target.foreground.b, target.foreground.a);
    cairo_move_to(target.cairo, block.x + padding, y);
    pango_cairo_show_layout(target.cairo, layout);
    g_object_unref(layout);

    return block;
}

auto config_string(const json::Object& config, const std::string_view key, const std::string_view fallback) -> std::string {
    const auto value = config.find<json::String>(key);
    return value != nullptr ? value->value : std::string(fallback);
}

auto apply_format(const std::string_view format, const std::string_view value) -> std::string {
    const auto pos = format.find("{}");
    if(pos == std::string_view::npos) {
        return std::string(format);
    }
    return std::string(format.substr(0, pos)) + std::string(value) + std::string(format.substr(pos + 2));
}

auto read_pseudo_file(const char* const path) -> std::optional<std::string> {
    const auto fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        return std::nullopt;
    }

    auto buffer = std::string();
    auto chunk  = std::array<char, 4096>();
    while(true) {
        const auto len = ::read(fd, chunk.data(), chunk.size());
        if(len < 0) {
            if(errno == EINTR) {
                continue;
            }
            ::close(fd);
            return std::nullopt;
        }
        if(len == 0) {
            break;
        }
        buffer.append(chunk.data(), len);
    }

    ::close(fd);
    return buffer;
}

namespace {
// function-local static avoids the static-init order fiasco
auto registry() -> std::vector<std::pair<std::string_view, ModuleFactory>>& {
    static auto instance = std::vector<std::pair<std::string_view, ModuleFactory>>();
    return instance;
}
} // namespace

auto register_module(const std::string_view name, const ModuleFactory factory) -> bool {
    registry().emplace_back(name, factory);
    return true;
}

auto create_module(const std::string_view name) -> std::unique_ptr<Module> {
    for(const auto& [key, factory] : registry()) {
        if(key == name) {
            return factory();
        }
    }
    return nullptr;
}
