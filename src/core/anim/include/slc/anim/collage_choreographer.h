#pragma once

#include "slc/anim/icell_animator.h"

#include <cinder/Rect.h>
#include <cinder/Timeline.h>
#include <cinder/gl/Texture.h>

#include <memory>
#include <random>
#include <vector>

namespace slc::anim {

/// Per-cell visual state for one frame (the FrameSnapshot concept from
/// doc §9.3): fold of fade/scale entrance, drift offset, and spotlight boost.
struct CellVisual {
    float alpha = 1.0F;    ///< [0,1] draw opacity
    float scale = 1.0F;    ///< scale about the cell's cover-fit center
    ci::vec2 offset{0};    ///< px offset applied to the drawn rect
    float rotationDeg = 0.0F; ///< rotation about the cell's center
    float spotBoost = 0.0F; ///< [0,1] spotlight emphasis on this cell
};

/**
 * M4 — drives staggered fade-in, per-cell drift, and spotlight focus across a
 * full collage frame. Wraps a pausable/resettable ci::Timeline so tests can
 * step deterministically at a fixed timestep.
 *
 * Spotlight: a looping 0→1 phase over kSpotlightPeriodSec; on each wrap the
 * focused cell advances to a seeded choice. Emphasis follows the bump
 * 4f(1−f), which is 0 at both wrap edges — so the index switch never pops.
 */
class CollageChoreographer {
  public:
    static constexpr float kSpotlightPeriodSec = 12.0F;
    static constexpr float kStaggerRatio = 0.4F; // of fadeIn, per cell index
    static constexpr float kSpotScale = 0.05F;   // extra scale at pulse peak
    // RotateAnimator amplitudes top out here (0.7 + 0.9 * u01); the
    // renderer's overscan margin is provisioned against this bound.
    static constexpr float kMaxRotationDeg = 1.6F;

    CollageChoreographer(int fadeInMs, int driftPx, std::mt19937& rng);

    void addCell(const ci::Rectf& cell, std::shared_ptr<ci::gl::Texture2d> tex);
    void start();
    void pause();
    void reset();
    void step(float deltaSeconds); // fixed-timestep drive for tests

    [[nodiscard]] std::size_t cellCount() const { return m_cells.size(); }
    [[nodiscard]] CellVisual cellVisual(std::size_t index) const;
    [[nodiscard]] std::size_t spotlightIndex() const { return m_spotIndex; }

    [[nodiscard]] ci::Timeline& timeline() { return *m_timeline; }

  private:
    struct CellNode {
        ci::Rectf rect;
        std::shared_ptr<ci::gl::Texture2d> tex;
        std::unique_ptr<ICellAnimator> fade;
        // Seeded per cell: DriftAnimator | RotateAnimator | ScalePulseAnimator.
        std::unique_ptr<ICellAnimator> motion;
    };

    int m_fadeInMs;
    int m_driftPx;
    std::mt19937& m_rng;
    ci::TimelineRef m_timeline = ci::Timeline::create();
    ci::Anim<float> m_spotPhase{0.0F};
    std::size_t m_spotIndex = 0;
    float m_prevSpotPhase = 0.0F;
    bool m_paused = false;
    std::vector<CellNode> m_cells;
};

} // namespace slc::anim
