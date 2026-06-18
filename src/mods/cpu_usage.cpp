#include <chrono>

#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
struct CpuUsage : Module {
    using Clock = std::chrono::steady_clock;

    std::string       format     = "CPU {}";
    uint64_t          prev_total = 0;
    uint64_t          prev_idle  = 0;
    int               usage      = 0;
    Clock::time_point prev_sample;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        format = config_string(config, "format", format);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        update();
        draw_block(target, available, apply_format(format, std::format("{}%", usage)));
    }

    auto update() -> void {
        const auto now = Clock::now();
        if(prev_sample != Clock::time_point() && now - prev_sample < std::chrono::milliseconds(500)) {
            return;
        }
        prev_sample = now;

        unwrap(content, read_pseudo_file("/proc/stat"));

        auto user = 0ull, nice = 0ull, system = 0ull, idle = 0ull;
        auto iowait = 0ull, irq = 0ull, softirq = 0ull, steal = 0ull;
        if(sscanf(content.data(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                  &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 8) {
            return;
        }

        const auto idle_all = idle + iowait;
        const auto total    = user + nice + system + idle + iowait + irq + softirq + steal;
        const auto d_total  = total - prev_total;
        const auto d_idle   = idle_all - prev_idle;
        if(prev_total != 0 && d_total > 0) {
            usage = (d_total - d_idle) * 100 / d_total;
        }
        prev_total = total;
        prev_idle  = idle_all;
    }
};
} // namespace

REGISTER_MODULE("cpu_usage", CpuUsage)
