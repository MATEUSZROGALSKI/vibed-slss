// §6.1.3 fullscreen session — Cinder GLFW window driven by ScreensaverApp.
// Compiled only with SLC_BUILD_CINDER=ON.
#include "session.h"

#include "slc/app/screensaver_app.h"
#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/platform/paths.h"

#if defined(SLC_WAYLAND_WALLPAPER)
#include "wayland/layer_wallpaper.h"
#endif

// Needed for glfwGetX11Window/glfwGetX11Display in --xss reparenting and
// the X11 live-wallpaper desktop window.
#define GLFW_EXPOSE_NATIVE_X11

#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/gl/gl.h>
#include <cinder/app/linux/AppLinux.h>
#include <cinder/app/linux/WindowImplLinux.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// IPlatformHost for the fullscreen Linux session (§5.4 contract).
class LinuxHost final : public slc::app::IPlatformHost {
  public:
    // reportDisplays=false for surfaces that cover only one output (the
    // Wayland layer-shell wallpaper) — an empty list keeps buildCells on
    // the whole-window layout.
    explicit LinuxHost(bool reportDisplays = true)
        : m_reportDisplays(reportDisplays)
    {
    }

    [[nodiscard]] bool isPreviewMode() const noexcept override { return false; }

    void requestExit(int /*code*/) override
    {
        if (auto* app = ci::app::App::get()) {
            app->quit();
        }
    }

    [[nodiscard]] std::vector<slc::platform::DisplayInfo> displays() const override
    {
        if (!m_reportDisplays) {
            return {};
        }
        // Real monitor geometry via GLFW (Xrandr under X11, xdg-output
        // under Wayland). The DRM/sysfs fallback in enumerateDisplays()
        // fabricates a left-to-right layout in connector order that can
        // mismatch the actual desktop arrangement — per-display cells
        // would then land on the wrong monitors.
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (monitors == nullptr || count == 0) {
            return slc::platform::enumerateDisplays();
        }
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        std::vector<slc::platform::DisplayInfo> out;
        out.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode == nullptr) {
                continue;
            }
            slc::platform::DisplayInfo d;
            if (const char* name = glfwGetMonitorName(monitors[i])) {
                d.id = name;
            }
            glfwGetMonitorPos(monitors[i], &d.x, &d.y);
            d.width = mode->width;
            d.height = mode->height;
            float sx = 1.0F, sy = 1.0F;
            glfwGetMonitorContentScale(monitors[i], &sx, &sy);
            d.dpiScale = sx;
            d.primary = monitors[i] == primary;
            out.push_back(std::move(d));
        }
        return out;
    }

  private:
    bool m_reportDisplays;
};

void applySettings(ci::app::AppBase::Settings* settings)
{
    // GLFW fullscreen requires a working Xrandr/EWMH monitor path; a
    // maximized borderless window is equivalent for a screensaver and more
    // robust under XWayland/compositors that reject override-redirect.
    settings->setFullScreen();
    settings->setBorderless();
    settings->setFrameRate(60.0F);
    if (slc::env::isTruthy(slc::env::kTestDisableInputVar)) {
        // Mirrors the Windows debug flag: no top-most/quit-on-input forcing.
        settings->setAlwaysOnTop(false);
    }
}

// --- XScreenSaver hack mode (doc 6.1.3, Path B) ---------------------------

unsigned long g_xssParent = 0;

// GLFW cannot adopt a foreign XID for GL output, so the hack embeds by
// reparenting the GLFW window into $XSCREENSAVER_WINDOW — the same
// technique the reference GLX hacks use, and the only Path-B route that
// keeps the whole ScreensaverApp/render pipeline unmodified.
void adoptXssWindow()
{
    if (g_xssParent == 0) {
        return;
    }
    auto* app = ci::app::App::get();
    if (app == nullptr || app->getNumWindows() == 0) {
        return;
    }
    auto* glfwWin = static_cast<GLFWwindow*>(app->getWindow()->getNative());
    ::Display* dpy = glfwGetX11Display();
    const ::Window xwin = glfwGetX11Window(glfwWin);
    XWindowAttributes attr{};
    if (::XGetWindowAttributes(dpy, g_xssParent, &attr) == 0) {
        SLC_LOG_ERROR("xss: parent XID {:#x} not queryable", g_xssParent);
        return;
    }
    ::XReparentWindow(dpy, xwin, g_xssParent, 0, 0);
    ::XResizeWindow(dpy, xwin, attr.width, attr.height);
    ::XSync(dpy, False);
    SLC_LOG_INFO("xss: reparented into XID {:#x} ({}x{})", g_xssParent,
                 attr.width, attr.height);
}

