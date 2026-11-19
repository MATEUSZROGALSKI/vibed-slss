// §6.5 control channel, Windows side: a dedicated thread serves
// \\.\pipe\io.github.slc.Control — one client at a time is plenty for a
// local control interface; each connection is one command line in, one
// reply line out. Dispatch itself lives in ScreensaverApp::controlCommand
// so the transport stays dumb.

#include "control_pipe.h"

#include "slc/app/screensaver_app.h"
#include "slc/common/logging.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <thread>

namespace slc::platform {
namespace {

constexpr const wchar_t* kPipeName = LR"(\\.\pipe\io.github.slc.Control)";
constexpr DWORD kCmdMax = 64 * 1024;

std::atomic<bool> g_stop{false};
std::thread g_thread;

void serveClient(HANDLE pipe)
{
    std::array<char, kCmdMax> buf{};
    DWORD read = 0;
    if (!ReadFile(pipe, buf.data(), kCmdMax, &read, nullptr) || read == 0) {
        return;
    }
    std::string cmd(buf.data(), read);
    while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r')) {
        cmd.pop_back();
    }
    const std::string reply = app::ScreensaverApp::controlCommand(cmd) + "\n";
    DWORD written = 0;
    WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()),
              &written, nullptr);
    FlushFileBuffers(pipe);
}

void serverLoop()
{
    while (!g_stop.load(std::memory_order_relaxed)) {
        const HANDLE pipe = CreateNamedPipeW(
            kPipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1 /*nMaxInstances*/, kCmdMax, kCmdMax, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            SLC_LOG_ERROR("control pipe: CreateNamedPipe failed ({})",
                          GetLastError());
            return;
        }

        // Overlapped ConnectNamedPipe so stop() can wake the wait with a
        // fake self-connection instead of a timeout poll.
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const BOOL pending =
            ConnectNamedPipe(pipe, &ov) == FALSE &&
            GetLastError() == ERROR_IO_PENDING;
        if (pending) {
            WaitForSingleObject(ov.hEvent, INFINITE);
        }
        CloseHandle(ov.hEvent);

        if (g_stop.load(std::memory_order_relaxed)) {
            CloseHandle(pipe);
            break;
        }
        serveClient(pipe);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

} // namespace

void startControlServer()
{
    if (g_thread.joinable()) {
        return;
    }
    g_stop.store(false);
    g_thread = std::thread(serverLoop);
    SLC_LOG_INFO("control pipe listening on {}", "io.github.slc.Control");
}

void stopControlServer()
{
    if (!g_thread.joinable()) {
        return;
    }
    g_stop.store(true);
    // Unblock ConnectNamedPipe with a self-connect that the loop discards.
    const HANDLE wake = CreateFileW(kPipeName, GENERIC_READ, 0, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
        CloseHandle(wake);
    }
    g_thread.join();
}

} // namespace slc::platform
