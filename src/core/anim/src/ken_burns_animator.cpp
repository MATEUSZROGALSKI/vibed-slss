#include "slc/anim/ken_burns_animator.h"

#include <cinder/Easing.h>

#include <algorithm>
#include <cmath>

namespace slc::anim {

namespace {
constexpr float kTwoPi = 6.283185307179586F;
}

KenBurnsAnimator::KenBurnsAnimator(float zoomFrom, float zoomTo,
                                   int durationMs, std::mt19937& rng)
    : m_zoomTo(zoomTo), m_durationMs(durationMs), m_zoom(zoomFrom)
{
    // Uniform direction, then project onto the [-1,1] box perimeter so the
    // pan always starts touching an edge (doc: "seeded edge anchor").
    std::uniform_real_distribution<float> unit(0.0F, 1.0F);
    const float angle = unit(rng) * kTwoPi;
    const ci::vec2 dir(std::cos(angle), std::sin(angle));
    const float extent = std::max(std::abs(dir.x), std::abs(dir.y));
    m_anchorFrom = dir / extent;
    m_pan = ci::Anim<ci::vec2>(m_anchorFrom);
}

KenBurnsAnimator::KenBurnsAnimator(float zoomFrom, float zoomTo,
                                   const ci::vec2& panFrom,
                                   const ci::vec2& panTo, int durationMs)
    : m_zoomTo(zoomTo), m_panTo(panTo), m_durationMs(durationMs),
      m_zoom(zoomFrom), m_pan(panFrom), m_anchorFrom(panFrom)
{
}

void KenBurnsAnimator::attach(ci::Timeline& timeline,
                              const ci::Rectf& /*cell*/,
                              std::shared_ptr<ci::gl::Texture2d> /*tex*/)
{
    const float dur = static_cast<float>(m_durationMs) / 1000.0F;
    timeline.apply(&m_zoom, m_zoomTo, dur, ci::easeOutCubic);
    // Traverses use easeInOutQuad so motion doesn't lurch at the ends;
    // the anchor->center entrance keeps the original easeOutCubic.
    const bool traverse = m_panTo != ci::vec2(0);
    timeline.apply(&m_pan, m_panTo, dur,
                   traverse ? &ci::easeInOutQuad : &ci::easeOutCubic);
}

} // namespace slc::anim
