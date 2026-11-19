# Steam Library Screensaver

Cross-platform screensaver that composes your local Steam library artwork into
an animated collage. C++20, Cinder (OpenGL), CMake + vcpkg.

## Features

- **Screensaver + live wallpaper**: `.scr` contract (`/s` `/p` `/c`),
  Explorer `WorkerW` embedding behind desktop icons, static-swap fallback
  via `slc_wallpaperd`, Qt tray with wallpaper-mode switching.
- **Scenes**: single-hero Ken Burns + 7 collage layouts (grid, masonry,
  spiral, spotlight, bsp, rows, mosaic) with cell drift/fade, four
  transitions, device-pixel-snapped seams, still-hold window.
- **Badges**: per-scene game card — title, playtime + last played,
  HowLongToBeat (cached background resolution), achievement completion
  from Steam's local stat caches. All offline except optional enrichment.
- **Discovery**: `appinfo.vdf` (games only — DLC/tools filtered),
  `libraryfolders.vdf`/`loginusers.vdf`/`localconfig.vdf` parsing, user
  grid art precedence, optional SteamGridDB artwork (keyed, opt-in).
- **Rendering**: Cinder/OpenGL, cover-fit, blurred letterbox backdrop,
  user fragment-shader post-processing, scissor-clipped cells.
- **Config**: strict-tolerant `config.json` (schema in
  `resources/config.schema.json`) + Qt configurator with searchable
  per-game filter checklist.

## Status

Working on Windows today, most of it portable to Linux/macOS.

## Quickstart (Windows)

```bat
:: prerequisites: cmake 3.24+, ninja, a C++20 compiler, VCPKG_ROOT set
cmake --preset dev-win
cmake --build --preset dev-win
ctest --preset dev-win

:: dry-run the discovery pipeline against your real Steam install
build\dev-win\src\platform\win\slc_host_win.exe --list-artwork
```

Presets: `dev-win` / `dev-linux` / `dev-mac`, `ci-*`, `release-*`
(`CMakePresets.json`). vcpkg manifest mode pulls nlohmann-json + spdlog;
`tests` feature adds gtest + benchmark; `enrichment`/`gui`/`parallel` are
opt-in features (`vcpkg.json`).

## Configuration

`config.json` at `%APPDATA%\SteamLibraryScreensaver\` (Windows),
`~/.config/steam-library-screensaver/` (Linux),
`~/Library/Application Support/SteamLibraryScreensaver/` (macOS).
Schema: `resources/config.schema.json`. Env overrides: `SLC_STEAM_ROOT`,
`SLC_CONFIG_PATH`, `SLC_LOG_LEVEL`, `SLC_SGDB_KEY`.

## Layout

`src/core/{common,platform_iface,content,cache,config,layout,anim,render,app}`,
`src/enrichment`, `src/platform/{win,mac,linux}`,
`src/tools/{configurator,wallpaperd}`, `tests/{unit,integration,fixtures}`,
`tools/ci`.

## Testing

```bat
ctest --preset dev-win                  :: unit tests (GoogleTest)
python tools/ci/check_core_isolation.py :: core must not touch OS/Qt headers
```

Fixtures live in `tests/fixtures/steam-trees/` (synthetic Steam installs).
