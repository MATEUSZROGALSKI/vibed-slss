#pragma once

#include "slc/common/error.h"

#include <cinder/Vector.h>
#include <cinder/app/Window.h>

#include <memory>

namespace slc::app {

/**
 * ImGui debug overlay bound to a Cinder window (doc §4 item 12 — vcpkg
 * `imgui` + a thin Cinder-signal → ImGuiIO binding). Enabled by
 * `--debug-ui` or `advanced.debugUi`; rendered on top of the finished
 * frame at the end of draw().
 *
 * In builds without `SLC_WITH_IMGUI` every method compiles to a no-op so
 * call sites need no #if; `available()` reports which build this is.
 */
class DebugUi {
  public:
    DebugUi();
    ~DebugUi();
    DebugUi(const DebugUi&) = delete;
    DebugUi& operator=(const DebugUi&) = delete;

    /// Whether the binary was built with SLC_WITH_IMGUI.
    [[nodiscard]] static bool available();

    /// Creates the ImGui context and hooks the window's input signals.
    /// Needs a live GL context — call from setup().
    void attach(const ci::app::WindowRef& window);

    /// io.DeltaTime/DisplaySize + backend NewFrame + ImGui::NewFrame().
    void newFrame(float deltaSeconds, const ci::vec2& windowSize,
                  float contentScale);

    /// ImGui::Render + backend draw. Call at the very end of draw().
    void render();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace slc::app
