#include <bit>
#include <format>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "macros/assert.hpp"
#include "shm-buffer.hpp"

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
