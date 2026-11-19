#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace slc::anim {

/// One scrolling strip in a Matrix scene. Offsets live here (not in the
/// choreographer) so a snapshot copy of the column set freezes the scroll
/// position for transition rendering.
struct MatrixColumn {
    float x1 = 0.0F;
    float x2 = 0.0F;
    float speed = 0.0F;  ///< px/s, signed (negative scrolls up)
    float offset = 0.0F; ///< current scroll position, wraps mod stackH
    float stackH = 0.0F; ///< wrap period — full column stack height
};

/**
 * Matrix scene motion: columns of artwork scroll vertically, each at its
 * own seeded speed/direction, wrapping around like a marquee. On a
 * periodic cadence one cell is zoomed to a hero presentation:
 *
 *   Scroll (kScrollSec) -> ZoomIn -> Hero (kHeroSec) -> ZoomOut -> Scroll
 *
 * The app decides WHICH cell is the hero (it knows residency/on-screen
 * position); the choreographer owns the phase timers and mutates column
 * offsets in place. Pure math — no GL — so tests can step it directly.
 */
class MatrixChoreographer {
  public:
    enum class Phase { Scroll, ZoomIn, Hero, Flush, ZoomOut };

    static constexpr float kScrollSec = 12.0F;
    static constexpr float kZoomInSec = 0.9F;
    static constexpr float kHeroSec = 5.0F;
    static constexpr float kZoomOutSec = 0.8F;
    /// Columns keep drifting behind the dimmed hero, but slower.
    static constexpr float kHeroScrollFactor = 0.25F;
    /// Matrix→matrix transition: instead of zooming straight out, the
    /// marquee races so cells wrap and pick up the next scene's artwork.
    static constexpr float kFlushSec = 2.5F;
    static constexpr float kFlushScrollFactor = 12.0F;

    /// Takes over `cols` — held by reference, must outlive the choreo.
    /// `scrollSec` is the scrolling window before the first hero trigger
    /// — the app sizes it so the full hero cycle fits inside the scene
    /// dwell (zoom-in + hold + zoom-out ≈ 6.7s).
    void start(std::vector<MatrixColumn>& cols,
               float scrollSec = kScrollSec);
    void step(float dtSec);
    /// Locks the hero target; only meaningful while phase()==Scroll.
    void beginHero(std::size_t column, std::size_t cell);
    /// When armed, the hold is followed by a Flush phase (fast scroll
    /// for the matrix→matrix handoff) instead of going straight to
    /// ZoomOut. Consumed once the flush runs.
    void setFlushAtHoldEnd(bool flush) { m_flushQueued = flush; }

    [[nodiscard]] Phase phase() const { return m_phase; }
    /// true while Scroll and the scroll window has elapsed — the app
    /// should pick a hero cell and call beginHero (or leave it scrolling).
    [[nodiscard]] bool wantsHero() const
    {
        return m_phase == Phase::Scroll && m_scrollT >= m_scrollTarget;
    }
    /// Eased zoom amount [0,1]: 0 = fully embedded, 1 = hero position.
    [[nodiscard]] float heroT() const { return m_heroT; }
    [[nodiscard]] std::size_t heroColumn() const { return m_heroCol; }
    [[nodiscard]] std::size_t heroCell() const { return m_heroCell; }

  private:
    std::vector<MatrixColumn>* m_cols = nullptr;
    Phase m_phase = Phase::Scroll;
    float m_scrollTarget = kScrollSec;
    float m_scrollT = 0.0F;
    float m_phaseT = 0.0F;
    float m_heroT = 0.0F;
    bool m_flushQueued = false;
    std::size_t m_heroCol = 0;
    std::size_t m_heroCell = 0;
};

} // namespace slc::anim
