#pragma once

// §6.5 control channel (Windows transport): the running session listens
// on \\.\pipe\io.github.slc.Control; `slc_host_win --ctl <CMD>` sends one
// line and prints the reply. Commands: RELOAD, STATUS, SETCONFIG <json>.

#include <string>

namespace slc::platform {

/// Starts the pipe listener on a background thread. No-op without Cinder
/// (stub builds keep the symbol so call sites stay #if-free).
void startControlServer();
/// Signals the listener thread to exit and joins it.
void stopControlServer();
/// Client side: writes one command line to the pipe and returns the reply
/// line, or a string starting with "error ".
[[nodiscard]] std::string sendControlCommand(const std::string& command);

} // namespace slc::platform
