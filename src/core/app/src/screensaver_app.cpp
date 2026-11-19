// ScreensaverApp lifecycle — setup/cleanup, per-frame update, draw
// dispatch, input handling, and config reload. Rendering lives in
// screensaver_app_draw.cpp, scene planning/picking in
// screensaver_app_scenes.cpp, and the discovery/decode pipeline in
// screensaver_app_pipeline.cpp.
#include "slc/app/screensaver_app.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/platform/paths.h"

#include <cinder/app/App.h>
#include <cinder/app/Window.h>
#include <cinder/gl/draw.h>
#include <cinder/gl/gl.h>
#include <cinder/gl/scoped.h>
#include <cinder/ImageIo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <random>
#include <unordered_set>
#include <utility>

namespace slc::app {

namespace {

constexpr double kLoadingBudgetSec = 1.5;
constexpr float kUploadBudgetMs = 8.0F;
constexpr float kUploadBudgetPreviewMs = 14.0F;
constexpr double kDecodeStuckSec = 20.0; // watchdog: decode item deadline

std::function<std::unique_ptr<IPlatformHost>()>& hostFactorySlot()
{
    static std::function<std::unique_ptr<IPlatformHost>()> slot;
    return slot;
}

} // namespace

void ScreensaverApp::setHostFactory(
    std::function<std::unique_ptr<IPlatformHost>()> factory)
{
    hostFactorySlot() = std::move(factory);
}

namespace {
bool& ignoreReducedMotionSlot()
{
    static bool slot = false;
    return slot;
}

bool& debugUiSlot()
{
    static bool slot = false;
    return slot;
}

std::function<void()>& frameEndHookSlot()
{
    static std::function<void()> slot;
    return slot;
}

// Fires the frame-end hook when draw() leaves scope by any path.
struct FrameEndGuard {
    ~FrameEndGuard()
    {
        if (frameEndHookSlot()) {
            frameEndHookSlot()();
        }
    }
};
} // namespace

void ScreensaverApp::setIgnoreReducedMotion(bool ignore)
{
    ignoreReducedMotionSlot() = ignore;
}

void ScreensaverApp::setDebugUi(bool enabled)
{
    debugUiSlot() = enabled;
}

void ScreensaverApp::setFrameEndHook(std::function<void()> hook)
{
    frameEndHookSlot() = std::move(hook);
}

ScreensaverApp::ScreensaverApp()
{
    if (hostFactorySlot()) {
        m_host = hostFactorySlot()();
    }
}

ScreensaverApp::ScreensaverApp(std::unique_ptr<IPlatformHost> host)
    : m_host(std::move(host))
{
}

void ScreensaverApp::setup()
{
    m_configPath = config::defaultConfigPath();
    m_config = config::loadConfig(m_configPath);
    {
        std::error_code ec;
        m_configMtime = std::filesystem::last_write_time(m_configPath, ec);
    }
    const auto seed =
        m_config.layout.seed.value_or(std::random_device{}());
    m_rng.seed(static_cast<std::mt19937::result_type>(seed));
    m_reducedMotion =
        platform::prefersReducedMotion() && !ignoreReducedMotionSlot();

    SLC_LOG_INFO("screensaver setup (preview={}, reducedMotion={})",
                 m_host && m_host->isPreviewMode(), m_reducedMotion);
    m_timeline = ci::Timeline::create();
    // Capacity must cover a full collage AND a full matrix scene at once
    // (prev + current during a transition) — a scene that can't fit in
    // the pool can never reach planResident and stalls the advance loop.
    const auto poolCap = static_cast<std::size_t>(std::max(
        3, m_config.layout.cellCount +
               static_cast<int>(matrixGeom().cellCount())));
    m_pool = std::make_unique<render::TexturePool>(poolCap);

    if (m_config.animation.showBadge) {
        ensureBadgeFonts();
    }
    if ((debugUiSlot() || m_config.advanced.debugUi) &&
        DebugUi::available()) {
        m_debugUi = std::make_unique<DebugUi>();
        m_debugUi->attach(getWindow());
    } else if (debugUiSlot() || m_config.advanced.debugUi) {
        SLC_LOG_WARN("debug-ui requested but this build lacks SLC_WITH_IMGUI");
    }
    loadUserShader();
    spawnDecodeWorker();
    startDiscovery();
}

void ScreensaverApp::pollConfigReload()
{
    std::error_code ec;
    const auto mt = std::filesystem::last_write_time(m_configPath, ec);
    if (ec || mt == m_configMtime) {
        return;
    }
    m_configMtime = mt;
    auto next = config::loadConfig(m_configPath);
    SLC_LOG_INFO("config changed on disk — applying");
    applyConfig(std::move(next));
}

void ScreensaverApp::applyConfig(config::AppConfig next)
{
    const auto& prev = m_config;
    const bool contentChanged =
        prev.content.steamRoot != next.content.steamRoot ||
        prev.content.includeKinds != next.content.includeKinds ||
        prev.content.excludeAppIds != next.content.excludeAppIds ||
        prev.content.minImageWidth != next.content.minImageWidth ||
        prev.content.installedOnly != next.content.installedOnly ||
        prev.enrichment.steamGridDbEnabled !=
            next.enrichment.steamGridDbEnabled ||
        prev.enrichment.apiKeyEnvVar != next.enrichment.apiKeyEnvVar;
    const bool layoutChanged =
        prev.layout.cellCount != next.layout.cellCount ||
        prev.layout.cellCountMin != next.layout.cellCountMin ||
        prev.layout.gutter != next.layout.gutter ||
        prev.layout.mode != next.layout.mode ||
        prev.layout.collageProbability != next.layout.collageProbability ||
        prev.layout.seed != next.layout.seed;
    const bool animChanged =
        prev.animation.transition != next.animation.transition ||
        prev.animation.transitionMs != next.animation.transitionMs ||
        prev.animation.stillMs != next.animation.stillMs ||
        prev.animation.kenBurns.enabled != next.animation.kenBurns.enabled ||
        prev.animation.kenBurns.durationMs !=
            next.animation.kenBurns.durationMs ||
        prev.animation.collage.enabled != next.animation.collage.enabled ||
        prev.animation.collage.fadeInMs != next.animation.collage.fadeInMs ||
        prev.animation.collage.driftPx != next.animation.collage.driftPx;
    const bool shaderChanged =
        prev.rendering.shader != next.rendering.shader;
    const bool badgeOn = !prev.animation.showBadge &&
                         next.animation.showBadge;
    const bool hltbOn = !prev.enrichment.hltbEnabled &&
                        next.enrichment.hltbEnabled;
    const bool cellCountChanged =
        prev.layout.cellCount != next.layout.cellCount;
    const bool seedChanged = prev.layout.seed != next.layout.seed;

    if (prev.advanced.logLevel != next.advanced.logLevel) {
        spdlog::set_level(
            spdlog::level::from_str(next.advanced.logLevel));
    }
    if (prev.rendering.targetFps != next.rendering.targetFps ||
        prev.rendering.vsync != next.rendering.vsync) {
        SLC_LOG_INFO("config: targetFps/vsync apply on restart");
    }

    m_config = std::move(next);

    if (seedChanged) {
        const auto seed =
            m_config.layout.seed.value_or(std::random_device{}());
        m_rng.seed(static_cast<std::mt19937::result_type>(seed));
    }
    if (cellCountChanged && m_pool) {
        std::lock_guard poolLock(m_poolMutex);
        m_pool = std::make_unique<render::TexturePool>(
            static_cast<std::size_t>(std::max(
                3, m_config.layout.cellCount +
                       static_cast<int>(matrixGeom().cellCount()))));
    }
    if (badgeOn) {
        ensureBadgeFonts();
    }
    if (shaderChanged) {
        loadUserShader();
    }
    if (layoutChanged || animChanged) {
        // A plan made under the old settings would show stale layout/timing.
        m_next.reset();
    }
    if (contentChanged) {
        restartDiscovery();
        return;
    }
    if (hltbOn && m_discoveryDone) {
        startHltbResolve();
    }
}

void ScreensaverApp::resize()
{
    m_cells.clear(); // rebuilt lazily on next update
    m_choreo.reset();
    m_matrix = MatrixScene{};
    m_matrixChoreo.reset();
    // Old-geometry snapshots and plans can't transition cleanly — drop them.
    m_prevCells.clear();
    m_prevChoreo.reset();
    m_prevMatrix = MatrixScene{};
    m_prevMatrixChoreo.reset();
    m_next.reset();
    m_prevKey.clear();
    m_prevAnim.reset();
    m_prevSpin.reset();
    m_fade = 1.0F;
}

void ScreensaverApp::update()
{
    const double now = getElapsedSeconds();
    const double dt = std::max(0.0, now - m_lastElapsedSec);
    m_lastElapsedSec = now;
    m_loadingElapsedSec += dt;

    // Timelines freeze while the scene sits in its still window.
    if (m_timeline && !m_settled) {
        m_timeline->step(static_cast<float>(dt));
    }
    if (m_choreo && !m_settled) {
        m_choreo->step(static_cast<float>(dt));
    }
    // The Matrix marquee scrolls continuously — through the still window
    // too, since the scroll IS the scene (only the transition freeze is
    // handled by the outgoing-scene snapshot).
    if (m_scene == SceneKind::Matrix && m_matrixChoreo) {
        m_matrixChoreo->step(static_cast<float>(dt));
        const auto mxPhase = m_matrixChoreo->phase();
        // Arm the matrix→matrix flush while the hero holds: as soon as
        // the planned matrix is resident the choreographer will race the
        // columns at hold end instead of zooming straight out. Checked
        // every hold frame so a late-arriving plan can still arm.
        if (mxPhase == anim::MatrixChoreographer::Phase::Hero &&
            !m_heroDecided && m_next &&
            m_next->kind == SceneKind::Matrix && planResident(*m_next)) {
            m_matrixChoreo->setFlushAtHoldEnd(true);
            m_heroDecided = true;
            m_flushActive = true;
            m_flushDone.clear();
            m_flushEntries.clear();
            for (auto& c : m_next->matrix.cells) {
                m_flushEntries.push_back(std::move(c.entry));
            }
            const auto shown = std::move(m_next->indices);
            m_next.reset();
            ++m_sceneIndex;
            m_dwellElapsedSec = 0.0;
            markShown(shown);
            SLC_LOG_INFO("scene {}: matrix flush armed ({} entries)",
                         m_sceneIndex, m_flushEntries.size());
        }
        // Flush churn: cells that scroll fully out of view take the next
        // planned entry — the marquee reseeds into the new scene without
        // any crossfade. The hero cell is skipped: its slot stays empty
        // so the zoomed card docks back into it.
        if (mxPhase == anim::MatrixChoreographer::Phase::Flush &&
            m_flushActive && !m_flushEntries.empty()) {
            const ci::Rectf win(getWindowBounds());
            for (std::size_t i = 0;
                 i < m_matrix.cells.size() && !m_flushEntries.empty();
                 ++i) {
                if (m_flushDone.count(i) != 0 ||
                    i == m_matrixChoreo->heroCell()) {
                    continue;
                }
                auto& cell = m_matrix.cells[i];
                if (cell.column >= m_matrix.columns.size()) {
                    continue;
                }
                const auto& col = m_matrix.columns[cell.column];
                float y = std::fmod(cell.y + col.offset, col.stackH);
                if (y < 0.0F) {
                    y += col.stackH;
                }
                const float top = y - m_matrix.pitch;
                if (top + cell.h < win.y1 || top > win.y2) {
                    cell.entry = m_flushEntries.front();
                    m_flushEntries.pop_front();
                    m_flushDone.insert(i);
                }
            }
        }
        if (mxPhase == anim::MatrixChoreographer::Phase::ZoomOut &&
            !m_heroDecided) {
            // Hold ended without an armed flush — the fallback decision
            // point: swap the marquee under the zoomed art when the next
            // scene is another matrix, or let the normal advance run off
            // the frozen hero frame for any other layout.
            m_heroDecided = true;
            decideHeroEnd();
        } else if (mxPhase == anim::MatrixChoreographer::Phase::Scroll) {
            m_heroDecided = false;
            m_heroSwapActive = false;
            m_heroSwapTex.reset();
            m_flushActive = false;
            m_flushEntries.clear();
            m_flushDone.clear();
        }
        if (mxPhase == anim::MatrixChoreographer::Phase::Scroll &&
            m_matrixChoreo->wantsHero() && !m_matrix.cells.empty()) {
            // Zoom a cell sitting near the middle of the screen — the
            // hero reveal reads far better there than at the edges.
            const ci::Rectf win(getWindowBounds());
            const float mx1 = win.getX1() + win.getWidth() * 0.30F;
            const float mx2 = win.getX1() + win.getWidth() * 0.70F;
            const float my1 = win.getY1() + win.getHeight() * 0.25F;
            const float my2 = win.getY1() + win.getHeight() * 0.75F;
            std::vector<std::size_t> cands;
            for (std::size_t i = 0; i < m_matrix.cells.size(); ++i) {
                const auto& cell = m_matrix.cells[i];
                if (cell.entry.key.empty() ||
                    cell.column >= m_matrix.columns.size() ||
                    !m_pool->isResident(cell.entry.key)) {
                    continue;
                }
                const auto& col = m_matrix.columns[cell.column];
                float y = std::fmod(cell.y + col.offset, col.stackH);
                if (y < 0.0F) {
                    y += col.stackH;
                }
                const float cy = y - m_matrix.pitch + cell.h * 0.5F;
                const float cx = (col.x1 + col.x2) * 0.5F;
                if (cx >= mx1 && cx <= mx2 && cy >= my1 && cy <= my2) {
                    cands.push_back(i);
                }
            }
            if (!cands.empty()) {
                std::uniform_int_distribution<std::size_t> pick(
                    0, cands.size() - 1);
                const auto ci = cands[pick(m_rng)];
                m_matrixChoreo->beginHero(m_matrix.cells[ci].column, ci);
            }
            // No candidate this frame: wantsHero stays true and we retry
            // as the marquee scrolls a middle-cell into place.
        }
    }

    m_frameTimes.push_back(static_cast<float>(dt));
    while (m_frameTimes.size() > 240) {
        m_frameTimes.pop_front();
    }

    // Live settings: poll config.json roughly once a second. Same tick
    // runs the decode watchdog — a ci::loadImage stuck past the deadline
    // gets its key marked failed and a replacement worker spawned.
    m_configCheckSec += dt;
    if (m_configCheckSec >= 1.0) {
        m_configCheckSec = 0.0;
        pollConfigReload();
        std::size_t decodeQ = 0;
        {
            std::lock_guard lock(m_decodeMutex);
            decodeQ = m_decodeQueue.size();
        }
        // ~60s heartbeat: proves the render loop is alive in logs even
        // while the scene machine is parked in a plan-wait.
        if (++m_heartbeatTicks >= 60) {
            m_heartbeatTicks = 0;
            SLC_LOG_INFO(
                "tick: scene={} fade={:.2f} resident={} uploadQ={} "
                "decodeQ={} inflight={} zombies={}",
                m_sceneIndex, m_fade, m_pool->residentCount(),
                m_pool->queuedCount(), decodeQ, m_inflight.size(),
                m_decodeZombies.load());
        }
        {
            std::lock_guard lock(m_decodeActMutex);
            if (!m_decodeActiveKey.empty()) {
                const double busySec =
                    std::chrono::duration<double>(
                        std::chrono::steady_clock::now() -
                        m_decodeActiveSince)
                        .count();
                if (busySec > kDecodeStuckSec) {
                    SLC_LOG_WARN(
                        "decode stuck {:.0f}s on '{}' — marking failed, "
                        "spawning worker #{}",
                        busySec, m_decodeActiveKey,
                        m_decodeWorkerSeq.load() + 1);
                    {
                        std::lock_guard flock(m_failedMutex);
                        m_failed.insert(m_decodeActiveKey);
                    }
                    m_inflight.erase(m_decodeActiveKey);
                    m_inflightSince.erase(m_decodeActiveKey);
                    m_decodeActiveKey.clear();
                    m_decodeActiveWorker = -1;
                    ++m_decodeZombies;
                    spawnDecodeWorker();
                }
            }
        }

        std::string activeDecodeKey;
        {
            std::lock_guard lock(m_decodeActMutex);
            activeDecodeKey = m_decodeActiveKey;
        }

        // Dead-worker detector: a queue that stays non-empty with no
        // active decode means no worker is picking items (a worker that
        // died without an active key is invisible to the stuck watchdog).
        if (activeDecodeKey.empty()) {
            if (decodeQ > 0) {
                if (!m_decodeIdleSince) {
                    m_decodeIdleSince =
                        std::chrono::steady_clock::now();
                } else if (std::chrono::duration<double>(
                               std::chrono::steady_clock::now() -
                               *m_decodeIdleSince)
                               .count() > 10.0) {
                    SLC_LOG_WARN(
                        "decode queue stalled with no active worker — "
                        "respawning worker #{}",
                        m_decodeWorkerSeq.load() + 1);
                    ++m_decodeZombies;
                    spawnDecodeWorker();
                    m_decodeIdleSince.reset();
                }
            } else {
                m_decodeIdleSince.reset();
            }
        } else {
            m_decodeIdleSince.reset();
        }

        // Orphan sweep: an inflight key that is not queued, not the active
        // decode, not resident and not failed can never resolve on its
        // own (pool swap during config restart, worker death mid-item) —
        // re-queue it at the front.
        {
            std::unordered_set<std::string> queued;
            {
                std::lock_guard lock(m_decodeMutex);
                for (const auto& q : m_decodeQueue) {
                    queued.insert(q.key);
                }
            }
            const auto now2 = std::chrono::steady_clock::now();
            std::vector<std::string> orphans;
            for (const auto& key : m_inflight) {
                if (queued.contains(key) || key == activeDecodeKey ||
                    m_pool->isResident(key) || isFailed(key)) {
                    continue;
                }
                const auto it = m_inflightSince.find(key);
                const double age =
                    it == m_inflightSince.end()
                        ? 0.0
                        : std::chrono::duration<double>(now2 - it->second)
                              .count();
                if (age > 20.0) {
                    orphans.push_back(key);
                }
            }
            for (const auto& key : orphans) {
                // Drop the marker: issueDecodeWindow re-issues any key it
                // still wants on the next tick (plan keys go to the front).
                SLC_LOG_WARN(
                    "inflight key '{}' orphaned >20s — dropping marker so "
                    "it re-issues",
                    key);
                m_inflight.erase(key);
                m_inflightSince.erase(key);
            }
        }
    }

    if (((collage() && m_cells.empty()) ||
         (m_scene == SceneKind::Matrix && m_matrix.cells.empty())) &&
        playlistSize() > 0) {
        rebuildCells();
    }

    issueDecodeWindow();

    switch (m_state) {
    case State::Loading:
    case State::Fallback: {
        if (m_discoveryDone && playlistSize() > 0) {
            if (auto first = takeFirstResident()) {
                m_state = State::Running;
                m_dwellElapsedSec = 0.0;
                m_fade = 1.0F;
                m_scene = chooseSceneKind();
                if (m_scene == SceneKind::Collage) {
                    // rebuildCells derives this scene's layout pick —
                    // which may upgrade the kind to Matrix.
                    rebuildCells();
                    m_sceneArtCount =
                        m_scene == SceneKind::Matrix
                            ? std::max<std::size_t>(1,
                                                    m_matrix.cells.size())
                            : m_cells.size();
                    SLC_LOG_INFO("screensaver running, first scene={} "
                                 "({} cells)",
                                 m_scene == SceneKind::Matrix ? "matrix"
                                                              : "collage",
                                 m_sceneArtCount);
                } else {
                    m_currentKey = first->second.key;
                    m_curEntry = first->second;
                    m_sceneArtCount = 1;
                    markShown({first->first});
                    m_curAnim = makeAnimator();
                    m_curSpin = makeSpin();
                    SLC_LOG_INFO("screensaver running, first artwork={}",
                                 m_currentKey);
                }
            } else if (m_loadingElapsedSec > kLoadingBudgetSec) {
                if (m_state != State::Fallback) {
                    SLC_LOG_WARN(
                        "no artwork decoded within budget; fallback");
                }
                m_state = State::Fallback;
            }
        }
        break;
    }

    case State::Running: {
        m_dwellElapsedSec += dt;
        // Scene = animation phase (KB duration) + still hold. Once the
        // animation window ends the timelines freeze and the badge fades
        // in; the scene only advances when the still window also ends.
        const double animSec =
            static_cast<double>(m_config.animation.kenBurns.durationMs) /
            1000.0;
        const double stillSec =
            static_cast<double>(m_config.animation.stillMs) / 1000.0;
        if (!m_settled && m_dwellElapsedSec >= animSec) {
            m_settled = true;
        }
        // Plan the next scene as soon as the still window starts so its
        // textures have ~stillSec to decode+upload before the transition.
        if (m_settled && !m_next) {
            planNextScene();
        }
        // A matrix hero moment owns the scene through its decision
        // point: zoom-in/hold are never cut. When the next scene is
        // another matrix the zoom-out must also complete so the
        // decideHeroEnd swap can act as the transition; for any other
        // planned layout the advance runs off the frozen hero frame
        // (the zoomed presentation IS the outgoing scene).
        const auto heroPhase =
            m_scene == SceneKind::Matrix && m_matrixChoreo
                ? m_matrixChoreo->phase()
                : anim::MatrixChoreographer::Phase::Scroll;
        const bool matrixHeroInFlight =
            heroPhase == anim::MatrixChoreographer::Phase::ZoomIn ||
            heroPhase == anim::MatrixChoreographer::Phase::Hero ||
            heroPhase == anim::MatrixChoreographer::Phase::Flush ||
            (heroPhase == anim::MatrixChoreographer::Phase::ZoomOut &&
             m_next && m_next->kind == SceneKind::Matrix);
        // A matrix handing off to another matrix should always route
        // through the hero flush — a mid-scroll FBO fade would skip the
        // hero moment. Defer the advance while scrolling so the next
        // hero picks it up (bounded: past the cap the normal path takes
        // over if a hero somehow never fires).
        const bool matrixNextWaitHero =
            m_scene == SceneKind::Matrix && m_matrixChoreo && m_next &&
            m_next->kind == SceneKind::Matrix &&
            heroPhase == anim::MatrixChoreographer::Phase::Scroll &&
            m_dwellElapsedSec < animSec + stillSec + 10.0;
        if (m_dwellElapsedSec >= animSec + stillSec &&
            !matrixHeroInFlight && !matrixNextWaitHero &&
            playlistSize() > 1) {
            if (advanceScene()) {
                m_dwellElapsedSec = 0.0;
            }
        }
        if (m_fade < 1.0F) {
            const double fadeSec =
                m_reducedMotion
                    ? 0.0
                    : static_cast<double>(m_config.animation.transitionMs) /
                          1000.0;
            m_fade =
                (fadeSec <= 0.0)
                    ? 1.0F
                    : static_cast<float>(
                          std::min(1.0, m_fade + dt / fadeSec));
            if (m_fade >= 1.0F) {
                m_prevKey.clear();
                m_prevAnim.reset();
                m_prevSpin.reset();
                m_prevCells.clear();
                m_prevChoreo.reset();
            }
        }
        break;
    }
    }

    // Protected window: what is on screen plus what is about to be.
    // Computed after the state switch so a just-advanced scene's keys are
    // protected in the same frame the plan was adopted.
    {
        std::unordered_set<std::string> keep;
        if (!m_currentKey.empty()) keep.insert(m_currentKey);
        if (!m_prevKey.empty()) keep.insert(m_prevKey);
        for (const auto& c : m_prevCells) keep.insert(c.entry.key);
        for (const auto& c : m_cells) keep.insert(c.entry.key);
        for (const auto& c : m_prevMatrix.cells) {
            keep.insert(c.entry.key);
        }
        for (const auto& c : m_matrix.cells) keep.insert(c.entry.key);
        if (m_next) {
            if (m_next->kind == SceneKind::Collage) {
                for (const auto& c : m_next->cells) {
                    keep.insert(c.entry.key);
                }
            } else if (m_next->kind == SceneKind::Matrix) {
                for (const auto& c : m_next->matrix.cells) {
                    keep.insert(c.entry.key);
                }
            } else {
                keep.insert(m_next->key);
            }
        }
        m_pool->setProtectedKeys(std::move(keep));
    }

    const float budget =
        (m_host && m_host->isPreviewMode()) ? kUploadBudgetPreviewMs
                                            : kUploadBudgetMs;
    m_pool->drainUploads(budget);

    // Clear inflight markers for keys that made it to residency or
    // failed; a pool-level upload rejection also counts as failed so the
    // key is never re-queued or waited on again.
    for (auto it = m_inflight.begin(); it != m_inflight.end();) {
        if (m_pool->isUploadFailed(*it) && !isFailed(*it)) {
            SLC_LOG_WARN("texture upload failed for '{}'", *it);
            std::lock_guard lock(m_failedMutex);
            m_failed.insert(*it);
        }
        const bool done = m_pool->isResident(*it) || isFailed(*it);
        if (done) {
            m_inflightSince.erase(*it);
            it = m_inflight.erase(it);
        } else {
            ++it;
        }
    }
}

void ScreensaverApp::draw()
{
    const FrameEndGuard frameGuard;
    if (!m_userShader) {
        drawScene();
        if (m_debugUi) {
            drawDebugUiFrame();
        }
        return;
    }
    // User post-process pass: render the whole scene into the FBO, then
    // draw it back through the user-supplied fragment shader.
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    const int pw = static_cast<int>(getWindowWidth() * pxScale);
    const int ph = static_cast<int>(getWindowHeight() * pxScale);
    if (!m_fbo || m_fbo->getWidth() != pw || m_fbo->getHeight() != ph) {
        m_fbo = ci::gl::Fbo::create(pw, ph);
    }
    {
        const ci::gl::ScopedFramebuffer fboScope(m_fbo);
        ci::gl::clear(ci::Color::black());
        const ci::gl::ScopedViewport vp(0, 0, pw, ph);
        const ci::gl::ScopedMatrices mtx;
        ci::gl::setMatricesWindow(pw, ph, true);
        ci::gl::scale(pxScale, pxScale);
        drawScene();
    }
    m_userShader->uniform("uResolution",
                          ci::vec2(static_cast<float>(pw),
                                   static_cast<float>(ph)));
    m_userShader->uniform(
        "uTime", static_cast<float>(getElapsedSeconds()));
    const ci::gl::ScopedGlslProg shaderScope(m_userShader);
    ci::gl::draw(m_fbo->getColorTexture(), getWindowBounds());

    // ImGui overlay renders over the finished frame (never post-processed
    // by the user shader).
    if (m_debugUi) {
        drawDebugUiFrame();
    }
}

bool ScreensaverApp::inputDismissEnabled() const
{
    return (!m_host || !m_host->isPreviewMode()) &&
           !env::testHookTruthy(env::kTestDisableInputVar);
}

void ScreensaverApp::keyDown(ci::app::KeyEvent /*event*/)
{
    if (inputDismissEnabled()) {
        quit();
    }
}

void ScreensaverApp::mouseDown(ci::app::MouseEvent /*event*/)
{
    if (inputDismissEnabled()) {
        quit();
    }
}

void ScreensaverApp::mouseMove(ci::app::MouseEvent event)
{
    if (!inputDismissEnabled()) {
        return;
    }
    constexpr float kMouseDismissPx = 6.0F;
    const ci::vec2 pos{event.getPos()};
    if (m_mouseAnchor.x < 0.0F) {
        m_mouseAnchor = pos;
        return;
    }
    if (ci::distance(m_mouseAnchor, pos) > kMouseDismissPx) {
        quit();
    }
}

void ScreensaverApp::cleanup()
{
    SLC_LOG_INFO("screensaver cleanup (resident={})", m_pool->residentCount());
    // ImGui shutdown needs the GL context still current — before anything
    // else tears down render resources.
    m_debugUi.reset();
    m_decodeStop = true;
    m_decodeCv.notify_all(); // detached decode workers exit on next poll
    {
        std::lock_guard hlock(m_hltbMutex);
        if (m_hltbThread.joinable()) {
            m_hltbThread.request_stop();
        }
    }
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    std::lock_guard hlock(m_hltbMutex);
    if (m_hltbThread.joinable()) {
        // May still be inside one HTTP request — it honors the stop token
        // after it returns (bounded by the request timeout).
        m_hltbThread.join();
    }
}

bool ScreensaverApp::isFailed(const std::string& key) const
{
    std::lock_guard lock(m_failedMutex);
    return m_failed.contains(key);
}

std::size_t ScreensaverApp::playlistSize() const
{
    std::lock_guard lock(m_playlistMutex);
    return m_playlist.size();
}

bool ScreensaverApp::collage() const
{
    return m_scene == SceneKind::Collage;
}

ScreensaverApp::PlaylistEntry
ScreensaverApp::playlistEntry(std::size_t index) const
{
    std::lock_guard lock(m_playlistMutex);
    if (index >= m_playlist.size()) {
        return {};
    }
    return m_playlist[index];
}

} // namespace slc::app
