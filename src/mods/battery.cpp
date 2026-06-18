#include "../macros/unwrap.hpp"
#include "../mod.hpp"
#include "../util/charconv.hpp"

namespace {
struct Battery : Module {
    std::string sysfs;
    std::string now_file;
    std::string prefix = "BAT";
    uint32_t    full;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        unwrap(name, config.find<json::String>("name"));
        const auto charge = config_string(config, "charge", "charge");

        sysfs    = std::format("/sys/class/power_supply/{}/", name.value);
        now_file = std::format("{}_now", charge);
        unwrap(full_str, read_pseudo_file((sysfs + charge + "_full").data()));
        unwrap(full_num, from_chars<uint32_t>(full_str));
        full   = full_num;
        prefix = config_string(config, "prefix", prefix);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        unwrap(now_str, read_pseudo_file((sysfs + now_file).data()));
        unwrap(now_num, from_chars<uint32_t>(now_str));
        unwrap(status, read_pseudo_file((sysfs + "status").data()));

        auto sign = '=';
        if(status.starts_with("Discharging")) {
            sign = '-';
        } else if(status.starts_with("Charging")) {
            sign = '+';
        }
        draw_block(target, available, apply_prefix(prefix, std::format("{}{}%", uint32_t(100.0 * now_num / full), sign)));
    }
};
} // namespace

REGISTER_MODULE("battery", Battery)
