#include "slc/platform/paths.h"
#include "slc/platform/media.h"

#include <shlobj.h>
#include <windows.h>

#include <array>
#include <string>

namespace slc::platform {
namespace {

constexpr wchar_t kAppDirName[] = L"SteamLibraryScreensaver";

std::filesystem::path knownFolder(REFKNOWNFOLDERID id, const wchar_t* subDir)
{
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) {
        return std::filesystem::temp_directory_path() / subDir;
    }
    std::filesystem::path result = raw;
    CoTaskMemFree(raw);
    return result / subDir;
}

std::optional<std::wstring> readRegString(HKEY root, const wchar_t* subKey, const wchar_t* value)
{
    std::array<wchar_t, 1024> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    const LSTATUS status = RegGetValueW(root, subKey, value, RRF_RT_REG_SZ, nullptr,
                                        buffer.data(), &size);
    if (status != ERROR_SUCCESS || size < sizeof(wchar_t)) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

} // namespace

std::filesystem::path configDir()
{
    return knownFolder(FOLDERID_RoamingAppData, kAppDirName);
}

std::filesystem::path logDir()
{
    return knownFolder(FOLDERID_LocalAppData, kAppDirName) / L"logs";
}

std::filesystem::path cacheDir()
{
    return knownFolder(FOLDERID_LocalAppData, kAppDirName) / L"cache";
}

std::optional<std::filesystem::path> detectSteamRoot()
{
    if (auto fromReg = readRegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
        return std::filesystem::path(*fromReg);
    }
    const std::filesystem::path fallback = L"C:/Program Files (x86)/Steam";
    if (std::filesystem::exists(fallback)) {
        return fallback;
    }
    return std::nullopt;
}

bool prefersReducedMotion()
{
    // "Show animations in Windows" — off means the user wants reduced motion.
    ANIMATIONINFO info{sizeof(ANIMATIONINFO), 0};
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &info, 0)) {
        return info.iMinAnimate == 0;
    }
    return false;
}

namespace {
BOOL CALLBACK monitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto* out = reinterpret_cast<std::vector<DisplayInfo>*>(data);
    MONITORINFOEXW mi{sizeof(mi)};
    if (!GetMonitorInfoW(monitor, &mi)) {
        return TRUE;
    }
    DisplayInfo info;
    info.id = std::to_string(reinterpret_cast<std::uintptr_t>(monitor));
    info.x = mi.rcMonitor.left;
    info.y = mi.rcMonitor.top;
    info.width = static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left);
    info.height = static_cast<int>(mi.rcMonitor.bottom - mi.rcMonitor.top);
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    UINT dpiX = 96;
    UINT dpiY = 96;
    // GetDpiForMonitor lives in shcore.dll; resolve lazily to stay Win8-safe.
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(
                GetProcAddress(shcore, "GetDpiForMonitor"))) {
            fn(monitor, 0, &dpiX, &dpiY);
        }
        FreeLibrary(shcore);
    }
    info.dpiScale = static_cast<float>(dpiX) / 96.0F;
    out->push_back(std::move(info));
    return TRUE;
}
} // namespace

std::vector<DisplayInfo> enumerateDisplays()
{
    std::vector<DisplayInfo> result;
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc,
                        reinterpret_cast<LPARAM>(&result));
    return result;
}

void initImageDecodeThread()
{
    // WIC (used by Cinder's ImageIo on MSW) requires an initialised COM
    // apartment on the calling thread.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
}

} // namespace slc::platform
