#include "../mod.hpp"

namespace {
struct Padding : Module {
    double size = 8;

    auto init(const int /*epfd*/, const json::Object& config) -> bool override {
        const auto value = config.find<json::Number>("size");
        if(value != nullptr) {
            size = value->value;
        }
        return true;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        if(target.align == Align::Left) {
            available.x += size;
        }
        available.w -= size;
    }
};
} // namespace

REGISTER_MODULE("padding", Padding)
