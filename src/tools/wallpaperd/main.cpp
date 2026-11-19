// slc_wallpaperd — M7 wallpaper shuffle companion (doc 6.1.4).
//
//   slc_wallpaperd [--once] [--interval <minutes>] [--seed <u64>]
//
// Opt-in daemon: shares SteamLocalProvider discovery with the screensaver,
// shuffles hero/header artwork, applies it via the platform backend
// (Windows: IDesktopWallpaper). Never modifies Steam files; exits 0 when
// the platform backend is unavailable or the library is empty.
#include "slc/wallpaper/backend.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/config/app_config.h"
#include "slc/content/artwork.h"
#include "slc/content/content_service.h"
#include "slc/content/steam_local_provider.h"
#include "slc/platform/paths.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int kindRank(slc::content::ArtworkKind k)
{
    using slc::content::ArtworkKind;
    switch (k) {
    case ArtworkKind::Hero:   return 0;
    case ArtworkKind::Header: return 1;
    case ArtworkKind::Grid:   return 2;
    default:                  return 3;
    }
}

} // namespace

int main(int argc, char** argv)
{
    using namespace std::chrono_literals;

    bool once = false;
    auto interval = 30min;
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--once") {
            once = true;
        } else if (a == "--interval" && i + 1 < argc) {
            interval = std::chrono::minutes(std::strtol(argv[++i], nullptr, 10));
        } else if (a == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        } else {
            std::fprintf(stderr,
                         "usage: %s [--once] [--interval <min>] "
                         "[--seed <u64>]\n",
                         argv[0]);
            return 64;
        }
    }

    slc::logging::initFileLogging(slc::platform::logDir());
    slc::logging::applyLevelFromEnv();

    auto backend = slc::wallpaper::makeWallpaperBackend();
    if (!backend) {
        SLC_LOG_WARN("wallpaperd: no backend for this platform, exiting");
        return 0;
    }

    const auto cfg = slc::config::loadConfig(slc::config::defaultConfigPath());

    slc::content::ContentService service;
    service.addProvider(std::make_unique<slc::content::SteamLocalProvider>());
    slc::content::DiscoveryContext ctx;
    ctx.steamRootOverride = cfg.content.steamRoot;
    ctx.excludeAppIds = cfg.content.excludeAppIds;
    for (const auto& k : cfg.content.includeKinds) {
        if (auto kind = slc::content::artworkKindFromString(k)) {
            ctx.includeKinds.insert(*kind);
        }
    }
    ctx.minImageWidth = cfg.content.minImageWidth;
    ctx.installedOnly = cfg.content.installedOnly;

    auto result = service.collect(ctx);
    if (!result || result->empty()) {
        SLC_LOG_WARN("wallpaperd: no artwork discovered, exiting");
        return 0;
    }

    // Wide artwork preferred; best-ranked kind per app.
    std::vector<slc::content::Artwork> pool;
    for (const auto& a : *result) {
        if (kindRank(a.kind) <= 2) {
            pool.push_back(a);
        }
    }
    if (pool.empty()) {
        SLC_LOG_WARN("wallpaperd: no usable artwork, exiting");
        return 0;
    }

    std::mt19937 rng(static_cast<std::mt19937::result_type>(seed));
    std::shuffle(pool.begin(), pool.end(), rng);
    SLC_LOG_INFO("wallpaperd: {} candidates, interval {} min",
                 pool.size(),
                 std::chrono::duration_cast<std::chrono::minutes>(interval)
                     .count());

    // Deadline-based loop: sleep in short slices so suspend/resume just
    // fires the pending shuffle once instead of drift-accumulating.
    std::size_t i = 0;
    for (;;) {
        const auto& art = pool[i % pool.size()];
        if (auto r = backend->setWallpaper(art.path); r) {
            SLC_LOG_INFO("wallpaperd: {} -> {}", art.appId,
                         art.path.filename().string());
        } else {
            SLC_LOG_WARN("wallpaperd: set failed for {}: {}",
                         art.path.string(), r.error().message);
        }
        ++i;
        if (once) {
            return 0;
        }
        const auto wake = std::chrono::steady_clock::now() + interval;
        while (std::chrono::steady_clock::now() < wake) {
            std::this_thread::sleep_for(5s);
        }
    }
}
