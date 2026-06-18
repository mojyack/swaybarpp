#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>

// native libpipewire volume control for the default audio sink.
// tracks the "default" metadata, binds the current default sink, and exposes
// perceptual (cbrt) volume + mute, matching wpctl. shared by the pipewire bar
// module and the standalone volume slider.
struct VolumeControl {
    pw_main_loop* main_loop         = nullptr;
    pw_loop*      loop              = nullptr;
    pw_context*   context           = nullptr;
    pw_core*      core              = nullptr;
    pw_registry*  registry          = nullptr;
    spa_hook      registry_listener = {};

    pw_metadata* metadata          = nullptr;
    spa_hook     metadata_listener = {};

    pw_node* sink          = nullptr;
    spa_hook sink_listener = {};
    uint32_t sink_id       = 0;

    std::unordered_map<uint32_t, std::string> sink_names; // node id -> node.name
    std::string                               default_sink_name;

    float    volume   = 0; // linear, per the SPA_PROP_channelVolumes definition
    uint32_t channels = 2;
    bool     have     = false;
    bool     mute     = false;
    bool     dirty    = false;

    // pipewire callbacks
    static auto on_global(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version, const spa_dict* props) -> void;
    static auto on_global_remove(void* data, uint32_t id) -> void;
    static auto on_metadata_property(void* data, uint32_t subject, const char* key, const char* type, const char* value) -> int;
    static auto on_node_param(void* data, int seq, uint32_t id, uint32_t index, uint32_t next, const spa_pod* param) -> void;

    auto bind_sink(uint32_t id) -> void;

    // setup: connect to pipewire and pump the loop until volume data arrives.
    auto init() -> bool;
    // the loop fd to poll for readability.
    auto fd() -> int;
    // pump pending events; returns true if the volume/mute state changed.
    auto dispatch() -> bool;

    auto available() const -> bool;        // volume data has been received
    auto display_volume() const -> double; // perceptual (cbrt) volume, 0..1
    auto muted() const -> bool;

    auto set_perceptual(double perceptual) -> void; // 0..1
    auto add_perceptual(double delta) -> void;       // clamps to 0..1
    auto set_mute(bool value) -> void;
    auto toggle_mute() -> void;
};
