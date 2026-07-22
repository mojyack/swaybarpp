#include "../../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
struct File : Module {
    std::string path;
    std::string format = "{}";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(value, config.find<json::String>("path"));
        path   = value.value;
        format = config_string(config, "format", format);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap_mut(content, read_pseudo_file(path.data()));
        std::erase(content, '\n');
        draw_block(target, available, apply_format(format, content));
    }
};
} // namespace

REGISTER_MODULE("file", File)
