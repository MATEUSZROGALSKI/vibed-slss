// ScreensaverApp scene planning — scene-kind/transition picks, the
// weighted pick queue, collage cell layout + cell/entry matching, and
// the dwell/advance machinery. Split from screensaver_app.cpp.
#include "slc/app/screensaver_app.h"

#include "slc/common/logging.h"
#include "slc/layout/bsp_strategy.h"
#include "slc/layout/grid_strategy.h"
#include "slc/layout/masonry_strategy.h"
#include "slc/layout/mosaic_strategy.h"
#include "slc/layout/rows_strategy.h"
#include "slc/layout/spiral_strategy.h"
#include "slc/layout/spotlight_strategy.h"
#include "slc/platform/paths.h"

#include <cinder/app/App.h>
#include <cinder/app/Window.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>

namespace slc::app {

namespace {

constexpr double kStallCapSec = 45.0; // never hold a scene >45s on a plan

/// Builds the concrete strategy for a layout pick index (0..6). Indices
/// match the draw order in pickLayoutIndex; anything else is nullptr.
std::unique_ptr<layout::ILayoutStrategy> makeStrategy(std::size_t idx,
                                                    float gutter)
{
    switch (idx) {
    case 0: return std::make_unique<layout::GridStrategy>(gutter);
    case 1: return std::make_unique<layout::MasonryStrategy>(gutter);
    case 2: return std::make_unique<layout::SpiralStrategy>(gutter);
    case 3: return std::make_unique<layout::BspStrategy>(gutter);
    case 4: return std::make_unique<layout::RowsStrategy>(gutter);
    case 5: return std::make_unique<layout::MosaicStrategy>(gutter);
    case 6: return std::make_unique<layout::SpotlightStrategy>(gutter);
    default: return nullptr;
    }
}

} // namespace

std::mt19937 ScreensaverApp::layoutRng(std::size_t sceneIndex) const
{
    std::mt19937 lr;
    lr.seed(static_cast<std::mt19937::result_type>(
        m_config.layout.seed.value_or(0x5EEDu) +
        static_cast<std::uint64_t>(sceneIndex)));
    return lr;
}

std::size_t ScreensaverApp::pickLayoutIndex(std::mt19937& lr,
                                            LayoutMode mode)
{
    // Mode narrows the draw; the multi-image layout itself stays
    // scene-random either way. Matrix mode consumes no draw — the layout
    // is fixed — and Focused never reaches here (chooseSceneKind picks
    // Single first).
    switch (mode) {
    case LayoutMode::Matrix:
        return kMatrixLayoutIdx;
    case LayoutMode::Collage: {
        std::uniform_int_distribution<std::size_t> pick(
            0, kMatrixLayoutIdx - 1);
        return pick(lr);
    }
    default: {
        std::uniform_int_distribution<std::size_t> pick(
            0, kMatrixLayoutIdx);
        return pick(lr);
    }
    }
}

ScreensaverApp::LayoutMode ScreensaverApp::layoutMode() const
{
    const auto& m = m_config.layout.mode;
    if (m == "focused") {
        return LayoutMode::Focused;
    }
    if (m == "collage") {
        return LayoutMode::Collage;
    }
    if (m == "matrix") {
        return LayoutMode::Matrix;
    }
    return LayoutMode::Mix;
}

std::shared_ptr<anim::KenBurnsAnimator> ScreensaverApp::makeAnimator()
{
    const auto& kb = m_config.animation.kenBurns;
    if (!kb.enabled || m_reducedMotion || m_timeline == nullptr) {
        return nullptr;
    }
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);
    std::shared_ptr<anim::KenBurnsAnimator> an;
    const float pick = u01(m_rng);
    if (pick < 0.30F) {
        // Traverse: fixed zoom, edge-to-edge pan on a seeded axis
        // ("move from one side to another").
        const bool horizontal = u01(m_rng) < 0.5F;
        const float sgn = u01(m_rng) < 0.5F ? 1.0F : -1.0F;
        const ci::vec2 from =
            horizontal ? ci::vec2(-sgn, 0.0F) : ci::vec2(0.0F, -sgn);
        an = std::make_shared<anim::KenBurnsAnimator>(
            1.18F, 1.18F, from, -from, kb.durationMs);
    } else if (pick < 0.65F) {
        // Zoom in: start fitted, slowly magnify toward a seeded edge.
        constexpr float kTwoPi = 6.283185307179586F;
        const float angle = u01(m_rng) * kTwoPi;
        const ci::vec2 dir(std::cos(angle), std::sin(angle));
        const ci::vec2 anchor =
            dir / std::max(std::abs(dir.x), std::abs(dir.y));
        an = std::make_shared<anim::KenBurnsAnimator>(
            kb.zoomTo, kb.zoomFrom, ci::vec2(0), anchor, kb.durationMs);
    } else {
        // Settle-out: start magnified on a seeded edge, drift to fit.
        an = std::make_shared<anim::KenBurnsAnimator>(
            kb.zoomFrom, kb.zoomTo, kb.durationMs, m_rng);
    }
    an->attach(*m_timeline, ci::Rectf(getWindowBounds()), nullptr);
    return an;
}

