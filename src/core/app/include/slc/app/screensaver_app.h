#pragma once

#include "slc/anim/cell_animators.h"
#include "slc/anim/collage_choreographer.h"
#include "slc/anim/ken_burns_animator.h"
#include "slc/anim/matrix_choreographer.h"
#include "slc/anim/scene_transition.h"
#include "slc/app/iplatform_host.h"
#include "slc/app/pick_queue.h"
#include "slc/config/app_config.h"
#include "slc/content/artwork.h"
#include "slc/render/texture_pool.h"

#include <cinder/gl/Batch.h>
#include <cinder/gl/Fbo.h>
#include <cinder/gl/GlslProg.h>
#include <cinder/gl/Texture.h>
#include <cinder/gl/TextureFont.h>
#include <cinder/Timeline.h>

// Cinder only ships AppScreenSaver on Windows and macOS; on Linux the plain
// App (GLFW/headless impl) is the entry point. This selects between two
// Cinder APIs, not platform logic — OS integration still lives in
// src/platform (goal G6).
#if defined( CINDER_MSW ) || defined( CINDER_MAC )
#include <cinder/app/AppScreenSaver.h>
#else
#include <cinder/app/App.h>
#endif

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slc::layout {
class ILayoutStrategy;
}

namespace slc::app {

/**
 * M1 — Cinder app implementing the Loading/Running/Fallback state machine
 * (doc section 5.5) in single-image full-screen mode.
 *
 * A worker thread discovers local Steam artwork, picks the best piece per
 * game and shuffles it (seeded by layout.seed when configured). Decoding is
 * demand-driven: the app requests the few entries ahead of the playhead, the
 * worker turns them into Surfaces, and the GL thread drains uploads into
 * TexturePool on a per-frame budget. A protected-key window keeps the
 * current/previous/next textures resident while the LRU bounds memory
 * (doc section 5.6).
 *
 * Loading is bounded at 1500 ms; preview mode caps at 15 fps (host-enforced)
 * and never touches the disk cache.
 *
 * Constructed with no arguments from Cinder's entry machinery; the host must
 * register a factory via setHostFactory() first.
 */
#if defined( CINDER_MSW ) || defined( CINDER_MAC )
using ScreensaverAppBase = ci::app::AppScreenSaver;
#else
using ScreensaverAppBase = ci::app::App;
#endif

// Not final: the Linux XScreenSaver host subclasses it to reparent the
// Cinder window into $XSCREENSAVER_WINDOW during setup() (doc 6.1.3).
class ScreensaverApp : public ScreensaverAppBase {
  public:
    ScreensaverApp();
    explicit ScreensaverApp(std::unique_ptr<IPlatformHost> host);

    /// Host hook used by the no-arg ctor (Cinder entry). Must be set before
    /// the app is created; defaults to a headless host if unset.
    static void setHostFactory(std::function<std::unique_ptr<IPlatformHost>()> factory);

    /// Ignore the OS reduced-motion preference. Used by live-wallpaper
    /// mode, where launching the animation is the explicit user intent.
    static void setIgnoreReducedMotion(bool ignore);

    /// Called at the very end of draw() on the GL thread, whatever the
    /// exit path. Platform hosts use it to present the frame elsewhere —
    /// e.g. the Wayland wallpaper copies it to a layer-shell surface.
    static void setFrameEndHook(std::function<void()> hook);

    void setup() override;
    void update() override;
    void draw() override;
    void cleanup() override;
    void resize() override;

    // Input dismissal for hosts without a native screensaver impl (Linux).
    // AppScreenSaver::quit() is a documented no-op, so on Windows/macOS these
    // are harmless while on Linux they implement the dismiss-on-input contract.
    void keyDown(ci::app::KeyEvent event) override;
    void mouseDown(ci::app::MouseEvent event) override;
    void mouseMove(ci::app::MouseEvent event) override;

  private:
    enum class State { Loading, Running, Fallback };

