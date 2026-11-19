#include "slc/platform/paths.h"
#include "slc/platform/media.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace slc::platform {
namespace {

std::filesystem::path homeDir()
{
    if (const char* home = std::getenv("HOME")) {
        return home;
    }
    return "/tmp";
}

std::filesystem::path xdgDir(const char* var, const char* fallback)
{
    if (const char* value = std::getenv(var); value != nullptr && *value != '\0') {
        return value;
    }
    return homeDir() / fallback;
}

// Runs a fixed command and returns its stdout. Only used with literal argument
// lists we fully control — never with user input.
std::string runCapture(const char* cmd)
{
    std::string out;
    if (FILE* pipe = ::popen(cmd, "r")) {
        char buf[256];
        while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
            out += buf;
        }
        if (::pclose(pipe) != 0) {
            return {};
        }
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::string lowerOf(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool kdeAnimationsDisabled()
{
    // kdeglobals: [KDE] AnimationSpeedFactor=0 disables animation.
    const std::filesystem::path file = xdgDir("XDG_CONFIG_HOME", ".config") / "kdeglobals";
    std::ifstream in(file);
    if (!in) {
        return false;
    }
    std::string line;
    bool inKdeGroup = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.front() == '[') {
            inKdeGroup = (line == "[KDE]");
            continue;
        }
        if (!inKdeGroup) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (line.substr(0, eq) == "AnimationSpeedFactor" &&
            line.substr(eq + 1) == "0") {
            return true;
        }
    }
    return false;
}

} // namespace

std::filesystem::path configDir()
{
    return xdgDir("XDG_CONFIG_HOME", ".config") / "steam-library-screensaver";
}

std::filesystem::path logDir()
{
    return xdgDir("XDG_STATE_HOME", ".local/state") / "steam-library-screensaver";
}

std::filesystem::path cacheDir()
{
    return xdgDir("XDG_CACHE_HOME", ".cache") / "steam-library-screensaver";
}

std::optional<std::filesystem::path> detectSteamRoot()
{
    const std::filesystem::path candidates[] = {
        homeDir() / ".steam/steam",
        homeDir() / ".local/share/Steam",
        homeDir() / ".steam/root",
        homeDir() / ".var/app/com.valvesoftware.Steam/.local/share/Steam",
        homeDir() / "snap/steam/common/.local/share/Steam",
    };
    for (const auto& path : candidates) {
        if (std::filesystem::exists(path / "steamapps")) {
            return path;
        }
    }
    return std::nullopt;
}

bool prefersReducedMotion()
{
    const std::string desktop = lowerOf([] {
        const char* v = std::getenv("XDG_CURRENT_DESKTOP");
        return v != nullptr ? v : "";
    }());
    if (desktop.find("gnome") != std::string::npos ||
        desktop.find("unity") != std::string::npos ||
        desktop.find("pantheon") != std::string::npos ||
        desktop.find("cinnamon") != std::string::npos ||
        desktop.find("xfce") != std::string::npos) {
        // enable-animations == false means the user asked for reduced motion.
        const std::string out = runCapture(
            "gsettings get org.gnome.desktop.interface enable-animations 2>/dev/null");
        if (!out.empty()) {
            return out.find("false") != std::string::npos;
        }
    }
    if (desktop.find("kde") != std::string::npos ||
        desktop.find("plasma") != std::string::npos) {
        return kdeAnimationsDisabled();
    }
    return false;
}

std::vector<DisplayInfo> enumerateDisplays()
{
    // Headless-safe fallback: kernel DRM connector modes give real native
    // resolutions without X11/Wayland headers. Position order follows
    // connector enumeration; the Qt host refines this via QScreen at runtime.
    std::vector<DisplayInfo> displays;
    const std::filesystem::path drm = "/sys/class/drm";
    std::error_code ec;
    if (!std::filesystem::is_directory(drm, ec)) {
        return displays;
    }
    int xOffset = 0;
    for (const auto& entry : std::filesystem::directory_iterator(drm, ec)) {
        const std::string name = entry.path().filename().string();
        // Connector dirs look like card0-HDMI-A-1 / card1-DP-2; skip cardN.
        if (name.find('-') == std::string::npos) {
            continue;
        }
        std::ifstream statusFile(entry.path() / "status");
        std::string status;
        if (!statusFile || !(statusFile >> status) || status != "connected") {
            continue;
        }
        std::ifstream modesFile(entry.path() / "modes");
        std::string mode;
        if (!modesFile || !(modesFile >> mode)) {
            continue; // connected but no modes — panel not ready
        }
        const auto sep = mode.find('x');
        if (sep == std::string::npos) {
            continue;
        }
        DisplayInfo d;
        d.id = name;
        try {
            d.width = std::stoi(mode.substr(0, sep));
            d.height = std::stoi(mode.substr(sep + 1));
        } catch (...) {
            continue;
        }
        d.x = xOffset;
        d.y = 0;
        d.primary = displays.empty();
        xOffset += d.width;
        displays.push_back(std::move(d));
    }
    return displays;
}

void initImageDecodeThread() {}

} // namespace slc::platform
