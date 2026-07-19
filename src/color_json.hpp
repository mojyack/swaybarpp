#pragma once
#include "cairo_util.hpp"
#include "json/json.hpp"
#include "macros/unwrap.hpp"
#include "util/charconv.hpp"

// serde support for Color ("#rrggbb[aa]"), include before serde/json/format.hpp

namespace serde {
struct JsonFormat;
}

namespace serde::json {
inline auto serialize_element(JsonFormat& /*format*/, ::json::Value& /*value*/, const Color& /*data*/) -> bool {
    return false;
}

inline auto deserialize_element(JsonFormat& /*format*/, const ::json::Value& value, Color& data) -> bool {
    unwrap(node, value.get<::json::String>());
    auto str = std::string_view(node.value);
    if(str.starts_with('#')) {
        str.remove_prefix(1);
    }
    auto hex = std::string(str);
    if(hex.size() == 6) {
        hex += "ff"; // default to opaque
    }
    ensure(hex.size() == 8);
    unwrap(num, from_chars<uint32_t>(hex, 16));
    data = Color::from_hex(num);
    return true;
}
} // namespace serde::json