    struct PlaylistEntry {
        std::string key;
        std::filesystem::path path;
        /// Decode-side downscale cap (0 = native size). Cells and singles
        /// set this so resident textures fit the memory budget (doc 5.6).
        std::uint32_t maxDim = 0;
        /// Every artwork kind discovered for the app, best-ranked first.
        /// The slot aspect ratio picks among them via pick().
        std::vector<content::Artwork> variants;

        /// Badge metadata of the picked variant (name/stats are per-app,
        /// identical across variants).
        std::string appId;
        std::string gameName;
        std::uint64_t playtimeMin = 0;
        std::uint64_t lastPlayed = 0;
        std::uint32_t achEarned = 0;
        std::uint32_t achTotal = 0;
        /// HowLongToBeat main-story minutes; 0 until the background
        /// resolver fills it (patched live on m_playlist).
        std::uint32_t hltbMin = 0;

        /// Sets key/path to the variant whose aspect best fits a slot with
        /// aspect `slotAspect` and longest side `slotMaxDim` px; returns it
        /// (nullptr when variants is empty).
        const content::Artwork* pick(float slotAspect,
                                     float slotMaxDim = 0.0F);
    };

    struct Cell {
        ci::Rectf rect;
        PlaylistEntry entry;
    };

    /// One artwork slot in a Matrix column stack. x comes from the
    /// column strip; y is the unwrapped stack position in px.
    struct MatrixCell {
        std::size_t column = 0;
        float y = 0.0F;  ///< top within the column stack
        float h = 0.0F;  ///< cell height
        PlaylistEntry entry;
    };

    struct MatrixScene {
        std::vector<anim::MatrixColumn> columns;
        std::vector<MatrixCell> cells;
        float pitch = 0.0F; ///< cell height + gap — the wrap step
    };

    /// Column/cell geometry for the current window — shared by
    /// buildMatrixCells and the texture-pool capacity estimate (a
    /// matrix scene must fit in the pool or its plan can never adopt).
    struct MatrixGeom {
        int cols = 0;
        float gap = 0.0F;
        float colW = 0.0F;
        float cellH = 0.0F;
        float pitch = 0.0F;
        float stackH = 0.0F;
        std::size_t cellsPerCol = 0;
        [[nodiscard]] std::size_t cellCount() const
        {
            return static_cast<std::size_t>(cols > 0 ? cols : 0) *
                   cellsPerCol;
        }
    };

    // Scene model: each dwell period is a "scene" — a single full-screen
    // artwork, a collage, or a scrolling Matrix. Kind, motion style and
    // the transition into the next scene are all drawn from m_rng so a
    // configured layout.seed reproduces the whole show.
    enum class SceneKind { Single, Collage, Matrix };

    /// Which scene kinds the layout.mode config allows.
    enum class LayoutMode { Focused, Collage, Matrix, Mix };

    /// Next scene picked ahead of the transition so its textures are
    /// already decoded+uploaded before the fade starts (no mid-transition
    /// pop-in). `indices` are the playlist entries consumed.
    struct PlannedScene {
        SceneKind kind = SceneKind::Single;
        std::vector<Cell> cells;            // collage
        MatrixScene matrix;                 // matrix
        std::vector<std::size_t> indices;   // consumed playlist indices
        PlaylistEntry entry;                // single
        std::string key;                    // single
    };

