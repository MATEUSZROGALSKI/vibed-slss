// Thin Cinder→ImGui binding (doc §4 item 12): imgui core + the official
// OpenGL3 backend from vcpkg; platform input reaches ImGui through Cinder's
// window signals so this file works on every OS Cinder supports.

#include "slc/app/debug_ui.h"

#include "slc/common/logging.h"

#if defined(SLC_WITH_IMGUI)

#include <cinder/app/KeyEvent.h>
#include <cinder/app/MouseEvent.h>
#include <cinder/app/Window.h>
#include <cinder/Signals.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

namespace slc::app {

namespace {

// Cinder KEY_* codes are cross-platform normalized ASCII-ish values
// (KeyEvent.h); ImGui needs its own key enum for navigation.
ImGuiKey mapCinderKey(int code)
{
    using K = ci::app::KeyEvent;
    switch (code) {
    case K::KEY_BACKSPACE: return ImGuiKey_Backspace;
    case K::KEY_TAB: return ImGuiKey_Tab;
    case K::KEY_RETURN: return ImGuiKey_Enter;
    case K::KEY_ESCAPE: return ImGuiKey_Escape;
    case K::KEY_DELETE: return ImGuiKey_Delete;
    case K::KEY_LEFT: return ImGuiKey_LeftArrow;
    case K::KEY_RIGHT: return ImGuiKey_RightArrow;
    case K::KEY_UP: return ImGuiKey_UpArrow;
    case K::KEY_DOWN: return ImGuiKey_DownArrow;
    case K::KEY_HOME: return ImGuiKey_Home;
    case K::KEY_END: return ImGuiKey_End;
    case K::KEY_PAGEUP: return ImGuiKey_PageUp;
    case K::KEY_PAGEDOWN: return ImGuiKey_PageDown;
    case K::KEY_LSHIFT:
    case K::KEY_RSHIFT: return ImGuiMod_Shift;
    case K::KEY_LCTRL:
    case K::KEY_RCTRL: return ImGuiMod_Ctrl;
    case K::KEY_LALT:
    case K::KEY_RALT: return ImGuiMod_Alt;
    default: break;
    }
    if (code >= 'a' && code <= 'z') {
        return static_cast<ImGuiKey>(ImGuiKey_A + (code - 'a'));
    }
    if (code >= '0' && code <= '9') {
        return static_cast<ImGuiKey>(ImGuiKey_0 + (code - '0'));
    }
    return ImGuiKey_None;
}

int mouseButtonIndex(const ci::app::MouseEvent& event)
{
    if (event.isLeft()) {
        return 0;
    }
    if (event.isRight()) {
        return 1;
    }
    return event.isMiddle() ? 2 : -1;
}

} // namespace

struct DebugUi::Impl {
    ci::signals::ConnectionList conns;

    void attachSignals(const ci::app::WindowRef& window)
    {
        ImGuiIO& io = ImGui::GetIO();
        conns += window->getSignalMouseMove().connect(
            [&io](ci::app::MouseEvent& e) {
                io.AddMousePosEvent(static_cast<float>(e.getX()),
                                    static_cast<float>(e.getY()));
            });
        conns += window->getSignalMouseDrag().connect(
            [&io](ci::app::MouseEvent& e) {
                io.AddMousePosEvent(static_cast<float>(e.getX()),
                                    static_cast<float>(e.getY()));
            });
        const auto button = [&io](ci::app::MouseEvent& e, bool down) {
            const int idx = mouseButtonIndex(e);
            if (idx >= 0) {
                io.AddMouseButtonEvent(idx, down);
            }
        };
        conns += window->getSignalMouseDown().connect(
            [button](ci::app::MouseEvent& e) { button(e, true); });
        conns += window->getSignalMouseUp().connect(
            [button](ci::app::MouseEvent& e) { button(e, false); });
        conns += window->getSignalMouseWheel().connect(
            [&io](ci::app::MouseEvent& e) {
                io.AddMouseWheelEvent(0.0F, e.getWheelIncrement());
            });
        const auto key = [&io](ci::app::KeyEvent& e, bool down) {
            if (const ImGuiKey k = mapCinderKey(e.getCode());
                k != ImGuiKey_None) {
                io.AddKeyEvent(k, down);
            }
            io.AddKeyEvent(ImGuiMod_Shift, e.isShiftDown());
            io.AddKeyEvent(ImGuiMod_Ctrl, e.isControlDown());
            io.AddKeyEvent(ImGuiMod_Alt, e.isAltDown());
            if (down && e.getCharUtf32() != 0) {
                io.AddInputCharacter(e.getCharUtf32());
            }
        };
        conns += window->getSignalKeyDown().connect(
            [key](ci::app::KeyEvent& e) { key(e, true); });
        conns += window->getSignalKeyUp().connect(
            [key](ci::app::KeyEvent& e) { key(e, false); });
    }
};

DebugUi::DebugUi() = default;

DebugUi::~DebugUi()
{
    if (m_impl) {
        // Caller must destroy while the GL context is still current
        // (ScreensaverApp::cleanup runs before context teardown).
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }
}

bool DebugUi::available()
{
    return true;
}

void DebugUi::attach(const ci::app::WindowRef& window)
{
    IMGUI_CHECKVERSION();
    m_impl = std::make_unique<Impl>();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    // Cinder requests a GL 3.3 core context → GLSL 330.
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        SLC_LOG_ERROR("debug-ui: ImGui OpenGL3 backend init failed");
        m_impl.reset();
        return;
    }
    m_impl->attachSignals(window);
    SLC_LOG_INFO("debug-ui: ImGui overlay active");
}

void DebugUi::newFrame(float deltaSeconds, const ci::vec2& windowSize,
                       float contentScale)
{
    if (!m_impl) {
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = deltaSeconds > 0.0F ? deltaSeconds : 1.0F / 60.0F;
    io.DisplaySize = ImVec2(windowSize.x, windowSize.y);
    io.DisplayFramebufferScale =
        ImVec2(contentScale, contentScale);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void DebugUi::render()
{
    if (!m_impl) {
        return;
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace slc::app

#else // !SLC_WITH_IMGUI — compiled stubs so call sites stay #if-free.

namespace slc::app {

struct DebugUi::Impl {};

DebugUi::DebugUi() = default;
DebugUi::~DebugUi() = default;

bool DebugUi::available()
{
    return false;
}

void DebugUi::attach(const ci::app::WindowRef&) {}
void DebugUi::newFrame(float, const ci::vec2&, float) {}
void DebugUi::render() {}

} // namespace slc::app

#endif // SLC_WITH_IMGUI
