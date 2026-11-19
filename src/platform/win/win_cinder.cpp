// Windows .scr rendering session (doc section 6.1.1), built only with
// SLC_BUILD_CINDER=ON.
//
// We keep our own WinMain (win_main.cpp) rather than linking scrnsave.lib so
// that --list-artwork diagnostics and the SLC_TEST_* env hooks live in the
// same binary. What follows re-implements the handful of scrnsave.lib
// behaviours the contract needs:
//   * /s: top-most popup over the virtual screen, hidden cursor
//   * /p: child window inside the supplied preview HWND
//   * quit on input/activation loss -> our DefScreenSaverProc
// Cinder's AppImplMswScreenSaver then owns frame timing, per-display blanking
// and update/draw dispatch once it has the HWND.

#include "win_session.h"

#include "slc/app/iplatform_host.h"
#include "slc/app/screensaver_app.h"
#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/platform/paths.h"

#include <windowsx.h>

#include <cinder/app/AppScreenSaver.h>
#include <cinder/app/RendererGl.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"SteamLibraryScreensaverWnd";
constexpr UINT_PTR kIdleTimerId = 2; // Cinder owns TIMER_ID=1
constexpr int kMouseMoveThresholdPx = 8;

ci::app::AppScreenSaver* s_app = nullptr;
HWND s_mainHwnd = nullptr;
bool s_preview = false;
bool s_wallpaper = false;
bool s_inputCheckDisabled = false;
bool s_haveFirstMouse = false;
POINT s_firstMouse{};

class WinHost final : public slc::app::IPlatformHost {
  public:
    explicit WinHost(bool preview) : m_preview(preview) {}

    [[nodiscard]] bool isPreviewMode() const noexcept override
    {
        return m_preview;
    }

    void requestExit(int code) override
    {
        if (s_mainHwnd != nullptr) {
            PostMessageW(s_mainHwnd, WM_CLOSE, 0, 0);
        } else {
            PostQuitMessage(code);
        }
    }

    [[nodiscard]] std::vector<slc::platform::DisplayInfo> displays()
        const override
    {
        return slc::platform::enumerateDisplays();
    }

  private:
    bool m_preview;
};

