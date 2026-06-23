#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>

#include "macros/unwrap.hpp"
#include "util/charconv.hpp"
#include "volume.hpp"
#include "json/json.hpp"

auto VolumeControl::on_global(void* const data, const uint32_t id, const uint32_t /*permissions*/, const char* const type, const uint32_t /*version*/, const spa_dict* const props) -> void {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    if(props == nullptr) {
        return;
    }
    if(std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        const auto media_class = spa_dict_lookup(props, "media.class");
        const auto node_name   = spa_dict_lookup(props, "node.name");
        if(media_class == nullptr || node_name == nullptr || std::strcmp(media_class, "Audio/Sink") != 0) {
            return;
        }
        self.sink_names[id] = node_name;
        if(self.sink == nullptr && self.default_sink_name == node_name) {
            self.bind_sink(id);
        }
    } else if(std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const auto name = spa_dict_lookup(props, "metadata.name");
        if(self.metadata == nullptr && name != nullptr && std::strcmp(name, "default") == 0) {
            self.metadata = std::bit_cast<pw_metadata*>(pw_registry_bind(self.registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));

            constexpr static auto events = pw_metadata_events{
                .version  = PW_VERSION_METADATA_EVENTS,
                .property = on_metadata_property,
            };
            pw_metadata_add_listener(self.metadata, &self.metadata_listener, &events, data);
        }
    }
}

auto VolumeControl::on_global_remove(void* const data, const uint32_t id) -> void {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    self.sink_names.erase(id);
    if(self.sink != nullptr && id == self.sink_id) {
        spa_hook_remove(&self.sink_listener);
        pw_proxy_destroy(std::bit_cast<pw_proxy*>(self.sink));
        self.sink = nullptr;
        self.have = false;
    }
    if(self.device != nullptr && id == self.device_id) {
        self.unbind_device();
    }
}

auto VolumeControl::on_metadata_property(void* const data, const uint32_t /*subject*/, const char* const key, const char* const /*type*/, const char* const value) -> int {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    if(key == nullptr || value == nullptr || std::strcmp(key, "default.audio.sink") != 0) {
        return 0;
    }
    // value is json like {"name":"alsa_output...."}
    unwrap(parsed, json::parse(value));
    const auto name = parsed.find<json::String>("name");
    if(name == nullptr || name->value == self.default_sink_name) {
        return 0;
    }
    self.default_sink_name = name->value;

    // re-bind to the new default sink
    if(self.sink != nullptr) {
        spa_hook_remove(&self.sink_listener);
        pw_proxy_destroy(std::bit_cast<pw_proxy*>(self.sink));
        self.sink = nullptr;
        self.have = false;
    }
    self.unbind_device();
    for(const auto& [id, node_name] : self.sink_names) {
        if(node_name == self.default_sink_name) {
            self.bind_sink(id);
            break;
        }
    }
    return 0;
}

auto VolumeControl::on_node_param(void* const data, const int /*seq*/, const uint32_t id, const uint32_t /*index*/, const uint32_t /*next*/, const spa_pod* const param) -> void {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    if(id != SPA_PARAM_Props || param == nullptr || self.route_resolved) {
        return;
    }
    const auto object = std::bit_cast<const spa_pod_object*>(param);
    auto       prop   = (const spa_pod_prop*)(nullptr);
    SPA_POD_OBJECT_FOREACH(object, prop) {
        if(prop->key == SPA_PROP_mute) {
            auto value = false;
            if(spa_pod_get_bool(&prop->value, &value) >= 0) {
                self.mute = value;
            }
        } else if(prop->key == SPA_PROP_channelVolumes) {
            auto       volumes = std::array<float, SPA_AUDIO_MAX_CHANNELS>();
            const auto n       = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, volumes.data(), volumes.size());
            if(n > 0) {
                self.channels = n;
                auto sum      = 0.0f;
                for(auto i = 0u; i < n; i += 1) {
                    sum += volumes[i];
                }
                self.volume = sum / n;
            }
        }
    }
    self.have  = true;
    self.dirty = true;
}

auto VolumeControl::on_node_info(void* const data, const pw_node_info* const info) -> void {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    if(info == nullptr || info->props == nullptr) {
        return;
    }
    const auto device_id_s = spa_dict_lookup(info->props, "device.id");
    const auto cpd_s       = spa_dict_lookup(info->props, "card.profile.device");
    if(device_id_s == nullptr || cpd_s == nullptr) {
        return;
    }
    unwrap(did, from_chars<uint32_t>(device_id_s));
    unwrap(cpd, from_chars<int32_t>(cpd_s));
    if(self.device == nullptr || self.device_id != did || self.route_device != cpd) {
        self.bind_device(did, cpd);
    }
}