std::shared_ptr<anim::RotateAnimator> ScreensaverApp::makeSpin()
{
    const auto& kb = m_config.animation.kenBurns;
    if (!kb.enabled || m_reducedMotion || m_timeline == nullptr) {
        return nullptr;
    }
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);
    if (u01(m_rng) > 0.35F) {
        return nullptr; // slight rotation on ~1 in 3 scenes
    }
    auto spin = std::make_shared<anim::RotateAnimator>(
        1.4F, 10.0F + 6.0F * u01(m_rng));
    spin->attach(*m_timeline, ci::Rectf(getWindowBounds()), nullptr);
    return spin;
}

ScreensaverApp::SceneKind ScreensaverApp::chooseSceneKind()
{
    switch (layoutMode()) {
    case LayoutMode::Focused:
        return SceneKind::Single;
    case LayoutMode::Collage:
    case LayoutMode::Matrix:
        // Both enter the collage planning branch; the layout pick then
        // produces a cell collage or the Matrix scene respectively.
        return SceneKind::Collage;
    default:
        break;
    }
    const float p = m_config.layout.collageProbability;
    if (m_config.layout.cellCount <= 1 || p <= 0.0F) {
        return SceneKind::Single;
    }
    if (p >= 1.0F) {
        return SceneKind::Collage;
    }
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);
    return u01(m_rng) < p ? SceneKind::Collage : SceneKind::Single;
}

anim::SceneTransition ScreensaverApp::chooseTransition()
{
    std::uniform_int_distribution<int> pick(0, 3);
    const auto randomKind = static_cast<anim::SceneTransition>(pick(m_rng));
    return anim::sceneTransitionFromString(m_config.animation.transition,
                                           randomKind);
}

void ScreensaverApp::rebuildCells()
{
    // Re-derive this scene's layout pick — layoutRng(sceneIndex) makes it
    // deterministic, so the rebuild reproduces the same layout a plan
    // for this scene index made (or upgrades Collage -> Matrix when the
    // pick says so, e.g. the very first scene).
    auto lr = layoutRng(m_sceneIndex);
    const auto layoutIdx = pickLayoutIndex(lr, layoutMode());
    if (m_scene == SceneKind::Collage &&
        layoutIdx == kMatrixLayoutIdx) {
        m_scene = SceneKind::Matrix;
    }
    if (m_scene == SceneKind::Matrix) {
        std::vector<std::size_t> indices;
        m_matrix = buildMatrixCells(indices, lr);
        markShown(indices);
        startMatrixChoreo();
        return;
    }
    if (!collage()) {
        m_cells.clear();
        return;
    }
    std::vector<std::size_t> indices;
    const auto strat =
        makeStrategy(layoutIdx,
                     static_cast<float>(m_config.layout.gutter));
    m_cells = buildCells(indices, strat.get(), lr);
    markShown(indices);
    startChoreo();
}

