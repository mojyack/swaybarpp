#include <bit>
#include <format>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <unistd.h>

#include "macros/assert.hpp"
#include "window.hpp"

namespace {
auto make_shm(const size_t size) -> FileDescriptor {
    static auto counter = 0;

    const auto name = std::format("/swaybarpp-{}-{}", getpid(), counter += 1);
    auto       fd   = FileDescriptor(shm_open(name.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_EXCL, 0600));
    ASSERT(fd.as_handle() >= 0, "failed to open shared memory");
    shm_unlink(name.c_str());
    ASSERT(ftruncate(fd.as_handle(), size) >= 0);
    return fd;
}
} // namespace

// buffer

auto Buffer::on_release(void* const data, wl_buffer* const /*buffer*/) -> void {
    std::bit_cast<Buffer*>(data)->busy = false;
}

Buffer::Buffer(towl::Shm* const shm, const size_t width, const size_t height)
    : width(width),
      height(height),
      size(width * height * 4),
      shm_fd(make_shm(size)),
      pool(shm->create_shm_pool(shm_fd.as_handle(), size)),
      buffer(pool.create_buffer(0, width, height, width * 4, WL_SHM_FORMAT_ARGB8888)) {
    data = std::bit_cast<uint8_t*>(mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd.as_handle(), 0));
    ASSERT(data != MAP_FAILED);
    cairo_surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, width, height, width * 4);
    wl_buffer_add_listener(buffer.native(), &listener, this);
}

Buffer::~Buffer() {
    cairo_surface_destroy(cairo_surface);
    munmap(data, size);
}

// bar

Bar::Bar(Window& app, wl_output* const output)
    : app(app),
      output(output),
      logical_height(app.height) {
    surface = app.compositor->create_surface();
    surface.init(this);

    layer_surface = app.layer_shell->create_layer_surface(surface.native(), output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, app.title);
    layer_surface.init(this);

    const auto edge = app.anchor_bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    layer_surface.set_anchor(edge | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    layer_surface.set_size(0, app.height);        // width 0: span the anchored edges
    layer_surface.set_exclusive_zone(app.height); // reserve space so windows don't overlap

    surface.commit();
}

auto Bar::on_wl_surface_preferred_buffer_scale(const int32_t factor) -> void {
    if(factor <= 0 || factor == scale) {
        return;
    }
    scale = factor;
    redraw();
}

auto Bar::on_zwlr_layer_surface_configure(const uint32_t width, const uint32_t height) -> void {
    if(width > 0) {
        logical_width = width;
    }
    if(height > 0) {
        logical_height = height;
    }
    redraw();
}

auto Bar::on_zwlr_layer_surface_closed() -> void {
    closed = true;
}

auto Bar::acquire_buffer() -> Buffer* {
    const auto pixel_width  = logical_width * scale;
    const auto pixel_height = logical_height * scale;
    if(pixel_width == 0 || pixel_height == 0) {
        return nullptr;
    }
    // reuse a free buffer that already has the right size
    for(const auto& buffer : buffers) {
        if(!buffer->busy && buffer->width == pixel_width && buffer->height == pixel_height) {
            return buffer.get();
        }
    }
    // otherwise repurpose a free slot, recreating it at the new size
    for(auto& buffer : buffers) {
        if(!buffer->busy) {
            buffer = std::make_unique<Buffer>(app.shm, pixel_width, pixel_height);
            return buffer.get();
        }
    }
    // every buffer is still held by the compositor; add another
    buffers.push_back(std::make_unique<Buffer>(app.shm, pixel_width, pixel_height));
    return buffers.back().get();
}

auto Bar::on_wl_surface_frame() -> void {
    frame_pending = false;
    if(dirty) {
        redraw();
    }
}

auto Bar::redraw(const bool force) -> void {
    if(frame_pending && !force) {
        dirty = true;
        return;
    }

    const auto buffer = acquire_buffer();
    if(buffer == nullptr) {
        return;
    }

    const auto cairo = cairo_create(buffer->cairo_surface);

    // background
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cairo, app.background.r, app.background.g, app.background.b, app.background.a);
    cairo_paint(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);

    // user contents
    cairo_scale(cairo, scale, scale);
    if(app.on_draw) {
        const auto name = app.output_name(output);
        app.on_draw(cairo, logical_width, logical_height, name);
    }

    cairo_destroy(cairo);
    cairo_surface_flush(buffer->cairo_surface);

    surface.attach(buffer->buffer.native(), 0, 0);
    surface.set_buffer_scale(scale);
    surface.damage(0, 0, buffer->width, buffer->height);
    surface.set_frame();
    surface.commit();
    buffer->busy  = true;
    frame_pending = true;
    dirty         = false;
}