    void startDiscovery();
    /// Background HowLongToBeat resolution for the current playlist;
    /// patches entry.hltbMin under m_playlistMutex. No-op if running.
    void startHltbResolve();
    void issueDecodeWindow();
    /// Picks the upcoming scene (kind + entries + layout) so the decode
    /// window can prefetch its textures during the still window.
    void planNextScene();
    /// Starts the transition into m_next. Returns false while the planned
    /// scene's textures aren't resident yet (dwell extends instead of
    /// showing a half-loaded scene) — up to a grace limit.
    bool advanceScene();
    /// Called once when the matrix hero's hold ends (ZoomOut begins):
    /// if the planned scene is another matrix, swaps the cell set under
    /// the cover of the zoomed art so the un-blur IS the transition;
    /// other layouts fall through to the normal advanceScene path.
    void decideHeroEnd();
    /// Weighted pick-queue: keeps a short list of upcoming playlist
    /// indices, sampled by weight, excluding recently-shown entries.
    void refillPickQueue();
    /// Pops the first queue index whose picked variant is resident —
    /// non-resident fronts are dropped from the queue.
    [[nodiscard]] std::optional<std::pair<std::size_t, PlaylistEntry>>
    takeFirstResident();
    /// Drops the weight of shown entries and records them in the recents
    /// window so they can't be re-picked until it slides past.
    void markShown(const std::vector<std::size_t>& indices);
    /// Deterministic per-scene layout rng — seed + sceneIndex, so a
    /// configured layout.seed reproduces layouts (and the layout pick is
    /// always this stream's first draw).
    [[nodiscard]] std::mt19937 layoutRng(std::size_t sceneIndex) const;
    /// Draws the layout choice for a collage scene: 0..6 = strategy
    /// index, kMatrixLayoutIdx = the Matrix scene. The mode decides the
    /// allowed range (Matrix mode always returns kMatrixLayoutIdx);
    /// the multi-image layout itself stays scene-random.
    static constexpr std::size_t kMatrixLayoutIdx = 7;
    static std::size_t pickLayoutIndex(std::mt19937& lr, LayoutMode mode);
    /// Maps config layout.mode to LayoutMode (unknown values -> Mix).
    [[nodiscard]] LayoutMode layoutMode() const;
    /// Assigns playlist entries to aspect slots using the pick queue's
    /// lookahead matching (plus the unseen-variant repeat fallback).
    /// Pops consumed indices into `indices`; entries parallel `slots`
    /// (an empty key marks a slot that got nothing).
    struct SlotPick {
        float aspect;
        float side;
        /// Prefer portrait/square variants (grid capsules) — slots that
        /// set this get a landscape variant only when the app has none.
        bool portraitOnly = false;
    };
    std::vector<PlaylistEntry>
    pickEntriesForSlots(const std::vector<SlotPick>& slots,
                        std::vector<std::size_t>& indices);
    /// Cells for a collage layout; pops their playlist indices into
    /// `indices` (consumed whether or not the plan is adopted). `strat`
    /// comes from pickLayoutIndex; `lr` is the layout rng with the
    /// layout draw already consumed.
    [[nodiscard]] std::vector<Cell>
    buildCells(std::vector<std::size_t>& indices,
               const layout::ILayoutStrategy* strat, std::mt19937& lr);
    /// Matrix column/cell geometry for the current window bounds.
    [[nodiscard]] MatrixGeom matrixGeom() const;
    /// Scrolling columns + wrapped cell stacks for a Matrix scene.
    [[nodiscard]] MatrixScene
    buildMatrixCells(std::vector<std::size_t>& indices, std::mt19937& lr);
    [[nodiscard]] bool planResident(const PlannedScene& plan) const;
    void startChoreo();
    void startMatrixChoreo();
    /// Decode workers are detached std::threads (never joined — a stuck
    /// ci::loadImage must not be joinable-wedged). update() watches the
    /// active item; past kDecodeStuckSec it marks the key failed and
    /// spawns a replacement worker so the queue keeps draining.
    void spawnDecodeWorker();
    /// config.json watch: reload + apply on mtime change (~1s poll).
    void pollConfigReload();
    void applyConfig(config::AppConfig next);
    void restartDiscovery();
    void ensureBadgeFonts();
    void rebuildCells();
    void drawDebugOverlay(const ci::Rectf& bounds);
    /// Whole-frame content; draw() either calls this directly or renders
    /// it into m_fbo and post-processes through m_userShader.
    void drawScene();
    /// Transition compositing: each scene renders at identity transform
    /// into its own FBO (static cell clips, no moving scissors), then the
    /// two whole images are drawn to screen under the transition
    /// transform — scale is about the viewport center, offsets apply
    /// uniformly. Allocates/resizes m_fboPrev/m_fboCur on demand.
    void ensureTransitionFbos();
    void renderIntoFbo(const ci::gl::FboRef& fbo,
                       const std::function<void()>& drawFn);
    void drawComposite(const ci::gl::Texture2dRef& tex,
                       const ci::Rectf& bounds,
                       const anim::TransitionVisual& tr);
    /// Loads config.rendering.shader from <configDir>/shaders/; seeds an
    /// example file when the directory is empty. Compile failures log and
    /// leave m_userShader null (passthrough).
    void loadUserShader();
    [[nodiscard]] bool collage() const;
    [[nodiscard]] SceneKind chooseSceneKind();
    [[nodiscard]] anim::SceneTransition chooseTransition();
    [[nodiscard]] std::shared_ptr<anim::KenBurnsAnimator> makeAnimator();
    [[nodiscard]] std::shared_ptr<anim::RotateAnimator> makeSpin();
    void drawSceneCollage(const std::vector<Cell>& cells,
                          const anim::CollageChoreographer* choreo,
                          const anim::TransitionVisual& tr,
                          float badgeAlpha);
    /// Scrolling columns + periodic hero zoom. `choreo` is null while the
    /// scene renders frozen (outgoing transition, reduced motion).
    void drawSceneMatrix(const MatrixScene& scene,
                         const anim::MatrixChoreographer* choreo,
                         const anim::TransitionVisual& tr,
                         float badgeAlpha);
    /// Just the scrolling columns (hero cell excluded while it zooms).
    /// Used both for direct rendering and for the hero blur composite.
    void drawMatrixColumns(const MatrixScene& scene,
                           const anim::MatrixChoreographer* choreo,
                           const anim::TransitionVisual& tr, float dim);
    /// Allocates the matrix blur FBO chain (window, quarter, sixteenth
    /// res). Returns false when GL FBO support is unavailable.
    bool ensureMatrixFbos();
    /// Texture drawn into a rounded rect — lazily-compiled SDF shader,
    /// falls back to a plain draw when GL shader support is absent.
    void drawRounded(const ci::gl::Texture2dRef& tex, const ci::Rectf& dst,
                     float alpha);
    void ensureRoundedShader();
    void ensureBlurShader();
    /// One separable-gaussian pass: src texture -> dst FBO along dir.
    /// uTexel is dest-relative so a downsampled pass widens the kernel.
    void matrixBlurPass(const ci::gl::Texture2dRef& src,
                        const ci::gl::FboRef& dst, const ci::vec2& dir);
    void drawSceneSingle(const std::string& key,
                         const PlaylistEntry& entry,
                         const ci::Rectf& bounds,
                         const anim::KenBurnsAnimator* anim,
                         const anim::RotateAnimator* spin,
                         const anim::TransitionVisual& tr,
                         float badgeAlpha);
    void drawBadge(const PlaylistEntry& entry, const ci::Rectf& anchor,
                   float alpha);
    void drawCellBadge(const Cell& cell, float alpha);
    /// Renders the fixed-size badge card in design space (kBadgeCardW x
    /// kBadgeCardH); callers wrap it in the desired model transform.
    void drawBadgeCard(const PlaylistEntry& entry, float alpha);
    [[nodiscard]] std::size_t playlistSize() const;
    [[nodiscard]] PlaylistEntry playlistEntry(std::size_t index) const;
    [[nodiscard]] bool isFailed(const std::string& key) const;
    [[nodiscard]] bool inputDismissEnabled() const;