std::vector<ScreensaverApp::Cell>
ScreensaverApp::buildCells(std::vector<std::size_t>& indices,
                           const layout::ILayoutStrategy* strat,
                           std::mt19937& lr)
{
    std::vector<Cell> cells;
    const std::size_t n = playlistSize();
    if (n == 0 || strat == nullptr) {
        return cells;
    }
    refillPickQueue();

    // Scene-randomized cell count within [cellCountMin, cellCount].
    const int lo =
        std::clamp(m_config.layout.cellCountMin, 2, m_config.layout.cellCount);
    std::uniform_int_distribution<int> countDist(lo,
                                                 m_config.layout.cellCount);
    // A library smaller than the wanted cell count can't fill unique
    // cells — shrink the layout to the library rather than leaving dark
    // holes (the repeat fallback below still covers multi-display
    // oversubscription).
    const auto count =
        std::min(static_cast<std::size_t>(countDist(lr)), n);

    std::vector<ci::Rectf> rects;
    const auto displays =
        m_host ? m_host->displays() : std::vector<platform::DisplayInfo>{};
    if (displays.size() > 1) {
        // Per-display composition (doc M3): independent layout per monitor.
        // Display rects arrive in the platform's desktop coordinate space,
        // NOT window coords — Win32/macOS origins sit on the primary/main
        // display, so a monitor placed left of or above it reports
        // negative offsets. Map the union of all displays onto the window
        // bounds: the window always spans that union, so the transform is
        // exact and unit mismatches (points vs pixels) cancel out.
        ci::Rectf all(std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest());
        for (const auto& d : displays) {
            all.include(ci::Rectf(
                static_cast<float>(d.x), static_cast<float>(d.y),
                static_cast<float>(d.x + d.width),
                static_cast<float>(d.y + d.height)));
        }
        const ci::Rectf win(getWindowBounds());
        if (all.getWidth() > 0.0F && all.getHeight() > 0.0F) {
            const float sx = win.getWidth() / all.getWidth();
            const float sy = win.getHeight() / all.getHeight();
            for (const auto& d : displays) {
                ci::Rectf dr(
                    win.getX1() +
                        (static_cast<float>(d.x) - all.getX1()) * sx,
                    win.getY1() +
                        (static_cast<float>(d.y) - all.getY1()) * sy,
                    win.getX1() +
                        (static_cast<float>(d.x + d.width) - all.getX1()) *
                            sx,
                    win.getY1() +
                        (static_cast<float>(d.y + d.height) - all.getY1()) *
                            sy);
                dr.clipBy(win);
                if (dr.getWidth() <= 0.0F || dr.getHeight() <= 0.0F) {
                    continue;
                }
                auto part = strat->compute(dr, count, lr);
                rects.insert(rects.end(), part.begin(), part.end());
            }
        }
    }
    if (rects.empty()) {
        rects = strat->compute(ci::Rectf(getWindowBounds()), count, lr);
    }

    // Snap cell edges to whole device pixels: neighbouring cells then share
    // identical boundary coordinates, the per-cell scissor clips abut
    // exactly, and no ~1px overlap column can flicker between two
    // drifting textures at the seams.
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    for (auto& r : rects) {
        r.x1 = std::round(r.x1 * pxScale) / pxScale;
        r.y1 = std::round(r.y1 * pxScale) / pxScale;
        r.x2 = std::round(r.x2 * pxScale) / pxScale;
        r.y2 = std::round(r.y2 * pxScale) / pxScale;
    }

    std::vector<SlotPick> slots;
    slots.reserve(rects.size());
    for (const auto& r : rects) {
        slots.push_back(
            {r.getWidth() / std::max(1.0F, r.getHeight()),
             std::max(r.getWidth(), r.getHeight())});
    }
    const auto entries = pickEntriesForSlots(slots, indices);
    cells.reserve(rects.size());
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (!entries[i].key.empty()) {
            cells.push_back({rects[i], entries[i]});
        }
    }

    SLC_LOG_INFO("collage layout: {} cells, strategy={}", cells.size(),
                 strat->name());
    return cells;
}

