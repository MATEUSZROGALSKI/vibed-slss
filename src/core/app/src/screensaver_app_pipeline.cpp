// ScreensaverApp content pipeline — Steam/SGDB discovery, the background
// decode worker feeding TexturePool, and the HowLongToBeat resolver.
// Split from screensaver_app.cpp; class layout lives in
// slc/app/screensaver_app.h.
#include "slc/app/screensaver_app.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/content/content_service.h"
#include "slc/content/hltb.h"
#include "slc/content/steam_local_provider.h"
#include "slc/content/steamgriddb_provider.h"
#include "slc/platform/media.h"
#include "slc/platform/paths.h"

#include "transport_factory.h"

#include <cinder/app/App.h>
#include <cinder/ImageIo.h>
#include <cinder/ip/Resize.h>

#include <algorithm>
#include <chrono>
#include <thread>

namespace slc::app {

namespace {

// Best single piece per game: hero > grid > header > logo > icon.
int kindRank(content::ArtworkKind kind)
{
    switch (kind) {
    case content::ArtworkKind::Hero:   return 0;
    case content::ArtworkKind::Grid:   return 1;
    case content::ArtworkKind::Header: return 2;
    case content::ArtworkKind::Logo:   return 3;
    case content::ArtworkKind::Icon:   return 4;
    }
    return 5;
}

} // namespace

void ScreensaverApp::startDiscovery()
{
    const config::AppConfig cfg = m_config;
    m_worker = std::jthread([this, cfg](std::stop_token st) {
        platform::initImageDecodeThread();

        content::ContentService service;
        service.addProvider(std::make_unique<content::SteamLocalProvider>());

        content::DiscoveryContext ctx;
        ctx.steamRootOverride = cfg.content.steamRoot;
        ctx.excludeAppIds = cfg.content.excludeAppIds;
        for (const auto& kind : cfg.content.includeKinds) {
            if (auto k = content::artworkKindFromString(kind)) {
                ctx.includeKinds.insert(*k);
            }
        }
        ctx.minImageWidth = cfg.content.minImageWidth;
        ctx.installedOnly = cfg.content.installedOnly;
        ctx.stopToken = st;

        if (cfg.enrichment.steamGridDbEnabled) {
            if (auto transport = enrichment::makeHttpTransport()) {
                content::SgdbOptions opt;
                opt.apiKey = env::get(cfg.enrichment.apiKeyEnvVar)
                                 .value_or("");
                opt.cacheDir =
                    cfg.advanced.cacheDir.value_or(platform::cacheDir());
                opt.includeKinds = ctx.includeKinds;
                service.addProvider(
                    std::make_unique<content::SteamGridDbProvider>(
                        std::move(transport), std::move(opt)));
            } else {
                SLC_LOG_WARN("sgdb enrichment enabled but no HTTP transport "
                             "(built without SLC_WITH_CPR)");
            }
        }

        auto result = service.collect(ctx);
        if (result && !st.stop_requested()) {
            // Keep every artwork kind per app — the slot aspect picks the
            // variant that fits it best at scene-build time.
            std::unordered_map<std::string, PlaylistEntry> byApp;
            for (const auto& art : *result) {
                auto& e = byApp[art.appId];
                e.variants.push_back(art);
            }
            std::vector<PlaylistEntry> picks;
            picks.reserve(byApp.size());
            for (auto& [id, e] : byApp) {
                auto& v = e.variants;
                std::sort(v.begin(), v.end(),
                          [](const content::Artwork& a,
                             const content::Artwork& b) {
                              return kindRank(a.kind) < kindRank(b.kind);
                          });
                e.pick(0.0F); // default to the best-ranked variant
                picks.push_back(std::move(e));
            }
            std::shuffle(picks.begin(), picks.end(), m_rng);
            {
                std::lock_guard lock(m_playlistMutex);
                m_playlist = std::move(picks);
            }
        }
        m_discoveryDone = true;

        // HowLongToBeat: apply cached times immediately, then resolve the
        // rest on a paced background thread (startHltbResolve).
        if (cfg.enrichment.hltbEnabled && !st.stop_requested()) {
            startHltbResolve();
        }
    });
}

void ScreensaverApp::spawnDecodeWorker()
{
    // Detached: never joined anywhere — a stuck ci::loadImage (corrupt or
    // pathologically large artwork) leaves a zombie, not a wedge. The
    // watchdog in update() marks the stuck key failed and spawns a
    // replacement so the queue keeps draining.
    const int workerId = ++m_decodeWorkerSeq;
    std::thread([this, workerId] {
        platform::initImageDecodeThread();
        while (!m_decodeStop.load()) {
            PlaylistEntry entry;
            {
                std::unique_lock lock(m_decodeMutex);
                m_decodeCv.wait_for(
                    lock, std::chrono::milliseconds(200), [&] {
                        return m_decodeStop.load() ||
                               !m_decodeQueue.empty();
                    });
                if (m_decodeStop.load() || m_decodeQueue.empty()) {
                    continue;
                }
                entry = std::move(m_decodeQueue.front());
                m_decodeQueue.pop_front();
            }
            {
                std::lock_guard lock(m_decodeActMutex);
                m_decodeActiveKey = entry.key;
                m_decodeActiveSince =
                    std::chrono::steady_clock::now();
                m_decodeActiveWorker = workerId;
            }
            try {
                ci::Surface surface = ci::loadImage(entry.path);
                if (entry.maxDim > 0) {
                    const int w = surface.getWidth();
                    const int h = surface.getHeight();
                    const int longest = std::max(w, h);
                    if (longest > static_cast<int>(entry.maxDim)) {
                        const float s =
                            static_cast<float>(entry.maxDim) / longest;
                        surface = ci::ip::resizeCopy(
                            surface, surface.getBounds(),
                            ci::ivec2(static_cast<int>(w * s),
                                      static_cast<int>(h * s)));
                    }
                }
                // The UI thread may swap m_pool during config restarts —
                // hold m_poolMutex so the pool can't be destroyed under us.
                std::lock_guard poolLock(m_poolMutex);
                if (m_pool) {
                    m_pool->enqueueDecoded(entry.key, std::move(surface));
                }
            } catch (const std::exception& e) {
                SLC_LOG_WARN("decode failed for {}: {}",
                             entry.path.string(), e.what());
                std::lock_guard lock(m_failedMutex);
                m_failed.insert(entry.key);
            }
            {
                std::lock_guard lock(m_decodeActMutex);
                if (m_decodeActiveWorker == workerId) {
                    m_decodeActiveKey.clear();
                    m_decodeActiveWorker = -1;
                }
            }
        }
    }).detach();
}

void ScreensaverApp::startHltbResolve()
{
    std::lock_guard hlock(m_hltbMutex);
    if (m_hltbThread.joinable()) {
        return; // already running for this playlist
    }
    const config::AppConfig cfg = m_config;
    content::HltbResolver resolver(
        enrichment::makeHttpTransport(),
        cfg.advanced.cacheDir.value_or(platform::cacheDir()));
    std::vector<std::pair<std::string, std::string>> pending;
    std::size_t cached = 0;
    {
        std::lock_guard lock(m_playlistMutex);
        for (auto& e : m_playlist) {
            if (const auto v = resolver.lookup(e.appId)) {
                e.hltbMin = *v;
                ++cached;
            } else if (resolver.needsResolve(e.appId) &&
                       !e.gameName.empty()) {
                pending.emplace_back(e.appId, e.gameName);
            }
        }
    }
    SLC_LOG_INFO("hltb: {} cached, {} to resolve", cached, pending.size());
    if (pending.empty()) {
        return;
    }
    m_hltbThread = std::jthread(
        [this, pending = std::move(pending), resolver = std::move(resolver)](
            std::stop_token hst) mutable {
            int sinceSave = 0;
            for (const auto& [appId, name] : pending) {
                if (hst.stop_requested()) {
                    break;
                }
                if (const auto mins = resolver.resolve(appId, name, hst)) {
                    std::lock_guard lock(m_playlistMutex);
                    for (auto& e : m_playlist) {
                        if (e.appId == appId) {
                            e.hltbMin = *mins;
                        }
                    }
                }
                // Persist periodically so a kill doesn't discard a whole
                // session's resolutions.
                if (++sinceSave >= 32) {
                    sinceSave = 0;
                    resolver.save();
                }
                // ~3 lookups/sec: polite to a free community site.
                for (int i = 0; i < 33 && !hst.stop_requested(); ++i) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(10));
                }
            }
            resolver.save();
        });
}

