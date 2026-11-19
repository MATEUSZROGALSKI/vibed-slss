# Agent notes

## Build & verify

Requires: cmake 3.24+, ninja, a C++20 compiler, and vcpkg (`VCPKG_ROOT` set).
Qt 6 is only needed when `SLC_BUILD_QT_TOOLS=ON` (pass its install prefix via
`CMAKE_PREFIX_PATH`).

```sh
cmake --preset dev-win          # or dev-linux / dev-mac
cmake --build --preset dev-win
ctest --preset dev-win          # GoogleTest unit tests
```

Other presets: `ci-*` (warnings-as-errors, core-only coverage) and
`release-*` — see `CMakePresets.json`.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `SLC_BUILD_TESTS` | ON | Unit/integration tests |
| `SLC_BUILD_HOSTS` | ON | Platform host executables (.scr host, tray, wallpaperd) |
| `SLC_FETCH_CINDER` | ON | Fetch Cinder sources for header-only targets (`slc_layout` needs `ci::Rectf`) |
| `SLC_BUILD_CINDER` | OFF | Compile full libcinder + the GL modules (anim/render/app) and real host paths |
| `SLC_BUILD_QT_TOOLS` | OFF | Qt configurator + Linux Qt host |
| `SLC_WITH_CPR` | OFF | cpr/OpenSSL HTTP transport for SteamGridDB enrichment |
| `SLC_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |

vcpkg manifest features (`vcpkg.json`): `tests`, `enrichment`, `gui`,
`parallel` (TBB decode pool).

## Conventions

- `slc::` namespaces, snake_case file names, `m_` prefix for members.
- Log via `SLC_LOG_*` macros (`slc/common/logging.h`).
- Errors: `Result<T>` = `std::expected<T, Error>` (`slc/common/error.h`).
- Runtime env overrides: `SLC_STEAM_ROOT`, `SLC_CONFIG_PATH`,
  `SLC_LOG_LEVEL`, `SLC_SGDB_KEY` (values are machine/user-local — never
  commit them).

## Core isolation

No platform `#ifdef` or OS/Qt headers under `src/core/` — enforced by
`tools/ci/check_core_isolation.py` (run it before submitting). Per-OS code
lives under `src/platform/{win,mac,linux,wallpaper}` and is selected by
CMake, not the preprocessor.

## Gotchas

- **Never add `glm` to `vcpkg.json`.** Cinder TUs resolve `glm/` from
  Cinder's bundled copy; vcpkg's newer GLM produces a different ABI under
  the same headers — an invisible ODR violation. All Cinder-consuming TUs
  must see the bundled GLM.
- Wayland protocols (`wlr-layer-shell`, `xdg-shell`) are vendored as XML
  under `src/platform/linux/wayland/` and code-generated at build time;
  the layer-shell `namespace` arg is renamed `namespace_` in generated
  code (C++ keyword).
- `resources/config.schema.json` is the source of truth for `config.json`
  keys — keep it in sync with `slc/config` parsing.

## Layout

```
src/core/{common,platform_iface,content,cache,config,layout,anim,render,app}
src/enrichment                # SteamGridDB / HLTB (optional cpr transport)
src/platform/{win,mac,linux,wallpaper}
src/tools/{configurator,tray,wallpaperd}
tests/{unit,fixtures}         # fixtures = synthetic Steam installs
tools/ci                      # isolation check, SBOM gen, soak scripts
```