std::vector<ScreensaverApp::PlaylistEntry>
ScreensaverApp::pickEntriesForSlots(const std::vector<SlotPick>& slots,
                                    std::vector<std::size_t>& indices)
{
    std::vector<PlaylistEntry> out(slots.size());
    const std::size_t n = playlistSize();
    if (n == 0) {
        return out;
    }
    refillPickQueue();

    // Entries are matched to slot SHAPE, not just taken in queue order:
    // within a lookahead window the app whose best artwork variant fits
    // this slot's aspect wins, so a square cell never forces a 3:1 hero
    // into a centre-crop while a portrait capsule sits in the queue.
    constexpr std::size_t kCellFitLookahead = 16;
    // Variant keys already placed this scene — repeat picks avoid them
    // so the same app can show different art, never the identical file.
    std::unordered_set<std::string> usedKeys;
    // Per-entry repeat count — spreads fallback picks round-robin
    // instead of letting the single best-fit app win every cell.
    std::unordered_map<std::size_t, std::size_t> repCount;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const float slotAspect = slots[i].aspect;
        const float side = slots[i].side;

        // The queue window (~cellCount+ahead) is often smaller than the
        // slot count (matrix scenes need ~100+): top it up mid-build.
        // takeAt/popFront indices were burned, so refills only draw
        // apps not yet consumed by THIS build.
        if (m_picks.empty()) {
            refillPickQueue();
        }

        std::size_t bestPos = m_picks.size();
        float bestScore = std::numeric_limits<float>::max();
        const auto scan =
            std::min(m_picks.size(), kCellFitLookahead);
        for (std::size_t pos = 0; pos < scan; ++pos) {
            const auto cand = playlistEntry(m_picks.at(pos));
            if (cand.variants.empty()) {
                continue;
            }
            // Portrait slots (matrix columns) prefer grid capsules —
            // landscape art only wins when the app has none.
            std::vector<content::Artwork> portrait;
            const std::vector<content::Artwork>* src = &cand.variants;
            if (slots[i].portraitOnly) {
                for (const auto& v : cand.variants) {
                    if (v.width <= v.height) {
                        portrait.push_back(v);
                    }
                }
                if (!portrait.empty()) {
                    src = &portrait;
                }
            }
            const auto fit =
                content::pickBestFitScored(*src, slotAspect, side);
            const auto& v = (*src)[fit.index];
            // Entries whose chosen variant failed to decode before can
            // never become resident — pretend they don't exist.
            if (isFailed(v.appId + "/" +
                         std::string(content::toString(v.kind)))) {
                continue;
            }
            if (fit.score < bestScore) {
                bestScore = fit.score;
                bestPos = pos;
            }
        }

        PlaylistEntry e;
        if (bestPos < m_picks.size()) {
            const auto idx = m_picks.takeAt(bestPos);
            m_picks.burn(idx);
            indices.push_back(idx);
            e = playlistEntry(idx);
            e.pick(slotAspect, side);
        } else {
            // Nothing usable in the window — fall back to queue order.
            for (;;) {
                if (m_picks.empty()) {
                    break;
                }
                const auto idx = m_picks.front();
                m_picks.popFront();
                m_picks.burn(idx);
                indices.push_back(idx);
                e = playlistEntry(idx);
                e.pick(slotAspect, side);
                if (!e.key.empty() && !isFailed(e.key)) {
                    break;
                }
            }
        }
        if (e.key.empty()) {
            // Queue dried up (tiny library, or more cells across displays
            // than apps): reuse an already-consumed entry — preferring a
            // variant not yet shown this scene so a repeat reads as
            // variety (same game's hero + capsule) instead of the
            // identical image twice. A duplicate still beats a hole.
            float repScore = std::numeric_limits<float>::max();
            std::size_t repIdx = n;
            std::string repKey;
            std::filesystem::path repPath;
            for (std::size_t pi = 0; pi < n; ++pi) {
                const auto cand = playlistEntry(pi);
                std::vector<content::Artwork> avail;
                for (const auto& v : cand.variants) {
                    const std::string k =
                        v.appId + "/" +
                        std::string(content::toString(v.kind));
                    if (!usedKeys.contains(k) && !isFailed(k)) {
                        avail.push_back(v);
                    }
                }
                const auto& availSrc =
                    avail.empty() ? cand.variants : avail;
                std::vector<content::Artwork> portrait;
                const std::vector<content::Artwork>* src = &availSrc;
                if (slots[i].portraitOnly) {
                    for (const auto& v : availSrc) {
                        if (v.width <= v.height) {
                            portrait.push_back(v);
                        }
                    }
                    if (!portrait.empty()) {
                        src = &portrait;
                    }
                }
                if (src->empty()) {
                    continue;
                }
                const auto fit =
                    content::pickBestFitScored(*src, slotAspect, side);
                const auto& v = (*src)[fit.index];
                const std::string k =
                    v.appId + "/" + std::string(content::toString(v.kind));
                if (isFailed(k)) {
                    continue;
                }
                // Reusing an already-shown key is a last resort (large
                // penalty); among repeats the least-repeated app wins
                // so duplicates spread across the library instead of
                // one hot entry filling every leftover cell.
                const float effScore =
                    fit.score +
                    static_cast<float>(repCount[pi]) * 0.75F +
                    (usedKeys.contains(k) ? 1000.0F : 0.0F);
                if (effScore < repScore) {
                    repScore = effScore;
                    repIdx = pi;
                    repKey = k;
                    repPath = v.path;
                }
            }
            if (repIdx != n) {
                repCount[repIdx] += 1;
                m_picks.burn(repIdx);
                indices.push_back(repIdx);
                e = playlistEntry(repIdx);
                e.pick(slotAspect, side); // badge metadata + fallback key
                e.key = repKey;
                e.path = repPath;
            }
        }
        if (e.key.empty()) {
            continue;
        }
        usedKeys.insert(e.key);
        // The variant actually assigned (e.key) drives the decode cap —
        // don't re-pick: a repeat entry's key may deliberately point at
        // a different variant than pickBestFit would choose.
        const content::Artwork* chosen = nullptr;
        for (const auto& v : e.variants) {
            if (v.appId + "/" + std::string(content::toString(v.kind)) ==
                e.key) {
                chosen = &v;
                break;
            }
        }
        // Decode cap: ~1.5x the slot side covers drift/rotation overscan;
        // never decode larger than the variant's native size.
        std::uint32_t target = static_cast<std::uint32_t>(
            std::clamp(side * 1.5F, 512.0F, 2400.0F));
        if (chosen && chosen->width > 0) {
            target = std::min(target, std::max(chosen->width,
                                               chosen->height));
        }
        e.maxDim = target;
        out[i] = std::move(e);
    }
    return out;
}

