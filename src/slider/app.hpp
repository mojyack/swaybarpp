#pragma once
#include <string>
#include <string_view>

#include "model.hpp"

namespace slider {
auto socket_path(std::string_view name) -> std::string;
auto forward(const char* path, const char* cmd) -> bool;
auto run_server(const char* path, SliderModel& model, const char* initial_cmd) -> int;
auto apply_command(SliderModel& model, std::string_view cmd) -> void;
} // namespace slider
