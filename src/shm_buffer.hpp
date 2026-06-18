#pragma once
#include <cstddef>

#include <cairo/cairo.h>

#include "towl/towl.hpp"
#include "util/fd.hpp"

struct Buffer {
    size_t           width;
    size_t           height;
    size_t           size;
    FileDescriptor   shm_fd;
    towl::ShmPool    pool;
    towl::Buffer     buffer;
    uint8_t*         data;
    cairo_surface_t* cairo_surface;
    bool             busy = false; // compositor holds the buffer

    static auto on_release(void* data, wl_buffer* buffer) -> void;

    static inline wl_buffer_listener listener = {on_release};

    Buffer(towl::Shm* shm, size_t width, size_t height);
    ~Buffer();
};
