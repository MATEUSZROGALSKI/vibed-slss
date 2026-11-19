#include "slc/anim/cell_animators.h"

namespace slc::anim {

void FadeScaleAnimator::attach(ci::Timeline& timeline, const ci::Rectf& cell,
                               std::shared_ptr<ci::gl::Texture2d> tex)
{
    (void)cell;
    (void)tex;
    timeline
        .apply(&m_alpha, 0.0F, 1.0F, m_fadeSec, ci::easeOutCubic)
        .delay(m_delaySec);
    timeline
        .apply(&m_scale, 0.96F, 1.0F, m_fadeSec, ci::easeOutCubic)
        .delay(m_delaySec);
}

void DriftAnimator::attach(ci::Timeline& timeline, const ci::Rectf& cell,
                           std::shared_ptr<ci::gl::Texture2d> tex)
{
    (void)cell;
    (void)tex;
    timeline
        .apply(&m_offset, ci::vec2(0), m_dir * m_amplitudePx, m_periodSec,
               ci::easeInOutQuad)
        .pingPong()
        .loop();
}

void RotateAnimator::attach(ci::Timeline& timeline, const ci::Rectf& cell,
                            std::shared_ptr<ci::gl::Texture2d> tex)
{
    (void)cell;
    (void)tex;
    timeline
        .apply(&m_angle, -m_maxDeg, m_maxDeg, m_periodSec, ci::easeInOutQuad)
        .pingPong()
        .loop();
}

void ScalePulseAnimator::attach(ci::Timeline& timeline, const ci::Rectf& cell,
                                std::shared_ptr<ci::gl::Texture2d> tex)
{
    (void)cell;
    (void)tex;
    timeline
        .apply(&m_scale, 1.0F, 1.0F + m_amount, m_periodSec, ci::easeInOutSine)
        .pingPong()
        .loop();
}

} // namespace slc::anim
