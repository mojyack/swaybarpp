#include "../macros/unwrap.hpp"
#include "../mod.hpp"
#include "../util/charconv.hpp"

namespace {
struct Temperature : Module {
    std::string path;
    std::string format = "{}°C";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(value, config.find<json::String>("path"));
        path   = value.value;
        format = config_string(config, "format", format);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(content, read_pseudo_file(path.data()));
        unwrap(milli, from_chars<uint32_t>(content));
        draw_block(target, available, apply_format(format, std::format("{}", milli / 1000)));
    }
};
} // namespace

REGISTER_MODULE("temp", Temperature)
