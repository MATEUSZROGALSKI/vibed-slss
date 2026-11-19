#include "layer_wallpaper.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "slc/common/logging.h"

#include <wayland-client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace slc::platform::wayland {
namespace {

constexpr int kBufferCount = 3;
constexpr std::uint32_t kShmFormat = WL_SHM_FORMAT_XRGB8888;

int memfd()
{
    return static_cast<int>(
        ::syscall(SYS_memfd_create, "slc-wallpaper", MFD_CLOEXEC));
}

} // namespace

struct LayerWallpaper::Impl {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    zwlr_layer_shell_v1* layerShell = nullptr;
    wl_surface* surface = nullptr;
    zwlr_layer_surface_v1* layerSurface = nullptr;

    wl_shm_pool* pool = nullptr;
    int poolFd = -1;
    void* poolData = nullptr;
    std::size_t poolSize = 0;

    struct Buffer {
        wl_buffer* buf = nullptr;
        std::uint8_t* data = nullptr;
        std::atomic<bool> busy{false};
    };
    std::array<Buffer, kBufferCount> buffers;
    int bufW = 0;
    int bufH = 0;
    int stride = 0;

    std::atomic<int> cfgW{0};
    std::atomic<int> cfgH{0};
    std::atomic<bool> configured{false};
    std::atomic<bool> closed{false};
    std::atomic<bool> stop{false};
    std::thread dispatchThread;
};

namespace {

void onRegistryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                      const char* iface, std::uint32_t /*version*/)
{
    auto* impl = static_cast<LayerWallpaper::Impl*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        impl->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        impl->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        impl->layerShell = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
                             1));
    }
}

void onRegistryRemove(void*, wl_registry*, std::uint32_t) {}

const wl_registry_listener kRegistryListener = {onRegistryGlobal,
                                                onRegistryRemove};

void onLayerConfigure(void* data, zwlr_layer_surface_v1* surface,
                      std::uint32_t serial, std::uint32_t w, std::uint32_t h)
{
    auto* impl = static_cast<LayerWallpaper::Impl*>(data);
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    impl->cfgW = static_cast<int>(w);
    impl->cfgH = static_cast<int>(h);
    impl->configured = true;
}

void onLayerClosed(void* data, zwlr_layer_surface_v1*)
{
    static_cast<LayerWallpaper::Impl*>(data)->closed = true;
}

const zwlr_layer_surface_v1_listener kLayerListener = {onLayerConfigure,
                                                     onLayerClosed};

void onBufferRelease(void* data, wl_buffer*)
{
    *static_cast<std::atomic<bool>*>(data) = false;
}

const wl_buffer_listener kBufferListener = {onBufferRelease};

} // namespace

LayerWallpaper::~LayerWallpaper()
{
    if (!m_impl) {
        return;
    }
    auto& i = *m_impl;
    i.stop = true;
    if (i.display != nullptr) {
        wl_display_cancel_read(i.display);
    }
    if (i.dispatchThread.joinable()) {
        i.dispatchThread.join();
    }
    freeBuffers();
    if (i.layerSurface != nullptr) {
        zwlr_layer_surface_v1_destroy(i.layerSurface);
    }
    if (i.surface != nullptr) {
        wl_surface_destroy(i.surface);
    }
    if (i.registry != nullptr) {
        wl_registry_destroy(i.registry);
    }
    if (i.display != nullptr) {
        wl_display_disconnect(i.display);
    }
}

std::unique_ptr<LayerWallpaper> LayerWallpaper::create()
{
    auto wp = std::make_unique<LayerWallpaper>();
    wp->m_impl = std::make_unique<Impl>();
    if (!wp->init()) {
        return nullptr;
    }
    return wp;
}

bool LayerWallpaper::supported()
{
    wl_display* display = wl_display_connect(nullptr);
    if (display == nullptr) {
        return false;
    }
    Impl i;
    i.display = display;
    wl_registry* registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &kRegistryListener, &i);
    wl_display_roundtrip(display);
    const bool ok = i.layerShell != nullptr;
    if (i.layerShell != nullptr) {
        zwlr_layer_shell_v1_destroy(i.layerShell);
    }
    if (i.compositor != nullptr) {
        wl_compositor_destroy(i.compositor);
    }
    if (i.shm != nullptr) {
        wl_shm_destroy(i.shm);
    }
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return ok;
}