ScreensaverApp::MatrixGeom ScreensaverApp::matrixGeom() const
{
    MatrixGeom g;
    const ci::Rectf win(getWindowBounds());
    const float W = win.getWidth();
    const float H = win.getHeight();
    if (W <= 0.0F || H <= 0.0F) {
        return g;
    }
    g.gap = std::max(4.0F,
                     static_cast<float>(m_config.layout.gutter) * 0.4F);
    // Many very narrow strips (>=12 wherever they fit) of small portrait
    // grid images — dense poster-marquee, not a sparse grid. Capped so
    // total cell count stays within texture-pool/decode budget.
    g.cols = std::clamp(static_cast<int>(W / 150.0F), 12, 16);
    g.colW = (W - g.gap * static_cast<float>(g.cols + 1)) /
             static_cast<float>(g.cols);
    // Portrait-ish slots (3:4): capsules and grids fit well, heroes
    // letterbox instead of being cropped to ribbons.
    g.cellH = g.colW * 4.0F / 3.0F;
    g.pitch = g.cellH + g.gap;
    // Wrap period: a whole number of pitches at least one cell taller
    // than the window, so the marquee never exposes a gap.
    g.stackH = std::ceil((H + g.pitch) / g.pitch) * g.pitch;
    g.cellsPerCol = static_cast<std::size_t>(g.stackH / g.pitch);
    return g;
}

ScreensaverApp::MatrixScene
ScreensaverApp::buildMatrixCells(std::vector<std::size_t>& indices,
                                 std::mt19937& lr)
{
    MatrixScene scene;
    const std::size_t n = playlistSize();
    if (n == 0) {
        return scene;
    }
    const ci::Rectf win(getWindowBounds());
    const MatrixGeom g = matrixGeom();
    if (g.cols <= 0) {
        return scene;
    }
    scene.pitch = g.pitch;

    std::vector<SlotPick> slots;
    slots.reserve(g.cellCount());
    for (std::size_t i = 0; i < g.cellCount(); ++i) {
        slots.push_back({g.colW / g.cellH, g.cellH, /*portraitOnly=*/true});
    }
    const auto entries = pickEntriesForSlots(slots, indices);

    // Seeded per column: direction (marquees read better alternating),
    // magnitude and a random start offset so strips aren't in lockstep.
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);
    std::uniform_real_distribution<float> spd(24.0F, 64.0F);
    std::uniform_int_distribution<int> dirPick(0, 1);
    const bool firstUp = dirPick(lr) == 0;
    scene.columns.reserve(static_cast<std::size_t>(g.cols));
    for (int c = 0; c < g.cols; ++c) {
        anim::MatrixColumn col;
        col.x1 = win.getX1() + g.gap +
                 static_cast<float>(c) * (g.colW + g.gap);
        col.x2 = col.x1 + g.colW;
        const bool up = ((c % 2) == 0) == firstUp;
        col.speed = spd(lr) * (up ? -1.0F : 1.0F);
        col.stackH = g.stackH;
        col.offset = u01(lr) * g.stackH;
        scene.columns.push_back(col);
    }

    auto it = entries.begin();
    for (std::size_t c = 0; c < scene.columns.size(); ++c) {
        for (std::size_t j = 0; j < g.cellsPerCol; ++j, ++it) {
            MatrixCell cell;
            cell.column = c;
            cell.y = static_cast<float>(j) * g.pitch;
            cell.h = g.cellH;
            if (it != entries.end()) {
                cell.entry = *it;
            }
            scene.cells.push_back(std::move(cell));
        }
    }
    SLC_LOG_INFO("matrix layout: {} cols x {} cells",
                 scene.columns.size(), g.cellsPerCol);
    return scene;
}

void ScreensaverApp::startChoreo()
{
    if (m_config.animation.collage.enabled && !m_reducedMotion) {
        m_choreo = std::make_unique<anim::CollageChoreographer>(
            m_config.animation.collage.fadeInMs,
            m_config.animation.collage.driftPx, m_rng);
        for (const auto& c : m_cells) {
            m_choreo->addCell(c.rect, nullptr);
        }
        m_choreo->start();
    } else {
        m_choreo.reset();
    }
}

void ScreensaverApp::startMatrixChoreo()
{
    if (m_config.animation.collage.enabled && !m_reducedMotion) {
        // The hero must reach its hold end inside the scene dwell so the
        // decideHeroEnd handoff fires before the scene clock advances —
        // the zoom-out itself is the transition (matrix→matrix swap or
        // the outgoing frame for other layouts), so only zoom-in + hold
        // need reserving.
        const float dwellSec =
            static_cast<float>(m_config.animation.kenBurns.durationMs +
                               m_config.animation.stillMs) / 1000.0F;
        const float heroReserve =
            anim::MatrixChoreographer::kZoomInSec +
            anim::MatrixChoreographer::kHeroSec + 1.5F;
        const float scrollSec = std::clamp(
            dwellSec - heroReserve, 4.0F,
            anim::MatrixChoreographer::kScrollSec);
        m_matrixChoreo = std::make_unique<anim::MatrixChoreographer>();
        m_matrixChoreo->start(m_matrix.columns, scrollSec);
        m_heroLogged = false;
        m_heroDecided = false;
        m_heroSwapActive = false;
        m_heroSwapTex.reset();
        m_flushActive = false;
        m_flushEntries.clear();
        m_flushDone.clear();
    } else {
        m_matrixChoreo.reset();
    }
}

