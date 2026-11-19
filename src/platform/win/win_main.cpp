// Windows .scr host (doc section 6.1.1).
//
// Argument contract:
//   (none)/ /s  -> fullscreen run
//   /c[:HWND]   -> launch Qt configurator (slc_config, M5 tooling)
//   /p <HWND>   -> render into the preview child window, 15 fps cap
//   /a          -> legacy password dialog; ignore, exit 0
//   /0,/1,...   -> legacy monitor index, treated as /s
//   --list-artwork -> dev diagnostic: run discovery, log + print results
//
// With SLC_BUILD_CINDER=ON, run/preview open a real GL window hosting
// slc::app::ScreensaverApp (win_cinder.cpp); without it the stub in
// win_cinder_stub.cpp runs the discovery diagnostic instead.

#include "win_session.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/config/app_config.h"
#include "slc/content/content_service.h"
#include "slc/content/steam_local_provider.h"
#include "slc/platform/paths.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr wchar_t kInstanceMutex[] = L"Global\\SteamLibraryScreensaver";

enum class Mode {
    Run,
    Configure,
    Preview,
    ChangePassword,
    ListArtwork,
    Wallpaper,
    Help
};

struct Args {
    Mode mode = Mode::Run;
    HWND previewParent = nullptr;
};

Args parseArgs(const std::wstring& cmdline)
{
    Args out;
    // lpCmdLine already excludes argv[0]; tokenise on whitespace.
    // scr args arrive as "/s", "/c:1234", "/p 5678".
    std::wstring token;
    std::wstringstream ss(cmdline);
    while (ss >> token) {
        std::wstring_view t = token;
        if (t.size() >= 2 && t[0] == L'/') {
            const wchar_t flag = static_cast<wchar_t>(towlower(t[1]));
            switch (flag) {
            case L's': out.mode = Mode::Run; break;
            case L'c': out.mode = Mode::Configure; break;
            case L'a': out.mode = Mode::ChangePassword; break;
            case L'p': {
                out.mode = Mode::Preview;
                std::wstring hwndStr;
                if (t.size() > 3 && t[2] == L':') {
                    hwndStr = t.substr(3);
                } else if (ss >> hwndStr) {
                }
                out.previewParent = reinterpret_cast<HWND>(
                    static_cast<uintptr_t>(wcstoull(hwndStr.c_str(), nullptr, 10)));
                break;
            }
            default:
                if (flag >= L'0' && flag <= L'9') {
                    out.mode = Mode::Run;
                }
                break;
            }
        } else if (t == L"--list-artwork") {
            out.mode = Mode::ListArtwork;
        } else if (t == L"--wallpaper") {
            out.mode = Mode::Wallpaper;
        } else if (t == L"--help" || t == L"-h") {
            out.mode = Mode::Help;
        }
    }
    return out;
}

} // namespace

int diagnoseArtwork()
{
    // Console diagnostic; also emits to the file log.
    slc::content::ContentService service;
    service.addProvider(std::make_unique<slc::content::SteamLocalProvider>());
    slc::content::DiscoveryContext ctx;

    auto cfg = slc::config::loadConfig(slc::config::defaultConfigPath());
    ctx.steamRootOverride = cfg.content.steamRoot;
    ctx.excludeAppIds = cfg.content.excludeAppIds;
    for (const auto& kind : cfg.content.includeKinds) {
        if (auto k = slc::content::artworkKindFromString(kind)) {
            ctx.includeKinds.insert(*k);
        }
    }
    ctx.minImageWidth = cfg.content.minImageWidth;
    ctx.installedOnly = cfg.content.installedOnly;

    auto result = service.collect(ctx);
    if (!result) {
        std::fprintf(stderr, "discovery failed: %s\n", result.error().message.c_str());
        return 1;
    }
    for (const auto& a : *result) {
        std::printf("%-8s %-7s %-40s %s\n", a.appId.c_str(),
                    std::string(slc::content::toString(a.kind)).c_str(),
                    a.gameName.c_str(), a.path.filename().string().c_str());
    }
    std::printf("%zu artworks\n", result->size());
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdline, int)
{
    slc::logging::initFileLogging(slc::platform::logDir());
    const Args args = parseArgs(cmdline ? cmdline : L"");
    SLC_LOG_INFO("slc_host_win start, mode={}", static_cast<int>(args.mode));

    switch (args.mode) {
    case Mode::Help:
        std::fputs("steam-library-screensaver (.scr host)\n"
                   "  /s            run fullscreen (default)\n"
                   "  /c[:HWND]     configure\n"
                   "  /p <HWND>     preview\n"
                   "  --wallpaper   live wallpaper behind desktop icons\n"
                   "  --list-artwork  print discovered artwork\n",
                   stdout);
        return 0;

    case Mode::ListArtwork:
        return diagnoseArtwork();

    case Mode::ChangePassword:
        return 0; // legacy contract: ignore, exit 0

    case Mode::Configure: {
        // Prefer the Qt configurator (doc 5.8) when it sits next to the .scr;
        // otherwise materialise the default config and hand it to the user's
        // JSON editor so /c does something useful instead of logging out.
        {
            wchar_t exeDir[MAX_PATH]{};
            const DWORD n =
                GetModuleFileNameW(nullptr, exeDir, std::size(exeDir));
            if (n > 0 && n < std::size(exeDir)) {
                std::filesystem::path cfgExe =
                    std::filesystem::path(exeDir).parent_path() /
                    L"slc_configurator.exe";
                if (std::filesystem::exists(cfgExe)) {
                    const HINSTANCE rc = ShellExecuteW(
                        nullptr, L"open", cfgExe.c_str(), nullptr,
                        exeDir, SW_SHOWNORMAL);
                    if (reinterpret_cast<INT_PTR>(rc) > 32) {
                        SLC_LOG_INFO("launched configurator: {}",
                                     cfgExe.string());
                        return 0;
                    }
                }
            }
        }
        const auto path = slc::config::defaultConfigPath();
        if (!std::filesystem::exists(path)) {
            if (auto saved =
                    slc::config::saveConfig(path, slc::config::AppConfig{});
                !saved) {
                SLC_LOG_WARN("could not write default config {}: {}",
                             path.string(), saved.error().message);
                return 0;
            }
        }
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
        SLC_LOG_INFO("opened config for editing: {}", path.string());
        return 0;
    }

    case Mode::Wallpaper: {
        // Separate mutex: wallpaper and screensaver sessions may coexist
        // (the saver temporarily draws over the wallpaper when it fires).
        HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\SLCWallpaper");
        if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            return 0;
        }
        const int rc = runWallpaperSession();
        if (mutex != nullptr) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        return rc;
    }

    case Mode::Preview:
        if (args.previewParent == nullptr || !IsWindow(args.previewParent)) {
            return 0; // contract: silent failure on invalid preview HWND
        }
        [[fallthrough]];
    case Mode::Run: {
        // Single-instance guard for fullscreen transitions.
        HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
        if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            return 0;
        }
        const int rc = runScreensaverSession(args.mode == Mode::Preview,
                                             args.previewParent);
        if (mutex != nullptr) {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        return rc;
    }
    }
    return 0;
}
