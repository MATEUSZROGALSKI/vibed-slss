// wlr-layer-shell wallpaper surface (doc 6.1.4 Wayland path).
//
// Opens its own wl_display connection — independent of the Cinder/GLFW
// window, which may be X11/XWayland on a Wayland session — creates a
// background-layer surface anchored to all edges, and presents frames
// through a ring of wl_shm buffers. The render thread hands each drawn
// frame to presentFrame(); a dispatch thread handles protocol events
// (configure, buffer release, closed).
//
// Supported on wlroots compositors (sway, Hyprland, ...) and KDE KWin —
// including SteamOS "Plasma (Wayland)". GNOME does not implement the
// protocol; create() fails there and the caller falls back to the static
// wallpaper daemon.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace slc::platform::wayland {

class LayerWallpaper {
  public:
    LayerWallpaper() = default;
    ~LayerWallpaper();
    LayerWallpaper(const LayerWallpaper&) = delete;
    LayerWallpaper& operator=(const LayerWallpaper&) = delete;

    /// Quick compositor capability check — true when a Wayland display is
    /// reachable and exposes zwlr_layer_shell_v1. Cheap enough to call
    /// before spinning up the GL pipeline.
    [[nodiscard]] static bool supported();

    /// Connects and creates the layer surface. Returns null when Wayland
    /// is unavailable or the compositor lacks zwlr_layer_shell_v1.
    [[nodiscard]] static std::unique_ptr<LayerWallpaper> create();

    /// First configure has arrived; surface has a real size and can take
    /// buffers.
    [[nodiscard]] bool ready() const;
    /// Compositor sent layer_surface.closed.
    [[nodiscard]] bool closed() const;
    [[nodiscard]] std::pair<int, int> surfaceSize() const;

    /// Present one frame of XRGB pixels (w x h, stride w*4). If the shm
    /// size differs the image is centered over black. Drops the frame
    /// silently when every buffer is still held by the compositor.
    void presentFrame(const void* pixels, int w, int h);

    // Opaque state — public only so the C event callbacks can name it.
    struct Impl;

  private:
    bool init();
    bool makeBuffers(int w, int h);
    void freeBuffers();
    void dispatchLoop();

    std::unique_ptr<Impl> m_impl;
};

} // namespace slc::platform::wayland