// window

auto Window::add_bar(wl_output* const output) -> void {
    bars.push_back(std::make_unique<Bar>(*this, output));
}

auto Window::bar_for_surface(wl_surface* const surface) -> Bar* {
    for(const auto& bar : bars) {
        if(bar->surface.native() == surface) {
            return bar.get();
        }
    }
    return nullptr;
}

auto Window::output_name(wl_output* const output) -> std::string {
    const auto i = output_names.find(output);
    return i != output_names.end() ? i->second : std::string();
}

auto Window::prune() -> void {
    if(focused != nullptr && focused->closed) {
        focused = nullptr;
    }
    std::erase_if(bars, [](const auto& bar) { return bar->closed; });
}

auto Window::on_wl_output_created(wl_output* const output) -> void {
    outputs.push_back(output);
    if(initialized) {
        add_bar(output);
    }
}

auto Window::on_wl_output_removed(wl_output* const output) -> void {
    std::erase(outputs, output);
    output_names.erase(output);
    for(const auto& bar : bars) {
        if(bar->output == output) {
            bar->closed = true; // pruned after dispatch
        }
    }
}

auto Window::on_wl_output_name(wl_output* const output, const char* const name) -> void {
    output_names[output] = name;
    for(const auto& bar : bars) {
        if(bar->output == output) {
            bar->redraw();
        }
    }
}

auto Window::on_wl_pointer_enter(wl_surface* const surface, const double x, const double y) -> void {
    focused   = bar_for_surface(surface);
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_pointer_leave(wl_surface* const surface) -> void {
    if(focused != nullptr && focused->surface.native() == surface) {
        focused = nullptr;
    }
}

auto Window::on_wl_pointer_motion(const double x, const double y) -> void {
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_pointer_button(const uint32_t button, const uint32_t state) -> void {
    if(state != WL_POINTER_BUTTON_STATE_PRESSED || focused == nullptr || !on_click) {
        return;
    }
    focused->redraw(true); // force redraw to refresh hit-regions
    on_click(pointer_x, pointer_y, button);
}

auto Window::on_wl_pointer_axis(const uint32_t axis, const double value) -> void {
    if(focused == nullptr || !on_scroll) {
        return;
    }
    focused->redraw(true);
    if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        on_scroll(pointer_x, pointer_y, 0, -value);
    } else {
        on_scroll(pointer_x, pointer_y, -value, 0);
    }
}

auto Window::on_wl_touch_down(wl_surface* const surface, const uint32_t id, const double x, const double y) -> void {
    touch_id  = id;
    focused   = bar_for_surface(surface);
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_touch_motion(const uint32_t id, const double x, const double y) -> void {
    if(id != touch_id) {
        return;
    }
    pointer_x = x;
    pointer_y = y;
}

auto Window::on_wl_touch_up(const uint32_t id) -> void {
    if(id != touch_id) {
        return;
    }
    touch_id = -1;
    on_wl_pointer_button(BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
}

auto Window::get_fd() -> int {
    return display.get_fd();
}

auto Window::flush() -> void {
    display.flush();
}

auto Window::dispatch() -> bool {
    const auto ok = display.dispatch();
    prune();
    return ok;
}

auto Window::redraw() -> void {
    for(const auto& bar : bars) {
        bar->redraw();
    }
    display.flush();
}

Window::Window(const size_t height, const bool anchor_bottom, const char* const title)
    : registry(display.get_registry()),
      height(height),
      anchor_bottom(anchor_bottom),
      title(title) {
    registry.set_binders({&compositor_binder, &layer_shell_binder, &shm_binder, &seat_binder, &output_binder});
    display.roundtrip();
    ASSERT(!compositor_binder.interfaces.empty(), "compositor not available");
    ASSERT(!layer_shell_binder.interfaces.empty(), "wlr-layer-shell not available");
    ASSERT(!shm_binder.interfaces.empty(), "shm not available");
    compositor  = std::bit_cast<towl::Compositor*>(compositor_binder.interfaces[0].get());
    layer_shell = std::bit_cast<towl::LayerShell*>(layer_shell_binder.interfaces[0].get());
    shm         = std::bit_cast<towl::Shm*>(shm_binder.interfaces[0].get());

    initialized = true;
    for(const auto output : outputs) {
        add_bar(output);
    }

    display.roundtrip();
}
