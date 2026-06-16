#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
// the 1-minute load average from /proc/loadavg.
struct LoadAverage : Module {
    std::string prefix = "LA";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        prefix = config_string(config, "prefix", prefix);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(content, read_pseudo_file("/proc/loadavg"));
        auto load = 0.0;
        if(sscanf(content.data(), "%lf", &load) < 1) {
            return;
        }
        draw_block(target, available, apply_prefix(prefix, std::format("{:.2f}", load)));
    }
};
} // namespace

REGISTER_MODULE("loadavg", LoadAverage)
