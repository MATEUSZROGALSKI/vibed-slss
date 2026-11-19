# Steam Library Screensaver — User Guide

Turns your Steam library artwork into an animated screensaver and live
wallpaper. Works offline — everything is discovered from your local Steam
installation. Optional online enrichment (SteamGridDB artwork,
HowLongToBeat durations) is opt-in and fully cached.

## Components

| Binary | What it does |
|---|---|
| `slc_tray` | System tray app. Starts/stops everything, shows status, opens Settings. Autostarts at login when installed. |
| `slc_host_win` / screensaver `.scr` | The animated renderer — embedded behind your desktop icons in live-wallpaper mode, or fullscreen as a screensaver. |
| `slc_wallpaperd` | Fallback daemon that periodically swaps a *static* wallpaper (used when GPU load is paused). |
| `slc_configurator` | Settings window (same one the tray opens). Edits `config.json`. |

## Where things live

| | Windows | Linux | macOS |
|---|---|---|---|
| Config | `%APPDATA%\SteamLibraryScreensaver\config.json` | `~/.config/steam-library-screensaver/` | `~/Library/Application Support/SteamLibraryScreensaver/` |
| User shaders | `<config>\shaders\` | same | same |
| Cache (HLTB, SGDB) | `%LOCALAPPDATA%\SteamLibraryScreensaver\cache\` | `~/.cache/steam-library-screensaver/` | `~/Library/Caches/` |
| Logs | `%LOCALAPPDATA%\SteamLibraryScreensaver\logs\slc.log` | state dir | state dir |

Install dir on Windows: `%LOCALAPPDATA%\Programs\SteamLibraryScreensaver`.

## Settings reference

All keys live in `config.json` (schema: `resources/config.schema.json`).
Everything below is also editable in the Settings window.

### `content` — which artwork to show

| Key | Default | Meaning |
|---|---|---|
| `steamRoot` | auto | Override Steam install path. Auto-detect: `SLC_STEAM_ROOT` env → registry/well-known paths. |
| `includeKinds` | `["hero","grid","header"]` | Artwork kinds. `hero` = wide cinematic art, `grid` = capsule/box art, `header` = store banner, `logo`/`icon` also available. |
| `excludeAppIds` | `["228980"]` | App IDs to never show. Editable via the searchable **Game Filter** checklist in Settings. |
| `minImageWidth` | `300` | Skip artwork narrower than this (px). |
| `installedOnly` | `false` | `false` = whole library incl. non-installed games. `true` = only games with a local appmanifest. |

Only **games** are shown — DLC, tools, videos, demos and other non-game
records from `appinfo.vdf` are filtered automatically.

### `layout` — collage composition

| Key | Default | Meaning |
|---|---|---|
| `strategy` | `"random"` | `random`, `grid`, `masonry`, `spiral`, `spotlight`, `bsp`, `rows`, `mosaic` (see below). |
| `cellCount` | `12` | Images per collage scene. |
| `cellCountMin` | `4` | Lower bound when the strategy varies count. |
| `gutter` | `12` | Pixels between cells. |
| `collageProbability` | `0.5` | Chance a scene is a collage vs single hero image (0–1). |
| `seed` | `"random"` | Fixed integer for reproducible layouts, `"random"` otherwise. |

Strategies:

- **grid** — uniform rows × columns.
- **masonry** — varying column widths, packed.
- **spiral** — cells spiraling inward from a large lead image.
- **spotlight** — one big hero cell, smaller cells around it.
- **bsp** — recursive binary splits; asymmetric, organic look.
- **rows** — horizontal bands of varying height/widths.
- **mosaic** — uniform grid with random 2×2 merged blocks.

### `animation` — motion and timing

| Key | Default | Meaning |
|---|---|---|
| `kenBurns.enabled` | `true` | Slow zoom/pan on single images. |
| `kenBurns.durationMs` | `8000` | Length of the zoom phase. |
| `kenBurns.zoomFrom` / `zoomTo` | `1.35` / `1.0` | Zoom range. |
| `collage.enabled` | `true` | Per-cell drift/fade motion in collages. |
| `collage.fadeInMs` | `600` | Cell fade-in time. |
| `collage.driftPx` | `40` | Max drift distance per cell. |
| `transitionMs` | `1200` | Scene-to-scene transition length. |
| `transition` | `"random"` | `random`, `crossfade`, `fadeblack`, `slide`, `zoom`. |
| `stillMs` | `5000` | Hold the finished frame still before the next scene (0–60 s). |
| `showBadge` | `true` | Show the game info card (title / playtime / HLTB / achievements). |

### `rendering` — GPU behavior

| Key | Default | Meaning |
|---|---|---|
| `targetFps` | `60` | Frame cap. |
| `vsync` | `true` | Sync to display refresh. |
| `blurBackground` | `true` | Blurred backdrop behind letterboxed images. |
| `shader` | `""` | Filename of a `.frag` post-process shader from `<config>\shaders\` (see below). |

### `enrichment` — optional online data

| Key | Default | Meaning |
|---|---|---|
| `steamGridDb.enabled` | `false` | Pull extra artwork from SteamGridDB. Needs a free API key. |
| `steamGridDb.apiKeyEnvVar` | `SLC_SGDB_KEY` | Name of the env var holding your SGDB key. |
| `hltb.enabled` | `true` | Resolve "how long to beat" durations in the background. |

HLTB lookups run on a background thread at ~3 requests/second, are cached
on disk (30-day negative cache), and never block rendering. Entries appear
on badges as they resolve.

### `advanced`

| Key | Default | Meaning |
|---|---|---|
| `logLevel` | `"info"` | `trace`/`debug`/`info`/`warn`/`error` (env: `SLC_LOG_LEVEL`). |
| `cacheDir` | auto | Override cache location. |
| `debugUi` | `false` | On-screen debug overlay. Builds with `SLC_WITH_IMGUI` show a full stats window; pass `--debug-ui` to enable it for one run. |

## Control channel

A running session (screensaver or live wallpaper) exposes three commands:

- Windows: `slc_host_win.exe --ctl STATUS` (also `RELOAD`, `SETCONFIG <json>`)
- Linux: `busctl --user call io.github.slc.Control /io/github/slc/Control io.github.slc.Control GetStatus` (`Reload`, `SetConfig s`)

`STATUS`/`GetStatus` returns a one-line JSON snapshot (state, scene, playlist, resident textures, decode queue, fps). `RELOAD`/`Reload` forces an immediate config re-read; `SETCONFIG`/`SetConfig` replaces `config.json` and reloads it.

## The badge

Each scene shows a card with the game's info, faded in once the motion
settles:

```
Game Title                     ← large, bold
12.4h played · last Nov 2024   ← grayed
How long to beat?: 27h         ← when resolved
3 out of 45 achievements       ← when Steam's local cache has stats
```

Rows that have no data are simply omitted (and the card shrinks).
Playtime/last-played come from `localconfig.vdf`; achievement counts come
from Steam's own stat caches under `appcache/stats/` — no API key needed.

## Custom shaders

Write a GLSL fragment shader, drop it in `<config>\shaders\`, select it in
Settings → Rendering. The rendered frame is passed to your shader once per
frame. A working `example_vignette.frag` is created automatically:

```glsl
#version 150
uniform sampler2D ciTex0;    // the rendered frame
uniform vec2 uResolution;    // framebuffer size, px
uniform float uTime;         // seconds since start
in vec2 vTexCoord;           // 0..1 across the frame
out vec4 oColor;
void main() { oColor = texture(ciTex0, vTexCoord); }
```

Shaders that fail to compile are logged and ignored (passthrough). The
config value is a filename only — paths are rejected.

## Tray menu

- **Wallpaper mode**: Off / Static / Live. *Static* swaps the desktop image
  on a timer via `slc_wallpaperd` (no GPU work). *Live* embeds the animated
  renderer behind the desktop icons.
- **Start now** / **Stop active**: trigger or kill the active session.
- **Enabled (system saver)**: toggle screensaver registration.
- **Settings…**: opens the configurator.
- **Exit**: restores your original wallpaper and quits.

## Environment variables

| Var | Use |
|---|---|
| `SLC_STEAM_ROOT` | Steam install override |
| `SLC_CONFIG_PATH` | config.json override |
| `SLC_LOG_LEVEL` | Log level override |
| `SLC_SGDB_KEY` | SteamGridDB API key |

## Troubleshooting

| Symptom | Check |
|---|---|
| No artwork | `--list-artwork` (on the host exe) dumps what discovery found; check `content.steamRoot`. |
| Badge missing | `animation.showBadge`, and the badge only fades in after motion settles. |
| No "How long to beat" row | `enrichment.hltb.enabled`; resolutions trickle in over time; check `cache\hltb.json`. |
| Black seams/lines in collage | Should be fixed by pixel snapping; if seen, report the `layout.strategy` value. |
| Tray won't start | Delete `%TEMP%\slc_tray.lock` if a killed instance left it behind; check the log. |
| High GPU use | Use wallpaper mode **Static** — live embedding is the only GPU-heavy part; stills/transitions can also be reduced via `stillMs`/`transitionMs`. |
