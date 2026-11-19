// Windows host contract test (doc 9.3): launch
// `slc_host_win.exe /p <HWND>` against a real parent window, assert the
// screensaver creates a child inside it, then close it and assert the
// process exits 0.
//
//   usage: slc_preview_harness <path-to-slc_host_win.exe>
#include <windows.h>

#include <cstdio>
#include <string>

namespace {

HWND findChild(HWND parent)
{
    return FindWindowExW(parent, nullptr, nullptr, nullptr);
}

void pumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <slc_host_win.exe>\n", argv[0]);
        return 64;
    }

    const wchar_t* kClass = L"SlcPreviewHarnessParent";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    HWND parent =
        CreateWindowExW(0, kClass, L"slc-preview-parent",
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                        CW_USEDEFAULT, 680, 420, nullptr, nullptr,
                        wc.hInstance, nullptr);
    if (parent == nullptr) {
        std::fprintf(stderr, "CreateWindowEx failed (%lu)\n", GetLastError());
        return 2;
    }

    wchar_t cmd[2048];
    swprintf_s(cmd, L"\"%hs\" /p %llu", argv[1],
               static_cast<unsigned long long>(
                   reinterpret_cast<uintptr_t>(parent)));

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi)) {
        std::fprintf(stderr, "CreateProcess failed (%lu)\n", GetLastError());
        return 3;
    }

    // Poll up to 15 s for the preview child while pumping our own queue.
    HWND child = nullptr;
    for (int i = 0; i < 150 && child == nullptr; ++i) {
        pumpMessages();
        child = findChild(parent);
        if (child == nullptr) {
            Sleep(100);
        }
    }

    int rc = 0;
    if (child == nullptr) {
        std::fprintf(stderr, "no preview child window appeared\n");
        TerminateProcess(pi.hProcess, 1);
        rc = 4;
    } else {
        RECT pr, cr;
        GetClientRect(parent, &pr);
        GetWindowRect(child, &cr);
        MapWindowPoints(HWND_DESKTOP, parent,
                        reinterpret_cast<POINT*>(&cr), 2);
        const long w = pr.right - pr.left;
        const long h = pr.bottom - pr.top;
        if (cr.right - cr.left != w || cr.bottom - cr.top != h) {
            std::fprintf(stderr,
                         "child rect %ldx%ld != client %ldx%ld\n",
                         cr.right - cr.left, cr.bottom - cr.top, w, h);
            rc = 5;
        }
        // The real contract: Windows tears down the preview by destroying
        // the host HWND, which destroys the child with it. Emulate that.
        DestroyWindow(parent);
        if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) {
            std::fprintf(stderr, "preview did not exit on host destroy\n");
            TerminateProcess(pi.hProcess, 1);
            rc = 6;
        } else {
            DWORD code = ~0u;
            GetExitCodeProcess(pi.hProcess, &code);
            if (code != 0) {
                std::fprintf(stderr, "exit code %lu\n",
                             static_cast<unsigned long>(code));
                rc = 7;
            }
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    DestroyWindow(parent);
    return rc;
}