void ScreensaverApp::refillPickQueue()
{
    const std::size_t n = playlistSize();
    if (n == 0) {
        return;
    }
    m_picks.ensureSize(n);
    const std::size_t target = std::max<std::size_t>(
        12, static_cast<std::size_t>(m_config.layout.cellCount) +
                kDecodeAhead + 2);
    m_picks.refill(target, m_rng);
}

std::optional<std::pair<std::size_t, ScreensaverApp::PlaylistEntry>>
ScreensaverApp::takeFirstResident()
{
    const ci::Rectf win(getWindowBounds());
    const float winAspect =
        win.getWidth() / std::max(1.0F, win.getHeight());
    const float winMax = std::max(win.getWidth(), win.getHeight());
    // Scan without consuming: non-resident entries stay queued for their
    // own scenes — only the found resident index is removed.
    for (std::size_t pos = 0; pos < m_picks.size(); ++pos) {
        const auto idx = m_picks.at(pos);
        auto e = playlistEntry(idx);
        e.pick(winAspect, winMax);
        if (!e.key.empty() && m_pool->isResident(e.key)) {
            m_picks.takeAt(pos);
            return std::pair{idx, std::move(e)};
        }
    }
    return std::nullopt;
}

void ScreensaverApp::markShown(const std::vector<std::size_t>& indices)
{
    m_picks.ensureSize(playlistSize());
    m_picks.markShown(indices);
}

void ScreensaverApp::planNextScene()
{
    const std::size_t n = playlistSize();
    if (n == 0) {
        return;
    }
    refillPickQueue();

    PlannedScene plan;
    plan.kind = chooseSceneKind();
    if (plan.kind == SceneKind::Collage) {
        // The layout pick is the scene rng's first draw — one of the
        // seven cell strategies or the scrolling Matrix scene.
        auto lr = layoutRng(m_sceneIndex + 1);
        const auto layoutIdx = pickLayoutIndex(lr, layoutMode());
        if (layoutIdx == kMatrixLayoutIdx) {
            plan.kind = SceneKind::Matrix;
            plan.matrix = buildMatrixCells(plan.indices, lr);
            if (plan.matrix.cells.empty()) {
                plan.kind = SceneKind::Single;
            }
        } else {
            const auto strat =
                makeStrategy(layoutIdx,
                             static_cast<float>(m_config.layout.gutter));
            plan.cells = buildCells(plan.indices, strat.get(), lr);
            if (plan.cells.empty()) {
                plan.kind = SceneKind::Single;
            }
        }
    }
    if (plan.kind == SceneKind::Single) {
        const ci::Rectf win(getWindowBounds());
        const float winAspect =
            win.getWidth() / std::max(1.0F, win.getHeight());
        const float winMax = std::max(win.getWidth(), win.getHeight());
        // Pop until an entry whose picked variant hasn't failed decode —
        // failed keys can never become resident.
        while (!m_picks.empty()) {
            const auto idx = m_picks.front();
            m_picks.popFront();
            plan.indices.push_back(idx);
            plan.entry = playlistEntry(idx);
            plan.entry.pick(winAspect, winMax);
            if (!plan.entry.key.empty() && !isFailed(plan.entry.key)) {
                break;
            }
        }
        if (plan.entry.key.empty()) {
            return;
        }
        const auto singleMax = static_cast<std::uint32_t>(
            std::clamp(winMax * 1.4F, 1024.0F, 4096.0F));
        plan.entry.maxDim = singleMax;
        plan.key = plan.entry.key;
    }
    m_next = std::move(plan);
}

