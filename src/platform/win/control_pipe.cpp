#include "control_pipe.h"

#include <windows.h>

#include <array>

namespace slc::platform {
namespace {

constexpr const wchar_t* kPipeName = LR"(\\.\pipe\io.github.slc.Control)";
constexpr DWORD kReplyMax = 64 * 1024;

} // namespace

std::string sendControlCommand(const std::string& command)
{
    // Wait briefly for a session to exist; an immediate CreateFile misses
    // a just-started listener.
    const HANDLE pipe = CreateFileW(
        kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_PIPE_BUSY &&
            WaitNamedPipeW(kPipeName, 2000)) {
            return sendControlCommand(command);
        }
        return "error no session listening (start the screensaver or "
               "wallpaper mode first)";
    }

    const std::string line = command + "\n";
    DWORD written = 0;
    if (!WriteFile(pipe, line.data(),
                   static_cast<DWORD>(line.size()), &written, nullptr)) {
        CloseHandle(pipe);
        return "error write failed";
    }

    std::array<char, kReplyMax> buf{};
    DWORD read = 0;
    std::string reply;
    // ReadFile returns when the server flushes; the reply is one line.
    if (ReadFile(pipe, buf.data(), kReplyMax, &read, nullptr) && read > 0) {
        reply.assign(buf.data(), read);
        while (!reply.empty() &&
               (reply.back() == '\n' || reply.back() == '\r')) {
            reply.pop_back();
        }
    } else {
        reply = "error no reply";
    }
    CloseHandle(pipe);
    return reply;
}

} // namespace slc::platform
