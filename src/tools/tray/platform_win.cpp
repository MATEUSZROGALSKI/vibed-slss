// Win32 helpers for slc_tray.
#include "platform_helpers.h"

#include <windows.h>

#include <shellapi.h> // SHQueryUserNotificationState

#include <cwchar>
#include <string>

namespace slc::tray_plat {

bool onBattery()
{
    SYSTEM_POWER_STATUS sps{};
    return GetSystemPowerStatus(&sps) && sps.ACLineStatus == 0;
}

bool fullscreenAppBusy()
{
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    if (FAILED(SHQueryUserNotificationState(&state))) {
        return false;
    }
    return state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
           state == QUNS_PRESENTATION_MODE;
}

QString describeFullscreenCause()
{
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    QString quns = QStringLiteral("query-failed");
    if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
        quns = QStringLiteral("QUNS=%1").arg(static_cast<int>(state));
    }
    QString fg = QStringLiteral("none");
    if (HWND hwnd = GetForegroundWindow()) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        QString exe = QStringLiteral("pid=%1").arg(pid);
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   pid)) {
            wchar_t name[MAX_PATH]{};
            DWORD len = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, name, &len)) {
                exe = QString::fromWCharArray(name).section('\\', -1);
            }
            CloseHandle(h);
        }
        fg = exe;
    }
    return QStringLiteral("%1 fg=%2").arg(quns, fg);
}

bool sessionLocked()
{
    // On a locked session the input desktop is "Winlogon", not "Default".
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (desk == nullptr) {
        return true;
    }
    wchar_t name[64]{};
    DWORD need = 0;
    const BOOL ok = GetUserObjectInformationW(desk, UOI_NAME, name,
                                              sizeof(name), &need);
    CloseDesktop(desk);
    return !ok || wcscmp(name, L"Default") != 0;
}

namespace {
constexpr wchar_t kSaverKey[] = L"Control Panel\\Desktop";
constexpr wchar_t kSaverValue[] = L"SCRNSAVE.EXE";
constexpr wchar_t kScrWndClass[] = L"SteamLibraryScreensaverWnd";
} // namespace

bool screensaverEnabled(const QString& scrPath)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSaverKey, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    wchar_t buf[MAX_PATH * 2]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS rc = RegQueryValueExW(key, kSaverValue, nullptr, &type,
                                        reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && type == REG_SZ &&
           _wcsicmp(buf, scrPath.toStdWString().c_str()) == 0;
}

bool setScreensaverEnabled(const QString& scrPath, bool enable,
                           QString* previousOut, const QString& previousIn)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSaverKey, 0,
                      KEY_READ | KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    if (enable) {
        // Stash the previous saver so unchecking restores it.
        wchar_t buf[MAX_PATH * 2]{};
        DWORD size = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExW(key, kSaverValue, nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf),
                             &size) == ERROR_SUCCESS &&
            type == REG_SZ && previousOut != nullptr) {
            *previousOut = QString::fromWCharArray(buf);
        }
        const std::wstring wide = scrPath.toStdWString();
        ok = RegSetValueExW(key, kSaverValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(wide.c_str()),
                            static_cast<DWORD>((wide.size() + 1) * 2)) ==
             ERROR_SUCCESS;
    } else {
        // Restore the displaced saver; empty string = "no saver".
        const std::wstring wide = previousIn.toStdWString();
        ok = RegSetValueExW(key, kSaverValue, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(wide.c_str()),
                            static_cast<DWORD>((wide.size() + 1) * 2)) ==
             ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return ok;
}

void startScreensaverNow(const QString& exePath)
{
    // Spawn our own /s session — works regardless of which saver the
    // system has registered.
    ShellExecuteW(nullptr, L"open", exePath.toStdWString().c_str(), L"/s",
                  nullptr, SW_HIDE);
}

void stopActiveScreensaver()
{
    // The saver window class is shared by /s sessions (win_cinder.cpp).
    if (HWND wnd = FindWindowW(kScrWndClass, nullptr); wnd != nullptr) {
        PostMessageW(wnd, WM_CLOSE, 0, 0);
    }
}

bool supportsLiveWallpaper()
{
    return true;
}

} // namespace slc::tray_plat
