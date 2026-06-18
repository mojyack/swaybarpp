#include <fcntl.h>
#include <unistd.h>

#include "macros/unwrap.hpp"
#include "slider/slider_app.hpp"
#include "slider/slider_model.hpp"
#include "util/charconv.hpp"
#include "util/fd.hpp"

namespace {
auto read_int(const char* const path) -> std::optional<size_t> {
    const auto fd = FileDescriptor(::open(path, O_RDONLY | O_CLOEXEC));
    ensure(fd.as_handle() >= 0);
    auto       buffer = std::array<char, 32>();
    const auto len    = ::read(fd.as_handle(), buffer.data(), buffer.size() - 1);
    ensure(len > 0);

    auto str = std::string_view(buffer.data(), len);
    while(!str.empty() && (str.back() == '\n' || str.back() == ' ')) {
        str.remove_suffix(1);
    }
    unwrap(num, from_chars<size_t>(str));
    return num;
}

auto write_int(const std::string& path, const size_t value) -> void {
    const auto fd = FileDescriptor(::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC));
    ensure(fd.as_handle() >= 0);
    const auto text = std::to_string(value);
    ::write(fd.as_handle(), text.data(), text.size());
}

struct BrightnessControl : SliderModel {
    std::string cur_path;
    size_t      max = 0;
    size_t      cur = 0;
    bool        ok  = false;

    auto init(const std::string& dir) -> bool {
        cur_path      = dir + "/brightness";
        auto max_path = dir + "/max_brightness";
        unwrap(max, read_int(max_path.data()));
        unwrap(cur, read_int(cur_path.data()));
        ensure(max > 0);
        this->max = max;
        this->cur = std::clamp(cur, 0uz, max);
        ok        = true;
        return true;
    }

    auto available() const -> bool override {
        return ok;
    }

    auto fraction() const -> double override {
        return max > 0 ? double(cur) / double(max) : 0.0;
    }

    auto set_fraction(const double value) -> void override {
        if(ok) {
            cur = size_t(std::lround(std::clamp(value, 0.0, 1.0) * max));
            write_int(cur_path.data(), cur);
        }
    }
};
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    ensure(argc >= 2);
    const auto dir  = argv[1];
    const auto cmd  = argc >= 3 ? argv[2] : "show";
    const auto path = slider::socket_path("swaybarpp-brightness");
    if(slider::forward(path.data(), cmd)) {
        return 0;
    }

    auto model = BrightnessControl();
    ensure(model.init(dir));
    return slider::run_server(path.data(), model, cmd);
}
