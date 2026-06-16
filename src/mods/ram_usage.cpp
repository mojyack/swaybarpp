#include <cstdlib>
#include <format>

#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
struct RamUsage : Module {
    std::string prefix = "MEM";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        prefix = config_string(config, "prefix", prefix);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(content, read_pseudo_file("/proc/meminfo"));

        const auto field = [&](const std::string_view key) -> uint64_t {
            const auto pos = content.find(key);
            if(pos == std::string::npos) {
                return 0;
            }
            return std::strtoull(content.data() + pos + key.size(), nullptr, 10);
        };

        const auto total_kb = field("MemTotal:");
        const auto avail_kb = field("MemAvailable:");
        const auto used_gib = (total_kb - avail_kb) / 1024.0 / 1024.0;
        draw_block(target, available, apply_prefix(prefix, std::format("{:.1f}G", used_gib)));
    }
};
} // namespace

REGISTER_MODULE("ram_usage", RamUsage)
