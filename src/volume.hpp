#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>

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

    pw_device* device          = nullptr;
    spa_hook   device_listener = {};
    uint32_t   device_id       = 0;
    int32_t    route_device    = -1; // sink node's card.profile.device
    int32_t    route_index     = -1; // active output route index on the device
    bool       route_resolved  = false;
    bool       route_pending   = false; // device bound, awaiting its Route param

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
    static auto on_node_info(void* data, const pw_node_info* info) -> void;
    static auto on_node_param(void* data, int seq, uint32_t id, uint32_t index, uint32_t next, const spa_pod* param) -> void;
    static auto on_device_param(void* data, int seq, uint32_t id, uint32_t index, uint32_t next, const spa_pod* param) -> void;

    auto bind_sink(uint32_t id) -> void;
    auto bind_device(uint32_t id, int32_t card_profile_device) -> void;
    auto unbind_device() -> void;
    auto write_route_props(const float* volumes, uint32_t count, const bool* mute) -> void;
    auto write_node_props(const float* volumes, uint32_t count, const bool* mute) -> void;
    auto apply_props(const float* volumes, uint32_t count, const bool* mute) -> bool;

    auto init() -> bool;
    auto fd() -> int;
    auto dispatch() -> bool;               // returns true if the volume/mute state changed
    auto available() const -> bool;        // volume data has been received
    auto display_volume() const -> double; // perceptual (cbrt) volume, 0..1
    auto muted() const -> bool;

    auto set_perceptual(double perceptual) -> void; // 0..1
    auto add_perceptual(double delta) -> void;      // clamps to 0..1
    auto set_mute(bool value) -> void;
    auto toggle_mute() -> void;
};
