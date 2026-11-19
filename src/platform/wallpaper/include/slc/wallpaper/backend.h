#pragma once

#include "slc/common/error.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace slc::wallpaper {

/// §6.1.4 per-platform wallpaper setter/getter. Implementations live in the
/// platform layer; core never sees COM/DBus/AppKit.
class IWallpaperBackend {
  public:
    virtual ~IWallpaperBackend() = default;
    /// Set `image` (absolute path) on all monitors.
    virtual Result<void> setWallpaper(const std::filesystem::path& image) = 0;
    /// Current wallpaper path for restore-on-exit; nullopt where the
    /// platform can't report it.
    [[nodiscard]] virtual Result<std::filesystem::path> currentWallpaper()
        const = 0;
};

/// Per-OS backend; nullptr where unsupported.
[[nodiscard]] std::unique_ptr<IWallpaperBackend> makeWallpaperBackend();

} // namespace slc::wallpaper