    std::unique_ptr<IPlatformHost> m_host;
    config::AppConfig m_config;
    State m_state = State::Loading;

    // Created in setup() once the config tells us how many cells are live:
    // single-image mode needs ~3, collage needs cellCount (+lookahead x2).
    /// Detached decode workers call enqueueDecoded() through this pointer
    /// while the UI thread may replace the pool (config restart). Guard
    /// every m_pool swap AND every worker-side dereference with
    /// m_poolMutex so a decode can't enqueue into a destroyed pool.
    std::unique_ptr<render::TexturePool> m_pool;
    std::mutex m_poolMutex;
    std::vector<PlaylistEntry> m_playlist;
    mutable std::mutex m_playlistMutex;
    std::atomic<bool> m_discoveryDone{false};

    std::deque<PlaylistEntry> m_decodeQueue;
    std::mutex m_decodeMutex;
    std::condition_variable m_decodeCv;
    std::unordered_set<std::string> m_inflight;
    /// When each inflight key was first issued — the orphan sweep re-queues
    /// keys that are neither queued, decoding, resident nor failed after
    /// the timeout (covers worker death and pool-swap losses).
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        m_inflightSince;
    std::unordered_set<std::string> m_failed;
    mutable std::mutex m_failedMutex;
    // Decode pipeline: detached workers + active-item watchdog state.
    std::atomic<bool> m_decodeStop{false};
    std::atomic<int> m_decodeWorkerSeq{0};
    std::atomic<int> m_decodeZombies{0};
    std::mutex m_decodeActMutex;
    std::string m_decodeActiveKey;
    std::chrono::steady_clock::time_point m_decodeActiveSince;
    int m_decodeActiveWorker = -1;
    /// Set while no decode is active; if the queue stays non-empty with no
    /// worker picking items, the worker is dead and gets respawned.
    std::optional<std::chrono::steady_clock::time_point> m_decodeIdleSince;
    int m_heartbeatTicks = 0; // ~60s liveness log in update()
    std::jthread m_worker; // discovery only — never runs decodes
    /// Resolves HowLongToBeat times for playlist apps in the background
    /// and patches entry.hltbMin under m_playlistMutex. Guarded by
    /// m_hltbMutex — spawned by the discovery worker or the UI thread
    /// (live config toggle).
    std::jthread m_hltbThread;
    std::mutex m_hltbMutex;
    std::mt19937 m_rng;

