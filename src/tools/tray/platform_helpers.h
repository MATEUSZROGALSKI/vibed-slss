#pragma once

#include <QString>

// OS-specific bits for slc_tray. Windows implementation in
// platform_win.cpp; a no-op fallback in platform_other.cpp keeps the tray
// usable (screensaver controls disabled, no auto-pause) elsewhere until a
// native backend lands.
namespace slc::tray_plat {

/// Laptop on battery (auto-pause trigger). false when unknown/desktop.
[[nodiscard]] bool onBattery();
/// An exclusive-fullscreen app is focused (game/video → auto-pause).
[[nodiscard]] bool fullscreenAppBusy();
/// Diagnostic: raw notification state + the foreground window's exe, for
/// identifying what actually triggers fullscreenAppBusy().
[[nodiscard]] QString describeFullscreenCause();
/// Session is locked / not on the default input desktop.
[[nodiscard]] bool sessionLocked();

/// Whether our .scr is registered as the system screensaver.
[[nodiscard]] bool screensaverEnabled(const QString& scrPath);
/// Register (or unregister) our .scr; returns false on failure. When
/// disabling, restores whatever saver was registered before we claimed it.
bool setScreensaverEnabled(const QString& scrPath, bool enable,
                           QString* previousOut = nullptr,
                           const QString& previousIn = {});

/// Launch the screensaver now (independent of registration).
void startScreensaverNow(const QString& exePath);
/// Close a running screensaver session, if any.
void stopActiveScreensaver();

/// Tray has a live-wallpaper mode to control (false -> menu item disabled).
[[nodiscard]] bool supportsLiveWallpaper();

} // namespace slc::tray_plat