bool LayerWallpaper::init()
{
    auto& i = *m_impl;
    i.display = wl_display_connect(nullptr);
    if (i.display == nullptr) {
        SLC_LOG_WARN("wayland wallpaper: wl_display_connect failed");
        return false;
    }
    i.registry = wl_display_get_registry(i.display);
    wl_registry_add_listener(i.registry, &kRegistryListener, &i);
    wl_display_roundtrip(i.display);

    if (i.compositor == nullptr || i.shm == nullptr ||
        i.layerShell == nullptr) {
        SLC_LOG_WARN(
            "wayland wallpaper: missing globals (compositor={}, shm={}, "
            "layer_shell={})",
            i.compositor != nullptr, i.shm != nullptr,
            i.layerShell != nullptr);
        return false;
    }

    i.surface = wl_compositor_create_surface(i.compositor);
    i.layerSurface = zwlr_layer_shell_v1_get_layer_surface(
        i.layerShell, i.surface, nullptr,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "steam-library-screensaver");
    zwlr_layer_surface_v1_add_listener(i.layerSurface, &kLayerListener, &i);
    zwlr_layer_surface_v1_set_anchor(
        i.layerSurface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_size(i.layerSurface, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(i.layerSurface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        i.layerSurface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(i.surface);

    // The roundtrip dispatches the initial configure on this thread.
    wl_display_roundtrip(i.display);
    if (!i.configured || i.cfgW <= 0 || i.cfgH <= 0) {
        SLC_LOG_WARN("wayland wallpaper: no initial configure");
        return false;
    }
    SLC_LOG_INFO("wayland wallpaper: layer surface {}x{}", i.cfgW.load(),
                 i.cfgH.load());
    if (!makeBuffers(i.cfgW, i.cfgH)) {
        return false;
    }

    i.dispatchThread = std::thread([this] { dispatchLoop(); });
    return true;
}

bool LayerWallpaper::makeBuffers(int w, int h)
{
    auto& i = *m_impl;
    freeBuffers();
    i.stride = w * 4;
    i.poolSize = static_cast<std::size_t>(i.stride) * h * kBufferCount;
    i.poolFd = memfd();
    if (i.poolFd < 0 || ::ftruncate(i.poolFd, static_cast<off_t>(i.poolSize)) != 0) {
        SLC_LOG_ERROR("wayland wallpaper: memfd failed");
        return false;
    }
    i.poolData = ::mmap(nullptr, i.poolSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, i.poolFd, 0);
    if (i.poolData == MAP_FAILED) {
        i.poolData = nullptr;
        SLC_LOG_ERROR("wayland wallpaper: mmap failed");
        return false;
    }
    i.pool = wl_shm_create_pool(i.shm, i.poolFd,
                              static_cast<int>(i.poolSize));
    for (int b = 0; b < kBufferCount; ++b) {
        const auto off = static_cast<std::uint32_t>(
            static_cast<std::size_t>(b) * i.stride * h);
        auto& buf = i.buffers[b];
        buf.buf = wl_shm_pool_create_buffer(i.pool, static_cast<int>(off), w,
                                            h, i.stride, kShmFormat);
        buf.data = static_cast<std::uint8_t*>(i.poolData) + off;
        buf.busy = false;
        wl_buffer_add_listener(buf.buf, &kBufferListener, &buf.busy);
    }
    i.bufW = w;
    i.bufH = h;
    return true;
}

void LayerWallpaper::freeBuffers()
{
    auto& i = *m_impl;
    for (auto& b : i.buffers) {
        if (b.buf != nullptr) {
            wl_buffer_destroy(b.buf);
            b.buf = nullptr;
        }
        b.data = nullptr;
        b.busy = false;
    }
    if (i.pool != nullptr) {
        wl_shm_pool_destroy(i.pool);
        i.pool = nullptr;
    }
    if (i.poolData != nullptr) {
        ::munmap(i.poolData, i.poolSize);
        i.poolData = nullptr;
    }
    if (i.poolFd >= 0) {
        ::close(i.poolFd);
        i.poolFd = -1;
    }
}

void LayerWallpaper::dispatchLoop()
{
    auto& i = *m_impl;
    while (!i.stop) {
        if (wl_display_dispatch(i.display) < 0) {
            break;
        }
    }
}

bool LayerWallpaper::ready() const
{
    return m_impl && m_impl->configured && !m_impl->buffers.empty() &&
           !m_impl->closed;
}

bool LayerWallpaper::closed() const
{
    return m_impl && m_impl->closed;
}

std::pair<int, int> LayerWallpaper::surfaceSize() const
{
    if (!m_impl) {
        return {0, 0};
    }
    return {m_impl->cfgW.load(), m_impl->cfgH.load()};
}

void LayerWallpaper::presentFrame(const void* pixels, int w, int h)
{
    auto& i = *m_impl;
    if (!ready()) {
        return;
    }
    // Reconfigured to a different size -> rebuild the pool.
    if (i.cfgW != i.bufW || i.cfgH != i.bufH) {
        if (!makeBuffers(i.cfgW, i.cfgH)) {
            return;
        }
    }
    Impl::Buffer* buf = nullptr;
    for (auto& b : i.buffers) {
        if (!b.busy) {
            buf = &b;
            break;
        }
    }
    if (buf == nullptr) {
        return; // compositor holds every buffer; drop the frame
    }

    // GL readback is bottom-up; wl_shm is top-down — flip while copying.
    // Mismatched geometry (odd configure vs framebuffer sizes) centers
    // the image on black.
    std::memset(buf->data, 0, static_cast<std::size_t>(i.stride) * i.bufH);
    const int rows = std::min(h, i.bufH);
    const int cols = std::min(w, i.bufW) * 4;
    const int xOff = (i.bufW * 4 - cols) / 2;
    const int yOff = std::max(0, (i.bufH - rows) / 2);
    const auto* src = static_cast<const std::uint8_t*>(pixels);
    for (int y = 0; y < rows; ++y) {
        const auto* row = src + static_cast<std::size_t>(h - 1 - y) * w * 4;
        std::memcpy(buf->data + static_cast<std::size_t>(yOff + y) * i.stride +
                        xOff,
                    row, static_cast<std::size_t>(cols));
    }

    buf->busy = true;
    wl_surface_attach(i.surface, buf->buf, 0, 0);
    wl_surface_damage_buffer(i.surface, 0, 0, i.bufW, i.bufH);
    wl_surface_commit(i.surface);
    // The dispatch thread only flushes around its poll wait — flush the
    // commit now so the compositor sees it this frame.
    wl_display_flush(i.display);
}

} // namespace slc::platform::wayland
