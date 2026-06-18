#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
// the 1-minute load average from /proc/loadavg.
struct LoadAverage : Module {
    std::string format = "LA {}";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        format = config_string(config, "format", format);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(content, read_pseudo_file("/proc/loadavg"));
        auto load = 0.0;
        if(sscanf(content.data(), "%lf", &load) < 1) {
            return;
        }
        draw_block(target, available, apply_format(format, std::format("{:.2f}", load)));
    }
};
} // namespace

REGISTER_MODULE("loadavg", LoadAverage)
