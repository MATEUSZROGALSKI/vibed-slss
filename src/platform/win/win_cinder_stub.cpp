// No-Cinder fallback: proves the discovery pipeline instead of rendering.
// Used only when SLC_BUILD_CINDER=OFF (dev/CI sanity builds).

#include "win_session.h"

#include "slc/common/logging.h"

int runScreensaverSession(bool /*preview*/, HWND /*previewParent*/)
{
    SLC_LOG_WARN("SLC_BUILD_CINDER=OFF; running discovery diagnostic instead "
                 "of opening the screensaver window");
    return diagnoseArtwork();
}

int runWallpaperSession()
{
    SLC_LOG_WARN("SLC_BUILD_CINDER=OFF; live wallpaper needs the Cinder "
                 "session");
    return 1;
}
