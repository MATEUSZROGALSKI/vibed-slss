// Non-Windows placeholder: §6.1.4 backends for GNOME/KDE/sway and macOS
// land with the respective hosts; until then the daemon reports the
// platform as unsupported rather than silently doing nothing.
#include "slc/wallpaper/backend.h"

namespace slc::wallpaper {

std::unique_ptr<IWallpaperBackend> makeWallpaperBackend()
{
    return nullptr;
}

} // namespace slc::wallpaper
