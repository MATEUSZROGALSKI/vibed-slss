# Steam Library Screensaver — Developer Guide

Architecture, build workflow, and recipes for extending the app. Read with
`AGENTS.md` (repo conventions) and `docs/SPEC_STATUS.md` (spec coverage).

## 1. Module map

```
src/
  core/
    common          Result<T>/Error, logging helpers. No deps.
    platform_iface  platform::paths (configDir/cacheDir/logDir per OS).
    config          AppConfig — strict-tolerant JSON, unknown keys warn+skip.
    content         Steam discovery (locator, VDF/appinfo parsers),
                    IContentProvider, Artwork model, IHttpTransport,
                    HltbResolver, token bucket.
    cache           ArtworkCache — manifest + LRU eviction.
    layout          ILayoutStrategy + 7 strategies → vector<ci::Rectf>.
    anim            ICellAnimator impls, CollageChoreographer,
                    KenBurnsAnimator, SceneTransition.
    render          Cover-fit, texture upload, quality checks.
    app             ScreensaverApp (Cinder App) — glues everything.
  enrichment        cpr/libcurl IHttpTransport (SLC_WITH_CPR only).
  platform/
    wallpaper       IWallpaperBackend + makeWallpaperBackend().
    win             slc_host_win (.scr contract /s /p /c, wallpaper embed).
    linux           Wayland/X11 layer, systemd unit.
    mac             .saver bundle (M6).
  tools/
    tray            Qt tray: mode switching, host spawn/kill, lock file.
    wallpaperd      Static wallpaper swapper (no GPU).
    configurator    Qt settings UI over AppConfig + schema.
tests/
  unit              GoogleTest, no GL required.
  fixtures          steam-trees/ synthetic Steam install (text+binary VDF).
```

**Dependency rule:** core modules never include platform headers; OS code
lives behind `IPlatformHost` / `IWallpaperBackend` / `platform::paths`.
`slc_content` is intentionally cpr-free — HTTP is injected via
`IHttpTransport` (production: `CprTransport` in `src/enrichment`; tests:
mocks).

## 2. Build & test

```bat
cmake --preset dev-win            :: configure
cmake --build --preset dev-win    :: build
build\dev-win\tests\slc_unit_tests.exe
```

Presets: `dev-{win,linux,mac}`, `ci-*`, `release-*`. Make sure `VCPKG_ROOT`
is set and your C++ toolchain is on PATH before configuring.

Feature toggles (vcpkg manifest features):
`enrichment` → cpr/libcurl for SGDB+HLTB (**release builds that should
resolve HLTB need `SLC_WITH_CPR=ON`**), `gui` → Qt tools,
`tests` → gtest. Without cpr, `makeHttpTransport()` returns nullptr and
enrichment resolves to a no-op by design.

## 3. Runtime data flow

```
SteamLocalProvider.discover() ──► vector<Artwork>
   appinfo.vdf (name + common.type) ─ filters non-games
   localconfig.vdf (playtime, lastPlayed)
   appcache/stats/*.bin (achievement earned/total)
        │
        ▼  optional online merge
SteamGridDbProvider / HltbResolver (background jthread, disk-cached)
        │
        ▼  pick()
PlaylistEntry {appId, name, path, stats…}  (m_playlistMutex)
        │
        ▼  per scene
ILayoutStrategy.compute() → cell rects (snapped to device px)
CollageChoreographer + ICellAnimators on ci::Timeline
        │
        ▼  draw()
drawScene() → [FBO → user .frag shader] → screen
```

Background HLTB updates patch playlist entries under `m_playlistMutex`;
saving every 32 lookups + at thread end so killed sessions retain data.
`resolve()` early-returns on null transport — never negative-cache when
offline.

## 4. Extension recipes

### 4.1 New collage layout

1. `src/core/layout/include/slc/layout/foo_strategy.h` +
   `src/foo_strategy.cpp`: implement `ILayoutStrategy`
   (`name()`, `compute(bounds, cellCount, rng) → vector<Rectf>`).
2. Add sources to `src/core/layout/CMakeLists.txt`.
3. Instantiate in the strategy picker in `screensaver_app_scenes.cpp`
   (search `"masonry"`).
4. Add the name to the `layout.strategy` enum in
   `resources/config.schema.json` and the combo in
   `config_window.cpp` (`m_strategy->addItems`).
5. Test in `tests/unit/test_layout.cpp`: rects inside bounds, positive
   area, exact count, deterministic for a fixed `std::mt19937` seed.

**Invariant:** rects are snapped to whole device pixels in
`rebuildCells()` (`contentScale`) so adjacent scissor clips abut exactly —
do not reintroduce sub-pixel offsets or the seam flicker returns.

### 4.2 New cell animator

Implement `ICellAnimator` (`attach(timeline, cell, tex)` + `alpha()`,
`scale()`, `offset()`, `rotation()` — multiply-fold for alpha/scale,
sum-fold for offset/rotation). Register it in `CollageChoreographer`.
Keep `attach()` cheap — the `ci::Timeline` owns the tweens. Clip-safe:
animators that *shrink* cells must stay within the scissor inflation
margin (currently 8%); see the seam comment in `drawCells()`.

