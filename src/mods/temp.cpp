#include "../macros/unwrap.hpp"
#include "../mod.hpp"
#include "../util/charconv.hpp"

namespace {
struct Temperature : Module {
    std::string path;
    std::string prefix;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(value, config.find<json::String>("path"));
        path   = value.value;
        prefix = config_string(config, "prefix", prefix);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(content, read_pseudo_file(path.data()));
        unwrap(milli, from_chars<uint32_t>(content));
        draw_block(target, available, apply_prefix(prefix, std::format("{}°C", milli / 1000)));
    }
};
} // namespace

REGISTER_MODULE("temp", Temperature)
