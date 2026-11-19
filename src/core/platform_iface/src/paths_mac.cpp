#include "slc/platform/paths.h"
#include "slc/platform/media.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

#include <cstdlib>
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

} // namespace

std::filesystem::path configDir()
{
    return homeDir() / "Library/Application Support/SteamLibraryScreensaver";
}

std::filesystem::path logDir()
{
    return homeDir() / "Library/Logs/SteamLibraryScreensaver";
}

std::filesystem::path cacheDir()
{
    return homeDir() / "Library/Caches/SteamLibraryScreensaver";
}

std::optional<std::filesystem::path> detectSteamRoot()
{
    const std::filesystem::path candidate =
        homeDir() / "Library/Application Support/Steam";
    if (std::filesystem::exists(candidate / "steamapps")) {
        return candidate;
    }
    return std::nullopt;
}

bool prefersReducedMotion()
{
    // NSAccessibilityReduceMotionEnabled (AppKit) reads this same backing
    // store; CFPreferences keeps this TU free of the Obj-C runtime.
    const CFTypeRef value = CFPreferencesCopyAppValue(
        CFSTR("ReduceMotionEnabled"), CFSTR("com.apple.Accessibility"));
    if (value == nullptr) {
        return false;
    }
    const bool reduce = CFGetTypeID(value) == CFBooleanGetTypeID() &&
                        CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    CFRelease(value);
    return reduce;
}

std::vector<DisplayInfo> enumerateDisplays()
{
    std::uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess ||
        count == 0) {
        return {};
    }
    std::vector<CGDirectDisplayID> ids(count);
    if (CGGetActiveDisplayList(count, ids.data(), &count) !=
        kCGErrorSuccess) {
        return {};
    }

    std::vector<DisplayInfo> displays;
    displays.reserve(count);
    for (const CGDirectDisplayID id : ids) {
        const CGRect bounds = CGDisplayBounds(id);
        DisplayInfo display;
        display.id = std::to_string(id);
        display.x = static_cast<int>(bounds.origin.x);
        display.y = static_cast<int>(bounds.origin.y);
        display.width = static_cast<int>(bounds.size.width);
        display.height = static_cast<int>(bounds.size.height);
        display.primary = CGDisplayIsMain(id) == kCGTrue;
        // Retina backing scale: pixel count vs point width of the mode.
        if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(id)) {
            const std::size_t pixelWidth = CGDisplayModeGetPixelWidth(mode);
            const std::size_t pointWidth = CGDisplayModeGetWidth(mode);
            if (pointWidth != 0) {
                display.dpiScale =
                    static_cast<float>(pixelWidth) /
                    static_cast<float>(pointWidth);
            }
            CGDisplayModeRelease(mode);
        }
        displays.push_back(display);
    }
    return displays;
}

void initImageDecodeThread() {}

} // namespace slc::platform
