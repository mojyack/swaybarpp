#include <format>
#include <string>
#include <vector>

#include <linux/input-event-codes.h>
#include <sys/epoll.h>

#include "../macros/unwrap.hpp"
#include "../mod.hpp"
#include "../sway_ipc.hpp"

namespace {
constexpr auto padding = 6.0;

auto set_source(cairo_t* const cairo, const Color& color) -> void {
    cairo_set_source_rgba(cairo, color.r, color.g, color.b, color.a);
}

auto reserve(RenderTarget& target, Rect& available, const double width) -> double {
    const auto x = target.align == Align::Left ? available.x : available.x + available.w - width;
    if(target.align == Align::Left) {
        available.x += width;
    }
    available.w -= width;
    return x;
}

struct Workspace : Module {
    struct Entry {
        std::string name;
        std::string output;
        int         num;
        bool        focused;
        bool        visible;
        bool        urgent;
    };

    struct Button {
        std::string name;
        int         num;
        double      x0;
        double      x1;
    };

    FileDescriptor      sock;
    int                 epfd = -1;
    std::vector<Entry>  workspaces;
    std::vector<Button> buttons;

    auto init(const int epfd, const json::Object& config) -> bool override {
        this->epfd = epfd;

        auto path = std::string();
        if(const auto obj = config.find<json::String>("socket")) {
            path = obj->value;
        } else if(const auto env = std::getenv("SWAYSOCK")) {
            path = env;
        } else {
            bail("could not find sway socket");
        }

        {
            unwrap_mut(s, sway_ipc::connect(path.c_str()));
            this->sock = std::move(s);
        }

        sway_ipc::send(sock, sway_ipc::subscribe, R"(["workspace"])");
        request();

        auto event     = epoll_event{.events = EPOLLIN};
        event.data.ptr = this;
        ensure(epoll_ctl(epfd, EPOLL_CTL_ADD, sock.as_handle(), &event) == 0);
        return true;
    }

    auto request() -> void {
        sway_ipc::send(sock, sway_ipc::get_workspaces, "");
    }

    auto disconnect() -> void {
        epoll_ctl(epfd, EPOLL_CTL_DEL, sock.as_handle(), nullptr);
        sock.close();
        workspaces.clear();
    }

    auto read() -> bool override {
        const auto message = sway_ipc::recv(sock);
        if(!message) {
            disconnect(); // sway closed the connection; stop polling the dead socket
            return true;
        }

        switch(message->type) {
        case sway_ipc::get_workspaces:
            parse_workspaces(message->payload);
            return true;
        case sway_ipc::workspace_event:
            request(); // the layout changed; re-query the full list
            return false;
        default:
            return false;
        }
    }

    auto parse_workspaces(const std::string& payload) -> void {
        const auto wrapped = std::format("{{\"v\":{}}}", payload); // wrap to object for tinyjson
        unwrap(parsed, json::parse(wrapped));
        unwrap(array, parsed.find<json::Array>("v"));
        workspaces.clear();
        for(const auto& value : array.value) {
            const auto object = value.get<json::Object>();
            if(!object) {
                continue;
            }
            const auto name    = object->find<json::String>("name");
            const auto output  = object->find<json::String>("output");
            const auto num     = object->find<json::Number>("num");
            const auto focused = object->find<json::Boolean>("focused");
            const auto visible = object->find<json::Boolean>("visible");
            const auto urgent  = object->find<json::Boolean>("urgent");
            workspaces.push_back(Entry{
                .name    = name ? name->value : std::string(),
                .output  = output ? output->value : std::string(),
                .num     = num ? int(num->value) : -1,
                .focused = focused && focused->value,
                .visible = visible && visible->value,
                .urgent  = urgent && urgent->value,
            });
        }
    }

    // rndering

    auto draw_button(RenderTarget& target, const Rect& rect, const Entry& ws, PangoLayout* const layout) -> void {
        if(ws.focused || ws.urgent) {
            set_source(target.cairo, target.foreground);
            cairo_rectangle(target.cairo, rect.x, rect.y, rect.w, rect.h);
            cairo_fill(target.cairo);
        } else if(ws.visible) {
            set_source(target.cairo, target.foreground);
            cairo_set_line_width(target.cairo, 1);
            cairo_rectangle(target.cairo, rect.x + 0.5, rect.y + 0.5, rect.w - 1, rect.h - 1);
            cairo_stroke(target.cairo);
        }

        auto text_height = 0;
        pango_layout_get_pixel_size(layout, nullptr, &text_height);
        set_source(target.cairo, ws.focused || ws.urgent ? target.background : target.foreground);
        cairo_move_to(target.cairo, rect.x + padding, rect.y + (rect.h - text_height) / 2);
        pango_cairo_show_layout(target.cairo, layout);
    }

    auto draw(RenderTarget& target, Rect& available) -> void override {
        buttons.clear();

        // filter by output name
        auto visible = std::vector<const Entry*>();
        for(const auto& ws : workspaces) {
            if(target.output.empty() || ws.output == target.output) {
                visible.push_back(&ws);
            }
        }
        if(visible.empty()) {
            return;
        }

        // lay out a label for every workspace and sum up the width they consume
        struct Slot {
            PangoLayout* layout;
            double       width;
        };
        auto slots = std::vector<Slot>();
        auto total = 0.0;
        for(const auto ws : visible) {
            const auto layout = pango_cairo_create_layout(target.cairo);
            pango_layout_set_font_description(layout, target.font);
            pango_layout_set_text(layout, ws->name.data(), ws->name.size());
            auto text_width = 0;
            pango_layout_get_pixel_size(layout, &text_width, nullptr);

            const auto width = text_width + padding * 2;
            slots.push_back(Slot{.layout = layout, .width = width});
            total += width;
        }

        auto x = reserve(target, available, total);
        for(auto i = 0uz; i < visible.size(); i += 1) {
            const auto& ws   = *visible[i];
            const auto  rect = Rect{.x = x, .y = available.y, .w = slots[i].width, .h = available.h};
            draw_button(target, rect, ws, slots[i].layout);
            g_object_unref(slots[i].layout);

            buttons.push_back(Button{.name = ws.name, .num = ws.num, .x0 = x, .x1 = x + slots[i].width});
            x += slots[i].width;
        }
    }

    // input

    auto click(const double x, const double /*y*/, const uint32_t button) -> bool override {
        if(button != BTN_LEFT || sock.as_handle() < 0) {
            return false;
        }
        for(const auto& b : buttons) {
            if(x < b.x0 || x >= b.x1) {
                continue;
            }
            const auto command = b.num >= 0 ? std::format("workspace number {}", b.num) : std::format("workspace \"{}\"", b.name);
            sway_ipc::send(sock, sway_ipc::run_command, command);
            break;
        }
        return false;
    }
};
} // namespace

REGISTER_MODULE("workspace", Workspace)
