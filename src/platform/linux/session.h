#pragma once

// Runs one fullscreen screensaver session until dismissal (input) or an
// external close. Implemented by session_cinder.cpp when
// SLC_BUILD_CINDER=ON, otherwise by session_stub.cpp.
// Returns the process exit code.
int runScreensaverSession();

// XScreenSaver hack mode: renders the session onto the X window identified
// by `parentXid` ($XSCREENSAVER_WINDOW). Returns the process exit code.
// Returns 3 when the host cannot honour a foreign XID (no Cinder/GL build).
int runXssSession(unsigned long parentXid);

// Live-wallpaper session (doc 6.1.4): renders the scene engine behind the
// desktop. Wayland sessions use a wlr-layer-shell background surface; X11
// sessions an EWMH desktop-type window. Returns 3 when the environment
// can't host a live wallpaper (e.g. GNOME Wayland without layer-shell),
// so callers fall back to the static wallpaper daemon.
int runWallpaperSession();

// Enable the ImGui debug overlay for subsequently started sessions
// (--debug-ui). No-op in non-Cinder builds.
void setSessionDebugUi(bool enabled);

// Discovery diagnostic shared by --list-artwork and the no-Cinder fallback.
// Defined in linux_main.cpp.
int diagnoseArtwork();
