#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace slc::platform {

// Per-OS implementation is selected by CMake (paths_{win,mac,linux}.cpp), which
// keeps core/ free of platform #ifdefs (doc goal G6).

struct DisplayInfo {
    std::string id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float dpiScale = 1.0F;
    bool primary = false;
};

/** %APPDATA%/SteamLibraryScreensaver, ~/.config/steam-library-screensaver, etc. */
[[nodiscard]] std::filesystem::path configDir();

/** Log directory (doc section 6.4). */
[[nodiscard]] std::filesystem::path logDir();

/** Base directory for the artwork/thumbnail cache. */
[[nodiscard]] std::filesystem::path cacheDir();

/** Default Steam install location, e.g. from registry (Windows) or well-known
 * paths (macOS/Linux). Returns nullopt when Steam is not detected. */
[[nodiscard]] std::optional<std::filesystem::path> detectSteamRoot();

/** OS-level reduced-motion setting (doc risk R7). */
[[nodiscard]] bool prefersReducedMotion();

/** Connected displays with DPI info (multi-monitor composition, M3). */
[[nodiscard]] std::vector<DisplayInfo> enumerateDisplays();

} // namespace slc::platform