auto VolumeControl::on_device_param(void* const data, const int /*seq*/, const uint32_t id, const uint32_t /*index*/, const uint32_t /*next*/, const spa_pod* const param) -> void {
    auto& self = *std::bit_cast<VolumeControl*>(data);
    if(id != SPA_PARAM_Route || param == nullptr || !spa_pod_is_object(param)) {
        return;
    }
    const auto object    = std::bit_cast<const spa_pod_object*>(param);
    auto       r_index   = int32_t(-1);
    auto       r_device  = int32_t(-1);
    auto       is_output = false;
    auto       props     = (const spa_pod*)(nullptr);
    auto       prop      = (const spa_pod_prop*)(nullptr);
    SPA_POD_OBJECT_FOREACH(object, prop) {
        switch(prop->key) {
        case SPA_PARAM_ROUTE_index:
            spa_pod_get_int(&prop->value, &r_index);
            break;
        case SPA_PARAM_ROUTE_device:
            spa_pod_get_int(&prop->value, &r_device);
            break;
        case SPA_PARAM_ROUTE_direction: {
            auto dir = uint32_t(0);
            if(spa_pod_get_id(&prop->value, &dir) >= 0) {
                is_output = dir == SPA_DIRECTION_OUTPUT;
            }
        } break;
        case SPA_PARAM_ROUTE_props:
            props = &prop->value;
            break;
        }
    }
    // only the active output route that backs our sink node matters
    if(!is_output || r_device != self.route_device) {
        return;
    }
    self.route_index    = r_index;
    self.route_resolved = true;
    self.route_pending  = false;

    if(props == nullptr || !spa_pod_is_object(props)) {
        return;
    }
    const auto props_obj = std::bit_cast<const spa_pod_object*>(props);
    auto       pprop     = (const spa_pod_prop*)(nullptr);
    SPA_POD_OBJECT_FOREACH(props_obj, pprop) {
        if(pprop->key == SPA_PROP_mute) {
            auto value = false;
            if(spa_pod_get_bool(&pprop->value, &value) >= 0) {
                self.mute = value;
            }
        } else if(pprop->key == SPA_PROP_channelVolumes) {
            auto       volumes = std::array<float, SPA_AUDIO_MAX_CHANNELS>();
            const auto n       = spa_pod_copy_array(&pprop->value, SPA_TYPE_Float, volumes.data(), volumes.size());
            if(n > 0) {
                self.channels = n;
                auto sum      = 0.0f;
                for(auto i = 0u; i < n; i += 1) {
                    sum += volumes[i];
                }
                self.volume = sum / n;
            }
        }
    }
    self.have  = true;
    self.dirty = true;
}

auto VolumeControl::bind_sink(const uint32_t id) -> void {
    sink_id = id;
    sink    = std::bit_cast<pw_node*>(pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));

    static constexpr auto events = pw_node_events{
        .version = PW_VERSION_NODE_EVENTS,
        .info    = on_node_info,
        .param   = on_node_param,
    };
    pw_node_add_listener(sink, &sink_listener, &events, this);
    auto ids = std::array<uint32_t, 1>{SPA_PARAM_Props};
    pw_node_subscribe_params(sink, ids.data(), ids.size());

    route_pending = true;
}

auto VolumeControl::bind_device(const uint32_t id, const int32_t card_profile_device) -> void {
    unbind_device();
    device_id     = id;
    route_device  = card_profile_device;
    route_pending = true;
    device        = std::bit_cast<pw_device*>(pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0));

    static constexpr auto events = pw_device_events{
        .version = PW_VERSION_DEVICE_EVENTS,
        .param   = on_device_param,
    };
    pw_device_add_listener(device, &device_listener, &events, this);
    auto ids = std::array<uint32_t, 1>{SPA_PARAM_Route};
    pw_device_subscribe_params(device, ids.data(), ids.size());
}

auto VolumeControl::unbind_device() -> void {
    if(device != nullptr) {
        spa_hook_remove(&device_listener);
        pw_proxy_destroy(std::bit_cast<pw_proxy*>(device));
        device = nullptr;
    }
    device_id      = 0;
    route_device   = -1;
    route_index    = -1;
    route_resolved = false;
    route_pending  = false;
}

