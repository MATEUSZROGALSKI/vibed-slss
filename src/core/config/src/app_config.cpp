#include "slc/config/app_config.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/platform/paths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <unordered_set>

namespace slc::config {
namespace {

using nlohmann::json;

constexpr int kCurrentVersion = 1;

void warnUnknown(const json& section, const std::unordered_set<std::string_view>& known,
                 std::string_view path)
{
    if (!section.is_object()) {
        return;
    }
    for (const auto& [key, value] : section.items()) {
        if (!known.contains(key)) {
            SLC_LOG_WARN("config: unknown key {}.{} ignored", path, key);
        }
    }
}

void readPath(const json& j, const char* key, std::optional<std::filesystem::path>& out)
{
    if (j.contains(key) && j[key].is_string()) {
        out = std::filesystem::path(j[key].get<std::string>());
    }
}

void readStringSet(const json& j, const char* key, std::unordered_set<std::string>& out)
{
    if (!j.contains(key)) {
        return;
    }
    out.clear();
    for (const auto& item : j[key]) {
        if (item.is_string()) {
            out.insert(item.get<std::string>());
        }
    }
}

template <class T>
void readScalar(const json& j, const char* key, T& out)
{
    if (j.contains(key)) {
        try {
            out = j[key].get<T>();
        } catch (const json::exception& e) {
            SLC_LOG_WARN("config: bad value for '{}': {}", key, e.what());
        }
    }
}

AppConfig fromJson(const json& doc)
{
    AppConfig cfg;
    warnUnknown(doc,
                {"$schema", "version", "content", "layout", "animation", "rendering",
                 "enrichment", "advanced"},
                "");
    readScalar(doc, "version", cfg.version);
    if (cfg.version > kCurrentVersion) {
        SLC_LOG_WARN("config: version {} newer than supported {}; reading best-effort",
                     cfg.version, kCurrentVersion);
    }

    if (doc.contains("content")) {
        const auto& s = doc["content"];
        warnUnknown(s, {"steamRoot", "includeKinds", "excludeAppIds",
                        "minImageWidth", "installedOnly"},
                    "content");
        readPath(s, "steamRoot", cfg.content.steamRoot);
        readStringSet(s, "includeKinds", cfg.content.includeKinds);
        readStringSet(s, "excludeAppIds", cfg.content.excludeAppIds);
        readScalar(s, "minImageWidth", cfg.content.minImageWidth);
        readScalar(s, "installedOnly", cfg.content.installedOnly);
    }

    if (doc.contains("layout")) {
        const auto& s = doc["layout"];
        // "strategy" stays a known key for backward compatibility — it is
        // accepted but ignored: the strategy is now always scene-random.
        warnUnknown(s,
                    {"strategy", "mode", "cellCount", "cellCountMin",
                     "gutter", "collageProbability", "seed"},
                    "layout");
        readScalar(s, "mode", cfg.layout.mode);
        readScalar(s, "cellCount", cfg.layout.cellCount);
        readScalar(s, "cellCountMin", cfg.layout.cellCountMin);
        readScalar(s, "gutter", cfg.layout.gutter);
        readScalar(s, "collageProbability", cfg.layout.collageProbability);
        if (s.contains("seed") && s["seed"].is_number_integer()) {
            cfg.layout.seed = s["seed"].get<std::uint64_t>();
        } // "random" or absent -> nullopt
    }

    if (doc.contains("animation")) {
        const auto& s = doc["animation"];
        warnUnknown(s, {"kenBurns", "collage", "transitionMs", "stillMs",
                        "showBadge", "transition"},
                    "animation");
        if (s.contains("kenBurns")) {
            const auto& kb = s["kenBurns"];
            warnUnknown(kb, {"enabled", "durationMs", "zoomFrom", "zoomTo"},
                        "animation.kenBurns");
            readScalar(kb, "enabled", cfg.animation.kenBurns.enabled);
            readScalar(kb, "durationMs", cfg.animation.kenBurns.durationMs);
            readScalar(kb, "zoomFrom", cfg.animation.kenBurns.zoomFrom);
            readScalar(kb, "zoomTo", cfg.animation.kenBurns.zoomTo);
        }
        if (s.contains("collage")) {
            const auto& c = s["collage"];
            warnUnknown(c, {"enabled", "fadeInMs", "driftPx"}, "animation.collage");
            readScalar(c, "enabled", cfg.animation.collage.enabled);
            readScalar(c, "fadeInMs", cfg.animation.collage.fadeInMs);
            readScalar(c, "driftPx", cfg.animation.collage.driftPx);
        }
        readScalar(s, "transitionMs", cfg.animation.transitionMs);
        readScalar(s, "stillMs", cfg.animation.stillMs);
        readScalar(s, "showBadge", cfg.animation.showBadge);
        readScalar(s, "transition", cfg.animation.transition);
    }

    if (doc.contains("rendering")) {
        const auto& s = doc["rendering"];
        warnUnknown(s, {"targetFps", "vsync", "blurBackground", "shader"},
                    "rendering");
        readScalar(s, "targetFps", cfg.rendering.targetFps);
        readScalar(s, "vsync", cfg.rendering.vsync);
        readScalar(s, "blurBackground", cfg.rendering.blurBackground);
        readScalar(s, "shader", cfg.rendering.shader);
    }

    if (doc.contains("enrichment")) {
        const auto& s = doc["enrichment"];
        warnUnknown(s, {"steamGridDb", "hltb"}, "enrichment");
        if (s.contains("steamGridDb")) {
            const auto& g = s["steamGridDb"];
            warnUnknown(g, {"enabled", "apiKeyEnvVar"}, "enrichment.steamGridDb");
            readScalar(g, "enabled", cfg.enrichment.steamGridDbEnabled);
            readScalar(g, "apiKeyEnvVar", cfg.enrichment.apiKeyEnvVar);
        }
        if (s.contains("hltb")) {
            const auto& g = s["hltb"];
            warnUnknown(g, {"enabled"}, "enrichment.hltb");
            readScalar(g, "enabled", cfg.enrichment.hltbEnabled);
        }
    }

    if (doc.contains("advanced")) {
        const auto& s = doc["advanced"];
        warnUnknown(s, {"logLevel", "cacheDir", "debugUi"}, "advanced");
        readScalar(s, "logLevel", cfg.advanced.logLevel);
        readPath(s, "cacheDir", cfg.advanced.cacheDir);
        readScalar(s, "debugUi", cfg.advanced.debugUi);
    }
    return cfg;
}

json toJson(const AppConfig& cfg)
{
    json doc;
    doc["version"] = cfg.version;

    doc["content"]["steamRoot"] =
        cfg.content.steamRoot ? json(cfg.content.steamRoot->generic_string()) : json(nullptr);
    doc["content"]["includeKinds"] = cfg.content.includeKinds;
    doc["content"]["excludeAppIds"] = cfg.content.excludeAppIds;
    doc["content"]["minImageWidth"] = cfg.content.minImageWidth;
    doc["content"]["installedOnly"] = cfg.content.installedOnly;

    doc["layout"]["mode"] = cfg.layout.mode;
    doc["layout"]["cellCount"] = cfg.layout.cellCount;
    doc["layout"]["cellCountMin"] = cfg.layout.cellCountMin;
    doc["layout"]["gutter"] = cfg.layout.gutter;
    doc["layout"]["collageProbability"] = cfg.layout.collageProbability;
    doc["layout"]["seed"] = cfg.layout.seed ? json(*cfg.layout.seed) : json("random");

    doc["animation"]["kenBurns"] = {
        {"enabled", cfg.animation.kenBurns.enabled},
        {"durationMs", cfg.animation.kenBurns.durationMs},
        {"zoomFrom", cfg.animation.kenBurns.zoomFrom},
        {"zoomTo", cfg.animation.kenBurns.zoomTo},
    };
    doc["animation"]["collage"] = {
        {"enabled", cfg.animation.collage.enabled},
        {"fadeInMs", cfg.animation.collage.fadeInMs},
        {"driftPx", cfg.animation.collage.driftPx},
    };
    doc["animation"]["transitionMs"] = cfg.animation.transitionMs;
    doc["animation"]["stillMs"] = cfg.animation.stillMs;
    doc["animation"]["showBadge"] = cfg.animation.showBadge;
    doc["animation"]["transition"] = cfg.animation.transition;

    doc["rendering"] = {
        {"targetFps", cfg.rendering.targetFps},
        {"vsync", cfg.rendering.vsync},
        {"blurBackground", cfg.rendering.blurBackground},
        {"shader", cfg.rendering.shader},
    };
    doc["enrichment"]["steamGridDb"] = {
        {"enabled", cfg.enrichment.steamGridDbEnabled},
        {"apiKeyEnvVar", cfg.enrichment.apiKeyEnvVar},
    };
    doc["enrichment"]["hltb"] = {
        {"enabled", cfg.enrichment.hltbEnabled},
    };
    doc["advanced"] = {
        {"logLevel", cfg.advanced.logLevel},
        {"cacheDir",
         cfg.advanced.cacheDir ? json(cfg.advanced.cacheDir->generic_string())
                               : json(nullptr)},
        {"debugUi", cfg.advanced.debugUi},
    };
    return doc;
}

} // namespace

std::filesystem::path defaultConfigPath()
{
    if (auto fromEnv = env::getPath(env::kConfigPathVar)) {
        return *fromEnv;
    }
    return platform::configDir() / "config.json";
}

AppConfig loadConfig(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in) {
        SLC_LOG_INFO("config: {} not found, using defaults", path.string());
        return AppConfig{};
    }
    try {
        json doc;
        in >> doc;
        return fromJson(doc);
    } catch (const json::exception& e) {
        SLC_LOG_ERROR("config: malformed JSON in {} ({}), using defaults", path.string(),
                      e.what());
        return AppConfig{};
    }
}

Result<void> saveConfig(const std::filesystem::path& path, const AppConfig& cfg)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, "cannot write " + path.string()));
    }
    out << toJson(cfg).dump(2) << '\n';
    return {};
}

} // namespace slc::config
