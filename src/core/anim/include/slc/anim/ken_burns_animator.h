#pragma once

#include "slc/anim/icell_animator.h"

#include <cinder/Tween.h>

#include <random>

namespace slc::anim {

/**
 * M2 — pan & zoom entrance (doc section 5.4): interpolates zoom
 * zoomFrom -> zoomTo over durationMs with ci::easeOutCubic while drifting
 * from a seeded edge anchor on the pan box ([-1,1], at least one axis on the
 * edge) to the cell centre. Direction comes from the injected rng
 * (determinism contract, doc section 6.3).
 *
 * attach() registers the tweens on the caller's timeline and returns; the
 * caller steps the timeline and reads zoom()/pan() per frame.
 */
class KenBurnsAnimator final : public ICellAnimator {
  public:
    KenBurnsAnimator(float zoomFrom, float zoomTo, int durationMs,
                     std::mt19937& rng);

    /// Full-parameter variant: explicit pan endpoints on the [-1,1] overflow
    /// box. Used for traverse motion (edge-to-edge pan) where panFrom and
    /// panTo sit on opposite edges.
    KenBurnsAnimator(float zoomFrom, float zoomTo, const ci::vec2& panFrom,
                     const ci::vec2& panTo, int durationMs);

    void attach(ci::Timeline& timeline, const ci::Rectf& cell,
                std::shared_ptr<ci::gl::Texture2d> tex) override;

    /// Current scale multiplier over the cover-fit rect (1.0 = exact fit).
    [[nodiscard]] float zoom() const { return m_zoom(); }
    /// Current pan offset, normalised to the [-1,1] overflow box.
    [[nodiscard]] ci::vec2 pan() const { return m_pan(); }
    /// Seeded start anchor; exposed for deterministic tests.
    [[nodiscard]] ci::vec2 anchor() const { return m_anchorFrom; }

    // ICellAnimator folds: KB is consumed through frameRect() (zoom/pan are
    // in image space, not compositing space) so the composite folds are
    // identity.
    [[nodiscard]] float alpha() const override { return 1.0F; }
    [[nodiscard]] float scale() const override { return 1.0F; }
    [[nodiscard]] ci::vec2 offset() const override { return ci::vec2(0); }

  private:
    float m_zoomTo;
    ci::vec2 m_panTo{0};
    int m_durationMs;
    ci::Anim<float> m_zoom;
    ci::Anim<ci::vec2> m_pan;
    ci::vec2 m_anchorFrom;
};

} // namespace slc::anim
