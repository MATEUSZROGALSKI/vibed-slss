# Agent notes

## Build & verify

Requires: cmake 3.24+, ninja, a C++20 compiler, and vcpkg (`VCPKG_ROOT` set).
Qt 6 is only needed when `SLC_BUILD_QT_TOOLS=ON` (pass its install prefix via
`CMAKE_PREFIX_PATH`).

```sh
cmake --preset dev-win          # or dev-linux / dev-mac
cmake --build --preset dev-win
ctest --preset dev-win          # GoogleTest unit tests
python tools/ci/check_core_isolation.py   # core must not touch OS/Qt headers
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

## Architecture rules

Layering: **hosts → core → providers**. Dependencies point downward only —
`src/core/` must never `#include` Qt, Win32, AppKit, or DBus headers
(CI-enforced via `tools/ci/check_core_isolation.py`). Forward-declare platform
types in core headers instead. Per-OS code lives under
`src/platform/{win,mac,linux,wallpaper}` and is selected by CMake, not the
preprocessor.

| Module | Target | Responsibility |
|---|---|---|
| `core/content` | `slc_content` | Steam discovery, VDF parsing, `Artwork` records |
| `core/cache` | `slc_cache` | Disk cache, LRU eviction, manifest |
| `core/layout` | `slc_layout` | `ILayoutStrategy` impls (grid, masonry, spiral, matrix, …) |
| `core/anim` | `slc_anim` | Ken Burns, cell fade/scale, collage choreography |
| `core/render` | `slc_render` | Texture upload, draw passes, post-processing |
| `core/app` | `slc_app` | `AppScreenSaver` lifecycle + state machine |

### Threading model

- **Main (GL) thread** owns all `ci::gl::Texture2d` creation/upload and every
  draw call — the only thread allowed to touch GL objects.
- Decode pool (`std::jthread` × N) decodes to CPU `Surface` only; uploads are
  drained on the main thread at **≤ 4 ms per frame**.
- `TexturePool` backpressure: at most `2 × visibleCellCount` resident textures.
- Never hold a lock across a GL call.
- Preview mode: capped at 15 fps, never touches the disk cache writer.

### Module boundary contracts

- **No exceptions across module boundaries.** Fallible APIs return
  `Result<T>` = `std::expected<T, Error>` (`slc/common/error.h`); catch-all
  lives at the host boundary.
- `[[nodiscard]]` on every fallible API.
- No raw owning pointers: `std::unique_ptr` for ownership; `shared_ptr` only
  for GL textures (`ci::gl::Texture2d::Ref`).
- Every randomized operation takes an explicit `std::mt19937&` — global RNG
  is forbidden (keeps scenes deterministic under a seeded config).
- Long-running discovery accepts a `std::stop_token` (cancellation).
- Use `SLC_ASSERT` (logs a fatal line first), never bare `assert()` for
  anything that could be input validation.

## Coding style

C++20, no compiler extensions outside platform-SDK-forced `#if defined(_MSC_VER)`
blocks. `.clang-format` (4-indent, col 100, left pointers) and `.clang-tidy`
are committed — CI fails on format diffs and on `core/` lint warnings.

| Entity | Convention | Example |
|---|---|---|
| Namespace | `snake_case` | `slc::core::layout` |
| Type / class | `PascalCase` | `LayoutEngine` |
| Interface | `I` + `PascalCase` | `IContentProvider` |
| Function / variable | `camelCase` | `discoverArtwork()`, `cellRect` |
| Member | `m_` + camelCase | `m_timeline` |
| Static / global | `s_` / `g_` + camelCase | `s_instance` |
| Constant / constexpr | `kPascalCase` | `kMaxCellCount` |
| Enum | `enum class PascalCase`, values `PascalCase` | `ArtworkKind::Hero` |
| Macro | `SLC_UPPER_SNAKE` | `SLC_LOG_INFO` |
| File | `snake_case.{h,cpp}` | `layout_engine.cpp` |

- Keep Cinder's upstream type names (`ci::gl::Texture2d`, `ci::Timeline`) —
  do not alias them.
- `#pragma once` everywhere. Include order: own header → stdlib → Cinder →
  Qt → platform SDK → project (enforced by clang-format categories).
- Public headers: Doxygen `/** … */` with `\param`, `\returns`,
  `\threadsafety`. Comments explain **why**, not what.
- Concurrency: `std::jthread`/`std::stop_token` over manual thread lifetime;
  `std::atomic` only for counters/flags; no `volatile`; justify any explicit
  `memory_order`.
- Logging: `SLC_LOG_*` macros only — no `printf`/`std::cout`. Screensaver
  contexts must stay silent on stdout/stderr; file logging is the channel.

## Config

`resources/config.schema.json` is the source of truth for `config.json` keys —
keep it in sync with `slc/config` parsing. Parsing is strict-tolerant: unknown
keys → warnings, missing keys → defaults, malformed JSON → whole default
document + `error` log. `version` gates migrations.

Runtime env overrides: `SLC_STEAM_ROOT`, `SLC_CONFIG_PATH`, `SLC_LOG_LEVEL`,
`SLC_SGDB_KEY` (values are machine/user-local — never commit them).

## Testing

- Unit tests in `tests/unit/` (GoogleTest); fixtures are small synthetic
  Steam trees in `tests/fixtures/steam-trees/<case>/` (keep each < 50 KB).
- Cover the usual edges: malformed/missing VDF, degenerate bounds (0×0),
  seeded determinism, corrupt manifest, `ENOSPC`, enum-string parsing.
- New public API needs a unit test; perf-sensitive paths need a benchmark or
  existing coverage.

## Git

- Trunk is `develop`; `release` is the stable line. Both are protected —
  all changes land via PR with ≥ 1 approval and green CI, squash-merged.
- Branch names: `feat/<slug>`, `fix/<slug>` (e.g. `feat/collage-layout`).
- Commits: Conventional Commits —
  `feat(layout): add masonry strategy`, `fix(win): guard null preview HWND`.

## Review checklist (before submitting)

- [ ] `core/` diff has no `#include <windows.h>`, `<Qt…>`, `<AppKit/…>`, `<dbus/…>`
- [ ] New public API is documented and unit-tested
- [ ] New deps added to `vcpkg.json` (never `glm` — see Gotchas)
- [ ] Randomness routes through injected `std::mt19937`
- [ ] `SLC_LOG_*`, no `printf`/`cout`
- [ ] `check_core_isolation.py` passes; `clang-format` clean

## Gotchas

- **Never add `glm` to `vcpkg.json`.** Cinder TUs resolve `glm/` from
  Cinder's bundled copy; vcpkg's newer GLM produces a different ABI under
  the same headers — an invisible ODR violation. All Cinder-consuming TUs
  must see the bundled GLM.
- Wayland protocols (`wlr-layer-shell`, `xdg-shell`) are vendored as XML
  under `src/platform/linux/wayland/` and code-generated at build time;
  the layer-shell `namespace` arg is renamed `namespace_` in generated
  code (C++ keyword).

## Layout

```
src/core/{common,platform_iface,content,cache,config,layout,anim,render,app}
src/enrichment                # SteamGridDB / HLTB (optional cpr transport)
src/platform/{win,mac,linux,wallpaper}
src/tools/{configurator,tray,wallpaperd}
tests/{unit,fixtures}         # fixtures = synthetic Steam installs
tools/ci                      # isolation check, SBOM gen, soak scripts
```