### 4.3 New scene transition

Add a value to `enum class SceneTransition`, extend the enum ↔ string
mapping and the `animation.transition` schema enum, then implement the
blend in the transition draw path in `screensaver_app.cpp`.

### 4.4 New content provider

Implement `IContentProvider::discover(DiscoveryContext)` — fill
`Artwork{appId, gameName, kind, path, width, height, source, playtime…}`.
Providers are merged in discovery order; `pickBestFit()` picks the best
aspect per slot. Respect `ctx.excludeAppIds`, `includeKinds`,
`minImageWidth`, `installedOnly`. Fixture a synthetic tree under
`tests/fixtures/steam-trees/`.

### 4.5 New enrichment resolver (HLTB pattern)

Copy `hltb.h/.cpp`:

- Inject `std::unique_ptr<IHttpTransport>` — never link cpr into
  `slc_content`. Use `post()` for JSON bodies (only cpr implements it).
- Rate-limit with `TokenBucket`; run resolution on `std::jthread` with a
  `stop_token`; patch the playlist under `m_playlistMutex`.
- Disk cache: atomic write via `serialize()/load()`; negative entries
  carry a TTL; serialize empty maps as `{}` not `null`.
- **Never poison the cache when transport is null** — guard `resolve()`.

### 4.6 New platform backend

- Rendering host: implement `IPlatformHost` (`isPreviewMode()`,
  `requestExit()`, `displays()`) and a `main` that maps the OS contract
  (Windows: `/s` `/p` `/c`; see `win_cinder.cpp`).
- Wallpaper: implement `IWallpaperBackend` (`setWallpaper`,
  `currentWallpaper`) and extend `makeWallpaperBackend()`. **Windows gotcha:**
  `SPI_SETDESKWALLPAPER|SPIF_SENDCHANGE` and `IDesktopWallpaper` can block
  forever on a hung top-level window — call them off the UI thread; the
  tray holds the backend in a `shared_ptr` for exactly this reason.

### 4.7 New config key (end-to-end)

1. `resources/config.schema.json` — declare type/default.
2. `app_config.h` — field on the right `*Config` struct.
3. `app_config.cpp` — read + write + add to the known-keys set (unknown
   keys log a warning).
4. `config_window.cpp/.h` — widget, `load()`, `collect()`/save.
5. Consume — e.g. `m_config.rendering.shader` in `loadUserShader()`.
6. Unit test in `test_config.cpp` (round-trip + default).

### 4.8 User shader contract

`<configDir>/shaders/*.frag`, GLSL 150. Uniforms: `ciTex0` (frame),
`uResolution` (vec2 px), `uTime` (float s); `vTexCoord` varying; `oColor`
out. Filename-only config value (traversal rejected); compile failure →
passthrough + log. Loader: `loadUserShader()` in `screensaver_app.cpp`.

## 5. Conventions & gotchas

- **Determinism:** layout strategies must be pure functions of the rng
  seed — tests assert identical output for identical seeds.
- **HiDPI:** multiply scissor rects by `getContentScale()`; snap layout
  bounds to device pixels (seam fix).
- **Threads:** `m_playlistMutex` guards playlist reads/writes;
  wallpaper backends may be invoked off-thread — keep them `shared_ptr`.
- **Errors:** `Result<T>`/`Error`, don't throw across module edges; log
  via `SLC_LOG_*` (spdlog/fmt).
- **No secrets in the repo:** SGDB key comes from env var only.
- **Empty JSON objects** must serialize as `{}`, not `null` (HLTB cache
  bug class).
- Debug binaries need debug CRT (`VCRUNTIME140D.dll`) — deploy/test only
  release exes on machines without the toolchain.

## 6. Testing

```bat
build\dev-win\tests\slc_unit_tests.exe            :: all suites
slc_unit_tests.exe --gtest_filter=Layout*          :: subset
```

- 110 tests / 25 suites currently; unit tests must not need GL or a real
  Steam install — fixtures live in `tests/fixtures/steam-trees/` (binary
  VDF blobs are generated by `tools/` scripts checked into the fixtures).
- Cover new strategies/animators/config keys with table tests matching
  `test_layout.cpp` style.
- `slc_host_win.exe --list-artwork` is the end-to-end discovery smoke test
  against a real install.

## 7. Release/deploy notes

- Release needs `SLC_WITH_CPR=ON` for HLTB/SGDB (links cpr/libcurl/libz —
  ship those DLLs).
- Windows runtime needs `vcruntime140*.dll` + `msvcp140.dll` alongside;
  Qt tools get theirs via `windeployqt`.
- The installed copy (`%LOCALAPPDATA%\Programs\SteamLibraryScreensaver`)
  is what autostart launches — sync binaries *and* DLLs there on release.
- Packaging/signing hooks: `cmake/SlcPackaging.cmake`, `CodeSign.cmake`,
  `BundleMacSaver.cmake`; CI in `.github/workflows/{ci,nightly-soak,release}.yml`.
