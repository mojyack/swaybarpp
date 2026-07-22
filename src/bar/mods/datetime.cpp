#include <array>
#include <ctime>
#include <string>

#include "../mod.hpp"

namespace {
struct DateTime : Module {
    std::string format = "%Y-%m-%d %H:%M";

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        format = config_string(config, "format", format);
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        const auto now = std::time(nullptr);
        auto       tm  = std::tm();
        localtime_r(&now, &tm);

        auto       buffer = std::array<char, 256>();
        const auto len    = std::strftime(buffer.data(), buffer.size(), format.c_str(), &tm);
        draw_block(target, available, std::string_view(buffer.data(), len));
    }
};
} // namespace

REGISTER_MODULE("datetime", DateTime)
