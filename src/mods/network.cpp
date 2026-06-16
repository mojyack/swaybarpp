#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
struct Network : Module {
    std::string prefix;
    std::string offline = "!";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        prefix  = config_string(config, "prefix", prefix);
        offline = config_string(config, "offline", offline);
        return true;
    }

    // returns the interface of the lowest-metric, up default route, or nullopt if none
    auto active_interface() const -> std::optional<std::string> {
        unwrap(content, read_pseudo_file("/proc/net/route"));

        constexpr auto rtf_up = 0x0001ul; // route is usable

        auto best_iface  = std::string();
        auto best_metric = std::numeric_limits<long>::max();
        for(auto pos = content.find('\n'); pos != std::string::npos;) {
            const auto next = content.find('\n', pos + 1);
            const auto line = content.c_str() + pos + 1;

            auto iface = std::array<char, 32>();
            auto dest = 0ul, gateway = 0ul, flags = 0ul, mask = 0ul;
            auto refcnt = 0, use = 0, metric = 0;
            if(std::sscanf(line, "%31s %lx %lx %lx %d %d %d %lx",
                           iface.data(), &dest, &gateway, &flags, &refcnt, &use, &metric, &mask) == 8) {
                // a default route has destination 0.0.0.0; pick the lowest metric
                if(dest == 0 && (flags & rtf_up) != 0 && metric < best_metric) {
                    best_metric = metric;
                    best_iface  = iface.data();
                }
            }
            pos = next;
        }
        return best_iface;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        const auto iface = active_interface();
        const auto text  = !iface ? offline : apply_prefix(prefix, *iface);
        if(!text.empty()) {
            draw_block(target, available, text);
        }
    }
};
} // namespace

REGISTER_MODULE("network", Network)
