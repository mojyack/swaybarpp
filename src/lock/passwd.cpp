#include <print>

#include "../macros/unwrap.hpp"
#include "passwd-file.hpp"

namespace {
const auto help = R"(usage: swaybarpp-lock-passwd [-o PATH] PIN
    PIN     numeric password ([0-9]+)
    -o PATH output file (default: $XDG_DATA_HOME/swaybarpp/passwd))";
} // namespace

auto main(const int argc, const char* const* const argv) -> int {
    constexpr auto error_value = 1;

    auto path = std::string();
    auto pin  = std::string_view();
    for(auto i = 1; i < argc; i += 1) {
        const auto arg = std::string_view(argv[i]);
        if(arg == "-h" || arg == "--help") {
            std::println("{}", help);
            return 0;
        } else if(arg == "-o") {
            ensure_v(i + 1 < argc, "-o requires an argument");
            i += 1;
            path = argv[i];
        } else {
            pin = arg;
        }
    }
    ensure_v(!pin.empty() && pin.find_first_not_of("0123456789") == pin.npos, "{}", help);
    ensure_v(pin.size() <= 32, "pin too long");
    if(path.empty()) {
        path = passwd::default_path();
        ensure_v(!path.empty(), "cannot determine output path");
    }

    unwrap_v(file, passwd::generate(pin));
    ensure_v(passwd::save(path.data(), file));
    std::println("wrote {}", path);
    return 0;
}
