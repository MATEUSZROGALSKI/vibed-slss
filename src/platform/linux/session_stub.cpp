// No-Cinder fallback for --run: render path unavailable without
// SLC_BUILD_CINDER=ON; run the discovery diagnostic instead (mirrors the
// Windows stub).
#include "session.h"

#include "slc/common/logging.h"

#include <cstdio>

int runScreensaverSession()
{
    SLC_LOG_WARN("runScreensaverSession: built without SLC_BUILD_CINDER; "
                 "running discovery diagnostic instead");
    std::fputs("GL renderer unavailable in this build "
               "(SLC_BUILD_CINDER=OFF); discovery output follows.\n",
               stderr);
    return diagnoseArtwork();
}

int runXssSession(unsigned long /*parentXid*/)
{
    SLC_LOG_ERROR("xss mode requires the GL build (SLC_BUILD_CINDER=ON)");
    std::fputs("xscreensaver embedding unavailable in this build "
               "(SLC_BUILD_CINDER=OFF)\n",
               stderr);
    return 3;
}

int runWallpaperSession()
{
    SLC_LOG_ERROR("wallpaper mode requires the GL build "
                  "(SLC_BUILD_CINDER=ON)");
    std::fputs("live wallpaper unavailable in this build "
               "(SLC_BUILD_CINDER=OFF)\n",
               stderr);
    return 3;
}

void setSessionDebugUi(bool /*enabled*/) {}
