#pragma once

#include "slc/common/error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>

namespace slc::config {

// Mirrors doc section 5.7 (config.json model, schema: resources/config.schema.json).

struct ContentConfig {
    std::optional<std::filesystem::path> steamRoot; // null = auto-detect
    std::unordered_set<std::string> includeKinds{"hero", "grid", "header"};
    std::unordered_set<std::string> excludeAppIds{"228980"};
    std::uint32_t minImageWidth = 300;
    bool installedOnly = false; // true = only apps with a local manifest
};

struct LayoutConfig {
    // Scene mix: "focused" = single artwork only, "collage" = multi-image
    // layouts only, "matrix" = the scrolling Matrix only, "mix" = all of
    // them. The multi-image layout itself is always scene-random.
    std::string mode = "mix";
    int cellCount = 12;
    int cellCountMin = 4;              // lower bound when scenes randomize count
    int gutter = 12;
    float collageProbability = 0.5F;   // per-scene chance of collage vs single
    std::optional<std::uint64_t> seed; // nullopt = "random"
};

struct KenBurnsConfig {
    bool enabled = true;
    int durationMs = 8000;
    float zoomFrom = 1.35F;
    float zoomTo = 1.0F;
};

struct CollageAnimConfig {
    bool enabled = true;
    int fadeInMs = 600;
    int driftPx = 40;
};

struct AnimationConfig {
    KenBurnsConfig kenBurns;
    CollageAnimConfig collage;
    int transitionMs = 1200;
    // Still-hold after the scene animation finishes, before the next
    // transition: the game badge fades in during this window.
    int stillMs = 5000;
    bool showBadge = true;
    // random | crossfade | fadeblack | slide | zoom
    std::string transition = "random";
};

struct RenderingConfig {
    int targetFps = 60;
    bool vsync = true;
    bool blurBackground = true;
    /// Filename of a user fragment shader in <configDir>/shaders/, applied
    /// as a full-screen post-process pass. Empty = disabled.
    std::string shader;
};

struct EnrichmentConfig {
    bool steamGridDbEnabled = false;
    std::string apiKeyEnvVar = "SLC_SGDB_KEY";
    /// HowLongToBeat lookup for the badge's "hltb:" readout. Uses the
    /// site's unofficial search endpoint; results are disk-cached, so the
    /// feature degrades to "no hltb shown" when offline.
    bool hltbEnabled = true;
};

struct AdvancedConfig {
    std::string logLevel = "info";
    std::optional<std::filesystem::path> cacheDir;
    bool debugUi = false;
};

struct AppConfig {
    int version = 1;
    ContentConfig content;
    LayoutConfig layout;
    AnimationConfig animation;
    RenderingConfig rendering;
    EnrichmentConfig enrichment;
    AdvancedConfig advanced;
};

/**
 * Strict-but-tolerant parsing (doc section 5.7):
 *   - malformed JSON     -> default document, Error logged, defaults returned
 *   - unknown keys       -> warn log, ignored
 *   - missing keys       -> field default
 *   - version > current  -> migrated best-effort, warn
 * Never throws; defaults are always usable.
 */
[[nodiscard]] AppConfig loadConfig(const std::filesystem::path& path);

[[nodiscard]] Result<void> saveConfig(const std::filesystem::path& path,
                                      const AppConfig& cfg);

/** Default config path: $SLC_CONFIG_PATH or platform::configDir()/config.json. */
[[nodiscard]] std::filesystem::path defaultConfigPath();

} // namespace slc::config
