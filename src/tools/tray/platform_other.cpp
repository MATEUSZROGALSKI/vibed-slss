// Non-Windows stubs: tray still runs (static wallpaper control + settings
// launcher), but screensaver/auto-pause/live-wallpaper controls stay
// disabled until a native backend lands for the platform.
#include "platform_helpers.h"

namespace slc::tray_plat {

bool onBattery() { return false; }
bool fullscreenAppBusy() { return false; }
QString describeFullscreenCause() { return {}; }
bool sessionLocked() { return false; }

bool screensaverEnabled(const QString&) { return false; }
bool setScreensaverEnabled(const QString&, bool, QString*, const QString&)
{
    return false;
}
void startScreensaverNow(const QString&) {}
void stopActiveScreensaver() {}

bool supportsLiveWallpaper() { return false; }

} // namespace slc::tray_plat