auto VolumeControl::write_route_props(const float* const volumes, const uint32_t count, const bool* const mute) -> void {
    if(device == nullptr || !route_resolved) {
        return;
    }
    auto buffer  = std::array<uint8_t, 1024>();
    auto builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
    auto outer   = spa_pod_frame();
    auto inner   = spa_pod_frame();
    spa_pod_builder_push_object(&builder, &outer, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
    spa_pod_builder_prop(&builder, SPA_PARAM_ROUTE_index, 0);
    spa_pod_builder_int(&builder, route_index);
    spa_pod_builder_prop(&builder, SPA_PARAM_ROUTE_device, 0);
    spa_pod_builder_int(&builder, route_device);
    spa_pod_builder_prop(&builder, SPA_PARAM_ROUTE_props, 0);
    spa_pod_builder_push_object(&builder, &inner, SPA_TYPE_OBJECT_Props, SPA_PARAM_Route);
    if(volumes != nullptr) {
        spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
        spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, count, volumes);
    }
    if(mute != nullptr) {
        spa_pod_builder_prop(&builder, SPA_PROP_mute, 0);
        spa_pod_builder_bool(&builder, *mute);
    }
    spa_pod_builder_pop(&builder, &inner);
    spa_pod_builder_prop(&builder, SPA_PARAM_ROUTE_save, 0);
    spa_pod_builder_bool(&builder, true);
    const auto pod = std::bit_cast<spa_pod*>(spa_pod_builder_pop(&builder, &outer));
    pw_device_set_param(device, SPA_PARAM_Route, 0, pod);
    pw_loop_iterate(loop, 0);
}

auto VolumeControl::write_node_props(const float* const volumes, const uint32_t count, const bool* const mute) -> void {
    if(sink == nullptr) {
        return;
    }
    auto buffer  = std::array<uint8_t, 1024>();
    auto builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
    auto frame   = spa_pod_frame();
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if(volumes != nullptr) {
        spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
        spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, count, volumes);
    }
    if(mute != nullptr) {
        spa_pod_builder_prop(&builder, SPA_PROP_mute, 0);
        spa_pod_builder_bool(&builder, *mute);
    }
    const auto pod = std::bit_cast<spa_pod*>(spa_pod_builder_pop(&builder, &frame));
    pw_node_set_param(sink, SPA_PARAM_Props, 0, pod);
    pw_loop_iterate(loop, 0);
}

auto VolumeControl::apply_props(const float* const volumes, const uint32_t count, const bool* const mute) -> bool {
    if(route_resolved && device != nullptr) {
        write_route_props(volumes, count, mute);
        return true;
    }
    if(sink != nullptr) {
        write_node_props(volumes, count, mute);
        return true;
    }
    return false;
}

auto VolumeControl::init() -> bool {
    pw_init(nullptr, nullptr);

    main_loop = pw_main_loop_new(nullptr);
    ensure(main_loop);
    loop    = pw_main_loop_get_loop(main_loop);
    context = pw_context_new(loop, nullptr, 0);
    ensure(context);
    core = pw_context_connect(context, nullptr, 0);
    ensure(core);
    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);

    constexpr static auto events = pw_registry_events{
        .version       = PW_VERSION_REGISTRY_EVENTS,
        .global        = on_global,
        .global_remove = on_global_remove,
    };
    pw_registry_add_listener(registry, &registry_listener, &events, this);

    pw_loop_enter(loop);
    // pump the loop until we have volume data and device route
    for(auto i = 0; i < 50 && (!have || route_pending); i += 1) {
        pw_loop_iterate(loop, 10);
    }
    return true;
}

auto VolumeControl::fd() -> int {
    return pw_loop_get_fd(loop);
}

auto VolumeControl::dispatch() -> bool {
    pw_loop_iterate(loop, 0);
    const auto was_dirty = dirty;
    dirty                = false;
    return was_dirty;
}

auto VolumeControl::available() const -> bool {
    return have;
}

auto VolumeControl::display_volume() const -> double {
    return std::cbrt(volume);
}

auto VolumeControl::muted() const -> bool {
    return mute;
}

auto VolumeControl::set_perceptual(const double perceptual) -> void {
    const auto clamped = std::clamp(perceptual, 0.0, 1.0);
    const auto linear  = float(clamped * clamped * clamped);
    auto       values  = std::array<float, SPA_AUDIO_MAX_CHANNELS>();
    const auto count   = std::max(channels, 1u);
    std::fill_n(values.begin(), count, linear);

    if(!apply_props(values.data(), count, nullptr)) {
        return;
    }
    volume = linear;
    dirty  = true;
}

auto VolumeControl::add_perceptual(const double delta) -> void {
    set_perceptual(display_volume() + delta);
}

auto VolumeControl::set_mute(const bool value) -> void {
    if(!apply_props(nullptr, 0, &value)) {
        return;
    }
    mute  = value;
    dirty = true;
}

auto VolumeControl::toggle_mute() -> void {
    set_mute(!mute);
}