void ScreensaverApp::decideHeroEnd()
{
    // Matrix hero hold just ended — the scene's transition point. If the
    // planned scene is another matrix, swap the cell set under the cover
    // of the zoomed art: the un-blur reveals the new marquee, so no FBO
    // transition is needed. Other layouts take the normal advanceScene
    // path, whose outgoing snapshot is this very hero frame.
    if (!m_next || m_next->kind != SceneKind::Matrix ||
        !planResident(*m_next) || !m_matrixChoreo) {
        return;
    }
    const std::size_t hc = m_matrixChoreo->heroCell();
    if (hc >= m_matrix.cells.size() ||
        m_matrix.cells[hc].column >= m_matrix.columns.size()) {
        return;
    }
    // Freeze the fly-back target + the art ref before the cell swap —
    // the zoom-out glides the old artwork back toward its old slot while
    // the new marquee is already scrolling underneath.
    const auto& hero = m_matrix.cells[hc];
    const auto& hcol = m_matrix.columns[hero.column];
    float hy = std::fmod(hero.y + hcol.offset, hcol.stackH);
    if (hy < 0.0F) {
        hy += hcol.stackH;
    }
    m_heroFrom = ci::Rectf(hcol.x1, hy - m_matrix.pitch, hcol.x2,
                           hy - m_matrix.pitch + hero.h);
    m_heroSwapTex = m_pool->get(hero.entry.key);
    m_heroSwapEntry = hero.entry;
    m_heroSwapActive = true;
    auto plan = std::move(*m_next);
    m_next.reset();
    m_matrix = std::move(plan.matrix);
    ++m_sceneIndex;
    m_dwellElapsedSec = 0.0;
    m_fade = 1.0F; // no FBO transition — the un-blur IS the transition
    markShown(plan.indices);
    SLC_LOG_INFO("scene {}: matrix swap under hero ({} cells)",
                 m_sceneIndex, m_matrix.cells.size());
}

bool ScreensaverApp::planResident(const PlannedScene& plan) const
{
    if (plan.kind == SceneKind::Collage) {
        bool any = false;
        for (const auto& c : plan.cells) {
            if (c.entry.key.empty()) {
                continue;
            }
            any = true;
            if (!m_pool->isResident(c.entry.key)) {
                return false;
            }
        }
        return any;
    }
    if (plan.kind == SceneKind::Matrix) {
        // A matrix scene may adopt with stragglers still decoding —
        // missing cells pop into the marquee when they arrive, which
        // reads far better than stalling the dwell on one slow decode
        // (or one permanently failed key holding ~24 textures hostage).
        std::size_t wanted = 0, have = 0;
        for (const auto& c : plan.matrix.cells) {
            if (c.entry.key.empty()) {
                continue;
            }
            ++wanted;
            if (m_pool->isResident(c.entry.key)) {
                ++have;
            }
        }
        return wanted > 0 && have * 10 >= wanted * 7; // ~70%
    }
    return !plan.key.empty() && m_pool->isResident(plan.key);
}