class XssScreensaverApp final : public slc::app::ScreensaverApp {
  public:
    void setup() override
    {
        // Reparent before the first emitResize so the initial layout pass
        // already sees the hack window's real bounds.
        adoptXssWindow();
        slc::app::ScreensaverApp::setup();
    }
};

void applyXssSettings(ci::app::AppBase::Settings* settings)
{
    // Windowed — the XSS parent dictates placement and size.
    settings->setFullScreen(false);
    settings->setFrameRate(60.0F);
    settings->setAlwaysOnTop(false);
}

// --- live wallpaper (doc 6.1.4) -------------------------------------------

// X11 path: an EWMH "desktop" window covering the virtual screen, held at
// the bottom of the stack and made input-transparent so clicks land on the
// real desktop icons. Covers every X11 DE — including SteamOS desktop mode
// (Plasma X11).
void adoptX11WallpaperWindow()
{
    auto* app = ci::app::App::get();
    if (app == nullptr || app->getNumWindows() == 0) {
        return;
    }
    auto* glfwWin = static_cast<GLFWwindow*>(app->getWindow()->getNative());
    ::Display* dpy = glfwGetX11Display();
    const ::Window xw = glfwGetX11Window(glfwWin);

    const ::Atom wmType = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    const ::Atom typeDesktop =
        XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    const ::Atom state = XInternAtom(dpy, "_NET_WM_STATE", False);
    const ::Atom atoms[] = {
        XInternAtom(dpy, "_NET_WM_STATE_BELOW", False),
        XInternAtom(dpy, "_NET_WM_STATE_STICKY", False),
        XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
        XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False),
    };
    XChangeProperty(dpy, xw, wmType, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&typeDesktop), 1);
    XChangeProperty(dpy, xw, state, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(atoms),
                    static_cast<int>(std::size(atoms)));

    // Geometry: cover the whole virtual screen so multi-monitor X11
    // displays get wallpaper on every output.
    XWindowAttributes root{};
    const ::Window rootWin = DefaultRootWindow(dpy);
    XGetWindowAttributes(dpy, rootWin, &root);
    XMoveResizeWindow(dpy, xw, 0, 0, root.width, root.height);

    // Input-transparent: an empty XShape input region sends every click to
    // the desktop/icons below us.
    XShapeCombineRectangles(dpy, xw, ShapeInput, 0, 0, nullptr, 0, ShapeSet,
                            Unsorted);
    XLowerWindow(dpy, xw);
    XSync(dpy, False);
    SLC_LOG_INFO("x11 wallpaper: desktop window {}x{}", root.width,
                 root.height);
}

class X11WallpaperApp final : public slc::app::ScreensaverApp {
  public:
    void setup() override
    {
        adoptX11WallpaperWindow();
        slc::app::ScreensaverApp::setup();
    }
};

void applyWallpaperSettings(ci::app::AppBase::Settings* settings)
{
    // A fullscreen _NET_WM_STATE_FULLSCREEN request would fight the
    // desktop-type hints; a borderless window resized to the virtual
    // screen is what wallpaper tools use.
    settings->setFullScreen(false);
    settings->setBorderless();
    settings->setFrameRate(60.0F);
    settings->setAlwaysOnTop(false);
}

#if defined(SLC_WAYLAND_WALLPAPER)