// Scrnsave.lib semantics, re-implemented so we never pull its WinMain.
// Cinder's AppImplMswScreenSaver defers to this for input/activation msgs.
extern "C" LRESULT WINAPI DefScreenSaverProc(HWND hWnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam)
{
    // SLC_TEST_DISABLE_INPUT_CHECK: keep running regardless of input.
    // Wallpaper windows sit behind the desktop icons and must never quit
    // on input or activation loss either.
    const bool swallowInput = s_inputCheckDisabled || s_preview ||
                              s_wallpaper;

    switch (msg) {
    case WM_MOUSEMOVE:
        if (swallowInput) {
            return 0;
        }
        {
            const POINT now{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!s_haveFirstMouse) {
                s_firstMouse = now;
                s_haveFirstMouse = true;
            } else if (std::abs(now.x - s_firstMouse.x) >
                           kMouseMoveThresholdPx ||
                       std::abs(now.y - s_firstMouse.y) >
                           kMouseMoveThresholdPx) {
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
            }
        }
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (!swallowInput) {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_ACTIVATE:
        if (!swallowInput && LOWORD(wParam) == WA_INACTIVE) {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_ACTIVATEAPP:
    case WM_NCACTIVATE:
        if (!swallowInput && wParam == FALSE) {
            PostMessageW(hWnd, WM_CLOSE, 0, 0);
        }
        return 0;

    case WM_SETCURSOR:
        if (!s_preview) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        s_app = nullptr;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void applySettings(ci::app::AppScreenSaver::Settings* settings)
{
    settings->setFrameRate(s_preview && !s_wallpaper ? 15.0F : 60.0F);
    if (s_wallpaper) {
        // Our window is a WS_CHILD inside WorkerW; keep its size and don't
        // spawn shell-owned blanking windows on other displays.
        settings->enableSecondaryDisplayBlanking(false);
    }
    if (s_inputCheckDisabled) {
        // Cinder debug mode: no quit-on-input, window not top-most.
        settings->enableDebug(true);
    }
}

// Default-constructible RendererGl with MSAA on — AppScreenSaver::main
// instantiates the renderer type itself, so options have to come from the
// ctor (clone() copies them through to every window's context).
class MsaaRendererGl final : public ci::app::RendererGl {
  public:
    MsaaRendererGl() : RendererGl(Options().msaa(4)) {}
};

LRESULT CALLBACK scrWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == kIdleTimerId) {
        PostMessageW(hWnd, WM_CLOSE, 0, 0);
        return 0;
    }
    if (msg == WM_CREATE) {
        s_mainHwnd = hWnd;
        s_app = ci::app::AppScreenSaver::main<slc::app::ScreensaverApp,
                                              MsaaRendererGl>(
            hWnd, "SteamLibraryScreensaver", &applySettings);
        if (const auto idle =
                slc::env::getTestHook(slc::env::kTestIdleTimeoutVar)) {
            if (const UINT ms =
                    static_cast<UINT>(std::strtoul(idle->c_str(), nullptr, 10));
                ms > 0) {
                SetTimer(hWnd, kIdleTimerId, ms, nullptr);
            }
        }
        return 0;
    }
    if (s_app != nullptr) {
        const LRESULT handled = s_app->eventHandler(hWnd, msg, wParam, lParam);
        if (msg == WM_DESTROY) {
            // Cinder consumes WM_DESTROY for cleanup but never posts WM_QUIT;
            // without this the pump idles on GetMessage forever after close.
            s_app = nullptr;
            PostQuitMessage(0);
        }
        return handled;
    }
    return DefScreenSaverProc(hWnd, msg, wParam, lParam);
}

} // namespace

int runScreensaverSession(bool preview, HWND previewParent)
{
    s_preview = preview;
    s_inputCheckDisabled =
        slc::env::testHookTruthy(slc::env::kTestDisableInputVar);
    slc::app::ScreensaverApp::setHostFactory(
        [preview] { return std::make_unique<WinHost>(preview); });

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = scrWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        SLC_LOG_ERROR("RegisterClassEx failed: {}", GetLastError());
        return 1;
    }

    HWND hWnd;
    if (preview) {
        RECT rc{};
        GetClientRect(previewParent, &rc);
        hWnd = CreateWindowExW(0, kWindowClass, L"SLC",
                               WS_CHILD | WS_VISIBLE, rc.left, rc.top,
                               rc.right - rc.left, rc.bottom - rc.top,
                               previewParent, nullptr, wc.hInstance, nullptr);
    } else {
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        hWnd = CreateWindowExW(WS_EX_TOPMOST, kWindowClass, L"SLC",
                               WS_POPUP | WS_VISIBLE, x, y, w, h, nullptr,
                               nullptr, wc.hInstance, nullptr);
    }
    if (hWnd == nullptr) {
        SLC_LOG_ERROR("CreateWindowEx failed: {}", GetLastError());
        return 1;
    }

    if (!preview) {
        ShowCursor(FALSE);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (!preview) {
        ShowCursor(TRUE);
    }
    s_mainHwnd = nullptr;
    SLC_LOG_INFO("screensaver session ended, code={}",
                 static_cast<int>(msg.wParam));
    return static_cast<int>(msg.wParam);
}

// Live-wallpaper mode (doc 6.1.4 extension): render the same scene engine
// into a WorkerW layer behind the desktop icons. The static wallpaper
// stays underneath, so killing this window restores the prior desktop.
// True when any descendant of `root` is the desktop icon view.
BOOL CALLBACK hasDefView(HWND child, LPARAM lp)
{
    wchar_t cls[64]{};
    if (GetClassNameW(child, cls, std::size(cls)) > 0 &&
        wcscmp(cls, L"SHELLDLL_DefView") == 0) {
        *reinterpret_cast<bool*>(lp) = true;
        return FALSE;
    }
    return TRUE;
}

struct DesktopLayer {
    HWND parent = nullptr;
    bool bottomZ = false; // child must sit at the bottom of `parent`
};

// SHELLDLL_DefView is also used by Explorer folder windows — only the
// one inside Progman is the desktop.
HWND findDefView(HWND progman)
{
    HWND defView = nullptr;
    EnumChildWindows(
        progman,
        [](HWND child, LPARAM lp) -> BOOL {
            wchar_t cls[64]{};
            if (GetClassNameW(child, cls, std::size(cls)) > 0 &&
                wcscmp(cls, L"SHELLDLL_DefView") == 0) {
                *reinterpret_cast<HWND*>(lp) = child;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&defView));
    return defView;
}

DesktopLayer findDesktopLayer()
{
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (progman == nullptr) {
        return {};
    }
    // Undocumented message 0x052C asks the shell to spawn a WorkerW layer
    // behind the one that hosts SHELLDLL_DefView (the icons). This is the
    // same technique Wallpaper Engine / Lively use.
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);

    // The layer may need a beat to materialize, and on newer shells
    // SHELLDLL_DefView sits deeper than a direct child — recurse.
    // Only Progman/WorkerW top-levels count: Explorer folder windows host
    // their own SHELLDLL_DefView and must not redirect the embed.
    for (int attempt = 0; attempt < 10; ++attempt) {
        HWND worker = nullptr;
        EnumWindows(
            [](HWND top, LPARAM lp) -> BOOL {
                wchar_t cls[64]{};
                GetClassNameW(top, cls, std::size(cls));
                if (wcscmp(cls, L"Progman") != 0 &&
                    wcscmp(cls, L"WorkerW") != 0) {
                    return TRUE;
                }
                bool has = false;
                EnumChildWindows(top, hasDefView,
                                 reinterpret_cast<LPARAM>(&has));
                if (has) {
                    *reinterpret_cast<HWND*>(lp) =
                        FindWindowExW(nullptr, top, L"WorkerW", nullptr);
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&worker));
        // Win11 24H2+ leaves only dead WorkerW stubs: a hidden parent
        // would hide us too, so require the layer to be visible.
        if (worker != nullptr && IsWindowVisible(worker)) {
            return {worker, false};
        }
        Sleep(50);
    }

    // No live WorkerW (modern shells): embed inside SHELLDLL_DefView at
    // bottom z-order — we cover its wallpaper paint while SysListView32
    // (the icons) stays above us.
    if (HWND defView = findDefView(progman); defView != nullptr) {
        SLC_LOG_INFO("no live WorkerW; embedding in SHELLDLL_DefView");
        return {defView, true};
    }
    // Last resort: under Progman. Icons may sit behind us, but the
    // wallpaper still shows.
    SLC_LOG_WARN("no desktop layer found; parenting to Progman");
    return {progman, false};
}

int runWallpaperSession()
{
    s_wallpaper = true; // never self-quit on input/activation
    slc::app::ScreensaverApp::setHostFactory(
        [] { return std::make_unique<WinHost>(false); });
    // Choosing animated wallpaper is explicit consent to motion; the OS
    // accessibility preference applies to system UI, not opted-in content.
    slc::app::ScreensaverApp::setIgnoreReducedMotion(true);

    const DesktopLayer layer = findDesktopLayer();
    if (layer.parent == nullptr) {
        SLC_LOG_ERROR("wallpaper: no desktop layer found");
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = scrWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (RegisterClassExW(&wc) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        SLC_LOG_ERROR("RegisterClassEx failed: {}", GetLastError());
        return 1;
    }

    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HWND hWnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, kWindowClass, L"SLC Wallpaper",
        WS_CHILD | WS_VISIBLE, x, y, w, h, layer.parent, nullptr,
        wc.hInstance, nullptr);
    if (hWnd == nullptr) {
        SLC_LOG_ERROR("wallpaper: CreateWindowEx failed: {}", GetLastError());
        return 1;
    }
    if (layer.bottomZ) {
        // Inside SHELLDLL_DefView: stay below SysListView32 (the icons).
        SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    wchar_t parentCls[64]{};
    GetClassNameW(layer.parent, parentCls, std::size(parentCls));
    SLC_LOG_INFO("wallpaper session embedded in {} ({}x{} @ {},{})",
                 std::filesystem::path(parentCls).string(), w, h, x, y);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    s_mainHwnd = nullptr;
    SLC_LOG_INFO("wallpaper session ended, code={}",
                 static_cast<int>(msg.wParam));
    return static_cast<int>(msg.wParam);
}

void setSessionDebugUi(bool enabled)
{
    slc::app::ScreensaverApp::setDebugUi(enabled);
}
