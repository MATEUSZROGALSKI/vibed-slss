#pragma once

#include <windows.h>

// Runs the screensaver window + message loop until it exits.
// Implemented by win_cinder.cpp when SLC_BUILD_CINDER=ON, otherwise by
// win_cinder_stub.cpp which falls back to the discovery diagnostic.
// Returns the process exit code.
int runScreensaverSession(bool preview, HWND previewParent);

// Live-wallpaper variant: same GL session parented into the WorkerW layer
// behind the desktop icons. Implemented next to runScreensaverSession.
int runWallpaperSession();

// Enable the ImGui debug overlay for subsequently started sessions
// (--debug-ui). No-op in non-Cinder builds.
void setSessionDebugUi(bool enabled);

// Discovery diagnostic shared by --list-artwork and the no-Cinder fallback.
// Defined in win_main.cpp.
int diagnoseArtwork();