// Wayland path: the GLFW window only drives the render loop (hidden —
// possibly an XWayland window on a Wayland session); every finished frame
// is read back and committed to a wlr-layer-shell background surface.
class WaylandWallpaperApp final : public slc::app::ScreensaverApp {
  public:
    void setup() override
    {
        slc::app::ScreensaverApp::setup();
        m_wall = slc::platform::wayland::LayerWallpaper::create();
        if (!m_wall) {
            SLC_LOG_ERROR("wayland wallpaper: layer-shell unavailable; "
                          "use the static wallpaper daemon instead");
            s_wpFailed = true;
            quit();
            return;
        }
        if (auto* app = ci::app::App::get();
            app != nullptr && app->getNumWindows() > 0) {
            glfwHideWindow(
                static_cast<GLFWwindow*>(app->getWindow()->getNative()));
        }
        slc::app::ScreensaverApp::setFrameEndHook([this] { present(); });
    }

    void cleanup() override
    {
        slc::app::ScreensaverApp::setFrameEndHook(nullptr);
        slc::app::ScreensaverApp::cleanup();
        m_wall.reset();
    }

    // Static so it survives past AppLinux::main returning.
    inline static bool s_wpFailed = false;

  private:
    void present()
    {
        if (!m_wall || !m_wall->ready()) {
            return;
        }
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        const int w = vp[2];
        const int h = vp[3];
        if (w <= 0 || h <= 0) {
            return;
        }
        const auto stride = static_cast<std::size_t>(w) * 4;
        m_staging.resize(stride * static_cast<std::size_t>(h));
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                     m_staging.data());
        m_wall->presentFrame(m_staging.data(), w, h);
    }

    std::unique_ptr<slc::platform::wayland::LayerWallpaper> m_wall;
    std::vector<unsigned char> m_staging;
};

// Out-param so the caller can distinguish "compositor lacks layer-shell"
// (return 3 -> tray falls back to the static daemon) from a clean exit.
int runWaylandWallpaper()
{
    // One layer surface on the compositor-chosen output — report no
    // display list so the collage covers the surface as a whole.
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<LinuxHost>(false); });
    slc::app::ScreensaverApp::setIgnoreReducedMotion(true);
    WaylandWallpaperApp::s_wpFailed = false;
    ci::app::RendererRef renderer(
        new ci::app::RendererGl(ci::app::RendererGl::Options().msaa(4)));
    ci::app::AppLinux::main<WaylandWallpaperApp>(
        renderer, "SteamLibraryScreensaver", &applyWallpaperSettings);
    return WaylandWallpaperApp::s_wpFailed ? 3 : 0;
}

#endif // SLC_WAYLAND_WALLPAPER

} // namespace

int runScreensaverSession()
{
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<LinuxHost>(); });
    ci::app::RendererRef renderer(
        new ci::app::RendererGl(ci::app::RendererGl::Options().msaa(4)));
    ci::app::AppLinux::main<slc::app::ScreensaverApp>(
        renderer, "SteamLibraryScreensaver", &applySettings);
    return 0;
}

int runXssSession(unsigned long parentXid)
{
    g_xssParent = parentXid;
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<LinuxHost>(); });
    ci::app::RendererRef renderer(
        new ci::app::RendererGl(ci::app::RendererGl::Options().msaa(4)));
    ci::app::AppLinux::main<XssScreensaverApp>(
        renderer, "SteamLibraryScreensaver", &applyXssSettings);
    return 0;
}

int runWallpaperSession()
{
#if defined(SLC_WAYLAND_WALLPAPER)
    if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
        // A Wayland session can't be papered via X11/XWayland — without
        // layer-shell there is no live-wallpaper path; report failure so
        // the caller falls back to the static wallpaper daemon.
        if (!slc::platform::wayland::LayerWallpaper::supported()) {
            SLC_LOG_ERROR("wayland session without layer-shell support; "
                          "live wallpaper unavailable");
            return 3;
        }
        return runWaylandWallpaper();
    }
#endif
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<LinuxHost>(); });
    slc::app::ScreensaverApp::setIgnoreReducedMotion(true);
    ci::app::RendererRef renderer(
        new ci::app::RendererGl(ci::app::RendererGl::Options().msaa(4)));
    ci::app::AppLinux::main<X11WallpaperApp>(
        renderer, "SteamLibraryScreensaver", &applyWallpaperSettings);
    return 0;
}

void setSessionDebugUi(bool enabled)
{
    slc::app::ScreensaverApp::setDebugUi(enabled);
}
