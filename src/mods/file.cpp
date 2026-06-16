#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
struct File : Module {
    std::string path;
    std::string prefix;
    std::string suffix;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(value, config.find<json::String>("path"));
        path   = value.value;
        prefix = config_string(config, "prefix", prefix);
        suffix = config_string(config, "suffix", suffix);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap_mut(content, read_pseudo_file(path.data()));
        std::erase(content, '\n');
        draw_block(target, available, prefix + content + suffix);
    }
};
} // namespace

REGISTER_MODULE("file", File)