    std::vector<Cell> m_cells;
    std::vector<Cell> m_prevCells; // outgoing collage during a transition
    // M4 choreography: staggered fade-in + per-cell motion + spotlight;
    // null when collage animation is disabled or reduced-motion.
    std::unique_ptr<anim::CollageChoreographer> m_choreo;
    // Outgoing collage's choreo — kept frozen (not stepped) during the
    // transition so cells leave at their current visual state instead of
    // snapping to full alpha.
    std::unique_ptr<anim::CollageChoreographer> m_prevChoreo;

    // Matrix scene: columns scroll independently (offsets live in the
    // column structs, so m_prevMatrix freezes them for the transition)
    // and the choreographer runs the periodic hero zoom.
    MatrixScene m_matrix;
    MatrixScene m_prevMatrix;
    std::unique_ptr<anim::MatrixChoreographer> m_matrixChoreo;
    std::unique_ptr<anim::MatrixChoreographer> m_prevMatrixChoreo;
    bool m_heroLogged = false;
    // Matrix hero handoff: when the next scene is also a matrix, the
    // preferred handoff is the Flush phase (columns race, wrapped cells
    // take new entries). The instant swap below is the fallback for a
    // plan that only became resident after the hero hold — it replaces
    // the cell set under the cover of the zoomed art, and these keep the
    // old hero cell's art + slot rect alive for the fly-back.
    bool m_heroDecided = false;    // decision consumed for this ZoomOut
    bool m_heroSwapActive = false; // cells were swapped mid-ZoomOut
    ci::gl::Texture2dRef m_heroSwapTex;
    PlaylistEntry m_heroSwapEntry;
    ci::Rectf m_heroFrom; // one-shot hero-draw diagnostic per scene
    // Matrix→matrix flush transition: while the choreographer races the
    // columns (Phase::Flush), wrapped cells take entries from this queue
    // — the adopted plan's artwork — so the marquee churns into the new
    // scene organically. The hero cell keeps its entry: its empty slot
    // is where the zoomed card docks back.
    bool m_flushActive = false;
    std::deque<PlaylistEntry> m_flushEntries;
    std::unordered_set<std::size_t> m_flushDone;