bool ScreensaverApp::advanceScene()
{
    const std::size_t n = playlistSize();
    if (n == 0) {
        return false;
    }
    if (!m_next) {
        planNextScene();
    }
    if (!m_next) {
        return false; // nothing to plan around (empty pick queue)
    }
    // The planned scene's textures were prefetched during the still
    // window; if they're still not here, extend the dwell rather than
    // starting a transition that pops cells in mid-fade. Past the grace
    // cap we try resident swaps; past the stall cap we drop the plan
    // entirely — the current scene keeps rendering either way.
    const double animSec =
        static_cast<double>(m_config.animation.kenBurns.durationMs) /
        1000.0;
    const double stillSec =
        static_cast<double>(m_config.animation.stillMs) / 1000.0;
    constexpr double kPlanGraceSec = 3.0;
    if (!planResident(*m_next)) {
        // A plan whose keys all failed decode can never turn resident —
        // dropping into Loading from here is the visible flicker path, so
        // re-plan around them instead of adopting a dead scene.
        bool anyAlive = false;
        if (m_next->kind == SceneKind::Collage ||
            m_next->kind == SceneKind::Matrix) {
            const auto alive = [&](const auto& cells) {
                for (const auto& c : cells) {
                    if (!isFailed(c.entry.key)) {
                        return true;
                    }
                }
                return false;
            };
            anyAlive = m_next->kind == SceneKind::Collage
                           ? alive(m_next->cells)
                           : alive(m_next->matrix.cells);
        } else {
            anyAlive = !isFailed(m_next->key);
        }
        if (!anyAlive) {
            SLC_LOG_WARN("planned scene undecodable — re-planning");
            m_next.reset();
            return false;
        }
        if (m_dwellElapsedSec < animSec + stillSec + kPlanGraceSec) {
            return false;
        }
        // Past the grace cap with a still-pending scene: swap in
        // already-resident queue entries instead of adopting a scene the
        // draw path can't paint (that drop is the visible flicker).
        if (m_next->kind == SceneKind::Single) {
            if (auto swap = takeFirstResident()) {
                m_next->indices.push_back(swap->first);
                m_next->entry = swap->second;
                m_next->key = swap->second.key;
            }
        } else {
            // Collage/Matrix: swap still-missing cells for resident
            // queue entries where possible.
            const auto swapMissing = [&](auto& cells) {
                for (auto& c : cells) {
                    if (c.entry.key.empty() ||
                        m_pool->isResident(c.entry.key)) {
                        continue;
                    }
                    if (auto swap = takeFirstResident()) {
                        c.entry = swap->second;
                        m_next->indices.push_back(swap->first);
                    }
                }
            };
            if (m_next->kind == SceneKind::Collage) {
                swapMissing(m_next->cells);
            } else {
                swapMissing(m_next->matrix.cells);
            }
        }
        // If the plan still can't paint fully, never adopt it — drop it
        // and keep the current scene (it still renders); the next tick
        // plans fresh entries. The 45s stall cap only bounds how long a
        // single plan may hog the dwell window.
        if (!planResident(*m_next)) {
            if (m_dwellElapsedSec < animSec + stillSec + kStallCapSec) {
                return false;
            }
            SLC_LOG_WARN(
                "scene stall cap — dropping unready plan, keeping current");
            m_next.reset();
            return false;
        }
    }

    // Snapshot the outgoing scene for the transition.
    m_prevScene = m_scene;
    m_prevChoreo.reset();
    m_prevMatrixChoreo.reset();
    if (m_scene == SceneKind::Collage) {
        m_prevCells = m_cells;
        // Keep the choreo frozen at its current visual state — cells leave
        // at their real fade/offset instead of snapping to defaults.
        m_prevChoreo = std::move(m_choreo);
    } else if (m_scene == SceneKind::Matrix) {
        // Column offsets live in the scene struct, so the copy freezes
        // the marquee; the choreo move preserves heroT for the hero draw.
        m_prevMatrix = m_matrix;
        m_prevMatrixChoreo = std::move(m_matrixChoreo);
    } else {
        m_prevKey = m_currentKey;
        m_prevEntry = m_curEntry;
        m_prevAnim = m_curAnim;
        m_prevSpin = m_curSpin;
    }

    m_transition = chooseTransition();
    {
        std::uniform_int_distribution<int> pick(0, 3);
        static const ci::vec2 kDirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        m_slideDir = kDirs[pick(m_rng)];
    }
    ++m_sceneIndex;
    m_fade = 0.0F;
    m_settled = false;

    auto plan = std::move(*m_next);
    m_next.reset();
    m_scene = plan.kind;
    if (m_scene == SceneKind::Collage) {
        m_cells = std::move(plan.cells);
        m_sceneArtCount = std::max<std::size_t>(1, m_cells.size());
        startChoreo();
    } else if (m_scene == SceneKind::Matrix) {
        m_matrix = std::move(plan.matrix);
        m_sceneArtCount =
            std::max<std::size_t>(1, m_matrix.cells.size());
        startMatrixChoreo();
    } else {
        m_currentKey = plan.key;
        m_curEntry = plan.entry;
        m_sceneArtCount = 1;
        m_curAnim = makeAnimator();
        m_curSpin = makeSpin();
    }
    markShown(plan.indices);
    const auto kindName = [](SceneKind k) {
        switch (k) {
        case SceneKind::Collage: return "collage";
        case SceneKind::Matrix: return "matrix";
        default: return "single";
        }
    };
    SLC_LOG_INFO("scene {}: {} -> {}, transition={}", m_sceneIndex,
                 kindName(m_prevScene), kindName(m_scene),
                 static_cast<int>(m_transition));
    if (m_scene == SceneKind::Single) {
        SLC_LOG_INFO("scene {} adopted key='{}' resident={}",
                     m_sceneIndex, m_currentKey,
                     m_pool->isResident(m_currentKey));
    }
    return true;
}

const content::Artwork* ScreensaverApp::PlaylistEntry::pick(
    float slotAspect, float slotMaxDim)
{
    if (variants.empty()) {
        return nullptr;
    }
    const auto& v =
        variants[content::pickBestFit(variants, slotAspect, slotMaxDim)];
    key = v.appId + "/" + std::string(content::toString(v.kind));
    path = v.path;
    // Badge metadata is per-app; enriched (SGDB) variants may lack it, so
    // take it from whichever variant carries it.
    appId = v.appId;
    gameName = v.gameName;
    playtimeMin = v.playtimeMin;
    lastPlayed = v.lastPlayed;
    achEarned = v.achEarned;
    achTotal = v.achTotal;
    for (const auto& alt : variants) {
        if (gameName.empty()) {
            gameName = alt.gameName;
        }
        if (playtimeMin == 0) {
            playtimeMin = alt.playtimeMin;
        }
        if (lastPlayed == 0) {
            lastPlayed = alt.lastPlayed;
        }
        if (achEarned == 0) {
            achEarned = alt.achEarned;
        }
        if (achTotal == 0) {
            achTotal = alt.achTotal;
        }
    }
    return &v;
}

} // namespace slc::app