void ScreensaverApp::issueDecodeWindow()
{
    if (!m_pool || !m_discoveryDone) {
        return;
    }
    refillPickQueue();

    std::vector<PlaylistEntry> want;
    std::vector<PlaylistEntry> priority; // plan keys — jump the decode queue
    if (collage() && !m_cells.empty()) {
        want.reserve(m_cells.size());
        for (const auto& c : m_cells) {
            want.push_back(c.entry);
        }
    } else if (m_scene == SceneKind::Matrix) {
        for (const auto& c : m_matrix.cells) {
            want.push_back(c.entry);
        }
    } else if (!m_currentKey.empty()) {
        want.push_back(m_curEntry);
    }

    if (m_next) {
        // The planned scene outranks everything else: its textures must be
        // resident before the transition starts, so it decodes ahead of
        // any queue-front prefetch backlog.
        if (m_next->kind == SceneKind::Collage) {
            for (const auto& c : m_next->cells) {
                priority.push_back(c.entry);
            }
        } else if (m_next->kind == SceneKind::Matrix) {
            for (const auto& c : m_next->matrix.cells) {
                priority.push_back(c.entry);
            }
        } else {
            priority.push_back(m_next->entry);
        }
    } else {
        // Nothing planned yet — prefetch the front of the pick queue so a
        // resident candidate exists when the next plan forms.
        const ci::Rectf win(getWindowBounds());
        const float winAspect =
            win.getWidth() / std::max(1.0F, win.getHeight());
        const float winMax = std::max(win.getWidth(), win.getHeight());
        // Decode cap: window longest side x1.4 covers the Ken Burns zoom
        // overscan; still capped for very large source files.
        const auto singleMax = static_cast<std::uint32_t>(
            std::clamp(winMax * 1.4F, 1024.0F, 4096.0F));
        for (std::size_t i = 0;
             i < std::min<std::size_t>(kDecodeAhead, m_picks.size());
             ++i) {
            auto e = playlistEntry(m_picks.at(i));
            e.pick(winAspect, winMax);
            e.maxDim = singleMax;
            want.push_back(std::move(e));
        }
    }
    for (auto& entry : priority) {
        if (entry.key.empty() || m_pool->isResident(entry.key) ||
            isFailed(entry.key)) {
            continue;
        }
        if (m_inflight.contains(entry.key)) {
            // Already queued from earlier prefetch — bump it to the front
            // so the upcoming scene decodes before the backlog.
            std::lock_guard lock(m_decodeMutex);
            const auto it = std::find_if(
                m_decodeQueue.begin(), m_decodeQueue.end(),
                [&](const PlaylistEntry& q) { return q.key == entry.key; });
            if (it != m_decodeQueue.end()) {
                auto e = std::move(*it);
                m_decodeQueue.erase(it);
                m_decodeQueue.push_front(std::move(e));
            }
            continue;
        }
        m_inflight.insert(entry.key);
        m_inflightSince.emplace(entry.key,
                                std::chrono::steady_clock::now());
        {
            std::lock_guard lock(m_decodeMutex);
            m_decodeQueue.push_front(std::move(entry));
        }
        m_decodeCv.notify_one();
    }
    for (auto& entry : want) {
        if (entry.key.empty() || m_pool->isResident(entry.key) ||
            m_inflight.contains(entry.key) || isFailed(entry.key)) {
            continue;
        }
        m_inflight.insert(entry.key);
        m_inflightSince.emplace(entry.key,
                                std::chrono::steady_clock::now());
        {
            std::lock_guard lock(m_decodeMutex);
            m_decodeQueue.push_back(std::move(entry));
        }
        m_decodeCv.notify_one();
    }
}

