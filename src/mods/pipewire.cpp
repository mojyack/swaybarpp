#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <string>
#include <unordered_map>

#include <sys/epoll.h>

#include <linux/input-event-codes.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>

#include "../macros/unwrap.hpp"
#include "../mod.hpp"

namespace {
constexpr auto step = 0.02;

struct Pipewire : Module {
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

    std::string prefix       = "VOL";  // shown before the volume percentage
    std::string prefix_muted = "MUTE"; // shown alone when the sink is muted

    float    volume   = 0; // linear, per the SPA_PROP_channelVolumes definition
    uint32_t channels = 2;
    bool     have     = false;
    bool     mute     = false;
    bool     dirty    = false;

    // pipewire callbacks

    static auto on_global(void* const data, const uint32_t id, const uint32_t /*permissions*/, const char* const type, const uint32_t /*version*/, const spa_dict* const props) -> void {
        auto& self = *std::bit_cast<Pipewire*>(data);
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

    static auto on_global_remove(void* const data, const uint32_t id) -> void {
        auto& self = *std::bit_cast<Pipewire*>(data);
        self.sink_names.erase(id);
        if(self.sink != nullptr && id == self.sink_id) {
            spa_hook_remove(&self.sink_listener);
            pw_proxy_destroy(std::bit_cast<pw_proxy*>(self.sink));
            self.sink = nullptr;
            self.have = false;
        }
    }

    static auto on_metadata_property(void* const data, const uint32_t /*subject*/, const char* const key, const char* const /*type*/, const char* const value) -> int {
        auto& self = *std::bit_cast<Pipewire*>(data);
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
        for(const auto& [id, node_name] : self.sink_names) {
            if(node_name == self.default_sink_name) {
                self.bind_sink(id);
                break;
            }
        }
        return 0;
    }

    static auto on_node_param(void* const data, const int /*seq*/, const uint32_t id, const uint32_t /*index*/, const uint32_t /*next*/, const spa_pod* const param) -> void {
        auto& self = *std::bit_cast<Pipewire*>(data);
        if(id != SPA_PARAM_Props || param == nullptr) {
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

    auto bind_sink(const uint32_t id) -> void {
        sink_id = id;
        sink    = std::bit_cast<pw_node*>(pw_registry_bind(registry, id, PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0));

        static constexpr auto events = pw_node_events{
            .version = PW_VERSION_NODE_EVENTS,
            .param   = on_node_param,
        };
        pw_node_add_listener(sink, &sink_listener, &events, this);
        auto ids = std::array<uint32_t, 1>{SPA_PARAM_Props};
        pw_node_subscribe_params(sink, ids.data(), ids.size());
    }

    auto display_volume() const -> double {
        return std::cbrt(volume);
    }

    auto set_volume(const double perceptual) -> void {
        if(sink == nullptr) {
            return;
        }
        const auto linear = float(perceptual * perceptual * perceptual);
        auto       values = std::array<float, SPA_AUDIO_MAX_CHANNELS>();
        const auto count  = std::max(channels, 1u);
        std::fill_n(values.begin(), count, linear);

        auto buffer  = std::array<uint8_t, 1024>();
        auto builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
        auto frame   = spa_pod_frame();
        spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
        spa_pod_builder_prop(&builder, SPA_PROP_channelVolumes, 0);
        spa_pod_builder_array(&builder, sizeof(float), SPA_TYPE_Float, count, values.data());
        const auto pod = std::bit_cast<spa_pod*>(spa_pod_builder_pop(&builder, &frame));
        pw_node_set_param(sink, SPA_PARAM_Props, 0, pod);
        pw_loop_iterate(loop, 0);
    }

    auto set_mute(const bool value) -> void {
        if(sink == nullptr) {
            return;
        }
        auto buffer  = std::array<uint8_t, 256>();
        auto builder = SPA_POD_BUILDER_INIT(buffer.data(), buffer.size());
        auto frame   = spa_pod_frame{};
        spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
        spa_pod_builder_prop(&builder, SPA_PROP_mute, 0);
        spa_pod_builder_bool(&builder, value);
        const auto pod = std::bit_cast<spa_pod*>(spa_pod_builder_pop(&builder, &frame));
        pw_node_set_param(sink, SPA_PARAM_Props, 0, pod);
        pw_loop_iterate(loop, 0);
    }

    // module

    auto init(const int epfd, const json::Object& config) -> bool override {
        prefix       = config_string(config, "prefix", prefix);
        prefix_muted = config_string(config, "prefix_muted", prefix_muted);

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
        // pump the loop briefly so the bar has volume data on the first draw
        for(auto i = 0; i < 50 && !have; i += 1) {
            pw_loop_iterate(loop, 10);
        }

        auto event     = epoll_event{.events = EPOLLIN};
        event.data.ptr = this;
        ensure(epoll_ctl(epfd, EPOLL_CTL_ADD, pw_loop_get_fd(loop), &event) == 0);
        return true;
    }

    auto read() -> bool override {
        pw_loop_iterate(loop, 0);
        const auto was_dirty = dirty;
        dirty                = false;
        return was_dirty;
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        if(!have) {
            return;
        }
        const auto text = mute ? prefix_muted : apply_prefix(prefix, std::format("{}%", int(std::lround(display_volume() * 100))));
        draw_block(target, available, text);
    }

    auto click(const double /*x*/, const double /*y*/, const uint32_t button) -> bool override {
        if(button != BTN_LEFT) {
            return false;
        }
        mute = !mute;
        set_mute(mute);
        return true;
    }

    auto scroll(const double /*x*/, const double /*y*/, const double /*dx*/, const double dy) -> bool override {
        if(!have || dy == 0) {
            return false;
        }
        const auto next = std::clamp(display_volume() + (dy > 0 ? step : -step), 0.0, 1.0);
        set_volume(next);
        volume = float(next * next * next);
        return true;
    }
};
} // namespace

REGISTER_MODULE("pipewire", Pipewire)
