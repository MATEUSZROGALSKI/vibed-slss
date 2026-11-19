# Spec conformance status

Tracks implementation status against the development plan (`doc.md`,
maintainer-local). **Update this file in the same PR that changes spec
coverage** — see `AGENTS.md`.

Legend: ✅ implemented · 🔶 partial / deviates · ❌ not implemented · ➕ beyond spec

## Milestones

| Spec item | Status | Notes |
|---|---|---|
| M0 — Foundations (presets, vcpkg, Cinder fetch, gtest/ctest, CI, lint tooling) | ✅ | |
| M1 — Shuffle & switch (SteamLocalProvider, TexturePool, ArtworkCache, cycling + crossfade) | ✅ | |
| M2 — Ken Burns entrance | ✅ | Golden-image exit criterion unmet (no visual tests) |
| M3 — Collage layout | ✅ ➕ | 7 strategies vs spec's 2 (grid, masonry, spiral, spotlight, bsp, rows, mosaic) + matrix choreographer |
| M4 — Collage animation | ✅ | Choreographer, cell animators, scene transitions, deterministic tests |
| M5 — Linux | 🔶 | Path A (DBus) + Path B (XSS) done; Path C (`ext-idle-notify-v1`) missing — DBus used on Wayland. `wlr-layer-shell` wallpaper added ➕ |
| M6 — macOS | 🔶 | Bundle, entitlements, universal binary, codesign/notarize hooks done; runtime validation pending (no Mac CI runner with GPU) |
| M7 — Wallpaper shuffle | 🔶 | Win + Linux backends done (Linux exceeds spec: Cinnamon/xwallpaper too); **macOS backend missing** (stub) |

## Protocols & contracts

| Spec item | Status | Notes |
|---|---|---|
| Windows `.scr` arg contract (`/s /c /p /a /0..9`, mutex, GUI subsystem) | ✅ | WorkerW live-wallpaper embedding ➕ |
| SteamGridDB §6.2 (token bucket, 429/Retry-After, backoff, ETag, 7-day cache, silent degradation) | ✅ | API key via env var only — **no OS keychain** (CredMan/Keychain/libsecret) ❌ |
| §6.5 IPC control channel (pipe/DBus `io.github.slc.Control`: Reload/GetStatus/SetConfig) | ✅ | Win: `\\.\pipe\io.github.slc.Control` + `--ctl` client; Linux: session-bus adaptor, drive via `busctl`; shared `ScreensaverApp::controlCommand` dispatch |
| §6.3 module boundary rules (Result<T>, mt19937 injection, stop_token) | ✅ | |
| §6.4 logging (spdlog rotating, silent stdout) | ✅ | |

## Testing & verification

| Spec item | Status | Notes |
|---|---|---|
| §9.2 Unit tests (GoogleTest) | ✅ | `tests/unit/` + `tests/host_win` |
| §9.3 Integration tests | ✅ | `tests/integration/test_pipeline.cpp` — fixture → content → layout → choreography |
| §9.4 Visual regression (`tests/visual/` goldens) | ❌ | |
| §9.5 Benchmarks (Google Benchmark) | ✅ | `tests/bench/` — layout + VDF/config/pick suites |
| §9.6 Test hooks (`SLC_TEST_*` behind `--allow-test-hooks`) | ✅ | `slc::env::getTestHook`/`testHookTruthy` gate all reads; flag forwarded to spawned sessions |
| §8.4 CI static analysis (clang-tidy/cppcheck/`/analyze`) | 🔶 | Advisory clang-tidy job (report artifact, non-gating while codebase is cleaned up) |
| Debug+Release CI builds | ✅ | ci-* (Release+Werror) + dev-* (Debug) across all 3 OSes |
| Coverage tracking (G6 ≥70%) | 🔶 | gcovr report + HTML artifact on Linux; ≥70% threshold not yet enforced |
| Nightly 8h soak | ✅ | `nightly-soak.yml` (llvmpipe/Xvfb + win) |
| Platform smoke matrix §9.6 | ❌ | No self-hosted/VM runners |

## Tooling & docs

| Spec item | Status | Notes |
|---|---|---|
| §4 ImGui `--debug-ui` overlay | ✅ | `SLC_WITH_IMGUI` + vcpkg `debug-ui` feature; Cinder-signal→ImGuiIO binding, stats + live config toggles |
| Doxygen `docs/api` | ✅ | `Doxyfile` + `docs` CI job |
| `docs/adr/` ADRs | ✅ | Template + ADR-0001 (bundled GLM) |
| git-cliff `CHANGELOG.md` | ✅ | `cliff.toml` + `CHANGELOG.md` |
| User/dev guides | ✅ | `docs/USER_GUIDE.md`, `docs/DEVELOPER_GUIDE.md` |
| Installers | ✅ | NSIS (win), .deb/.tar.gz, AppImage, DMG, Flatpak manifest |
| Signing/notarization | 🔶 | Hooks in `cmake/CodeSign.cmake`, secrets-gated |

## Extra features not in spec

Badges (HLTB, achievements via `UserGameStats`, playtime/last-played),
Qt tray with wallpaper-mode switching, custom post-shaders, live config
reload (mtime), `--list-artwork` diagnostics, Wayland `wlr-layer-shell`
live wallpaper, self-healing decode pipeline.