void ScreensaverApp::restartDiscovery()
{
    SLC_LOG_INFO("config: content settings changed — restarting discovery");
    {
        std::lock_guard hlock(m_hltbMutex);
        if (m_hltbThread.joinable()) {
            m_hltbThread.request_stop();
            m_hltbThread.join();
        }
    }
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join(); // discovery only — decode workers are detached
    }
    {
        std::lock_guard lock(m_playlistMutex);
        m_playlist.clear();
    }
    {
        std::lock_guard lock(m_decodeMutex);
        m_decodeQueue.clear();
    }
    {
        std::lock_guard lock(m_failedMutex);
        m_failed.clear();
    }
    m_inflight.clear();
    m_inflightSince.clear();
    m_picks.reset();
    m_next.reset();
    m_cells.clear();
    m_prevCells.clear();
    m_choreo.reset();
    m_prevChoreo.reset();
    m_curAnim.reset();
    m_prevAnim.reset();
    m_curSpin.reset();
    m_prevSpin.reset();
    m_currentKey.clear();
    m_prevKey.clear();
    m_curEntry = {};
    m_prevEntry = {};
    {
        std::lock_guard poolLock(m_poolMutex);
        m_pool = std::make_unique<render::TexturePool>(
            static_cast<std::size_t>(std::max(3, m_config.layout.cellCount)));
    }
    m_discoveryDone = false;
    m_state = State::Loading;
    m_loadingElapsedSec = 0.0;
    m_dwellElapsedSec = 0.0;
    m_fade = 1.0F;
    m_settled = false;
    startDiscovery();
}

} // namespace slc::app
