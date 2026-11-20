#pragma once

// §6.5 control channel (Linux transport): the running session owns
// io.github.slc.Control on the session bus, exposing Reload() /
// GetStatus() / SetConfig(json) at /io/github/slc/Control. External
// tools drive it directly, e.g.:
//   busctl --user call io.github.slc.Control /io/github/slc/Control
//       io.github.slc.Control GetStatus

namespace slc::platform {

/// Registers the adaptor on the session bus; no-op when QtDBus or the
/// app module is unavailable (stub builds keep the symbol so call sites
/// stay #if-free).
void registerControlAdaptor();

} // namespace slc::platform