    /// Entries past the playhead to prefetch — shared by the pick queue
    /// (scenes) and the decode-window issuer (pipeline).
    static constexpr std::size_t kDecodeAhead = 3;

    // Weighted picking: replaces a fixed shuffle+playhead. Every entry
    // starts at weight 1.0; showing it drops it ~8x and parks it in the
    // recents window; weights recover toward 1 over ~15-20 scenes.
    PickQueue m_picks; // UI thread only
    std::optional<PlannedScene> m_next;

    std::size_t m_sceneIndex = 0;   // drives per-scene deterministic seeds
    std::size_t m_sceneArtCount = 1; // playlist entries the scene consumes
    SceneKind m_scene = SceneKind::Single;
    SceneKind m_prevScene = SceneKind::Single;
    anim::SceneTransition m_transition = anim::SceneTransition::Crossfade;
    ci::vec2 m_slideDir{1.0F, 0.0F};
    std::string m_currentKey;
    std::string m_prevKey;
    PlaylistEntry m_curEntry;  // badge metadata for the current single scene
    PlaylistEntry m_prevEntry;
    ci::TimelineRef m_timeline;
    std::shared_ptr<anim::KenBurnsAnimator> m_curAnim;
    std::shared_ptr<anim::KenBurnsAnimator> m_prevAnim;
    std::shared_ptr<anim::RotateAnimator> m_curSpin;
    std::shared_ptr<anim::RotateAnimator> m_prevSpin;
    double m_dwellElapsedSec = 0.0;
    // Still-window state: set once the scene's animation phase has run out;
    // timelines stop stepping and the badge fades in until advanceScene.
    bool m_settled = false;
    float m_fade = 1.0F; // 1 = current fully opaque
    double m_lastElapsedSec = 0.0;
    std::deque<float> m_frameTimes; // rolling window for the debug overlay
    double m_loadingElapsedSec = 0.0;
    bool m_reducedMotion = false;
    ci::gl::TextureFontRef m_badgeFont;      // title face (26)
    ci::gl::TextureFontRef m_badgeStatsFont; // playtime row (16)
    ci::gl::TextureFontRef m_badgeAchFont;   // completion row (19)
    // User post-process: scene renders into m_fbo, then the FBO texture is
    // drawn through m_userShader (null = straight to screen).
    ci::gl::FboRef m_fbo;
    ci::gl::GlslProgRef m_userShader;
    ci::gl::GlslProgRef m_roundedShader; // matrix scene's rounded-corner draw
    ci::gl::BatchRef m_roundedBatch;     // unit-quad mesh bound to m_roundedShader
    ci::gl::GlslProgRef m_blurShader;    // separable gaussian for matrix hero
    ci::gl::BatchRef m_blurBatch;        // unit-quad mesh bound to m_blurShader
    // Offscreen scene buffers used only while a transition runs.
    ci::gl::FboRef m_fboPrev;
    ci::gl::FboRef m_fboCur;
    // Matrix hero blur: the columns render once into m_fboMatrixSharp,
    // downscale twice into the blur pair, then the blurred composite
    // fades over the sharp frame as heroT rises.
    ci::gl::FboRef m_fboMatrixSharp;
    ci::gl::FboRef m_fboMatrixBlurA; // quarter res
    ci::gl::FboRef m_fboMatrixBlurB; // sixteenth res
    // Live config reload: mtime-polled in update() (~1s).
    std::filesystem::path m_configPath;
    std::filesystem::file_time_type m_configMtime{};
    double m_configCheckSec = 0.0;
    // Mouse-jiggle tolerance for dismiss-on-input: first reported position is
    // the anchor; a move past kMouseDismissPx quits (Linux only in practice).
    ci::vec2 m_mouseAnchor{-1.0F, -1.0F};
};

} // namespace slc::app
