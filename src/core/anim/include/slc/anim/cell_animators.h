#pragma once

#include "slc/anim/icell_animator.h"

namespace slc::anim {

/// Staggered entrance: alpha 0->1 and scale 0.96->1.0 over `fadeSec`,
/// starting `delaySec` after the timeline's current time.
class FadeScaleAnimator final : public ICellAnimator {
  public:
    FadeScaleAnimator(float fadeSec, float delaySec)
        : m_fadeSec(fadeSec), m_delaySec(delaySec)
    {
    }

    void attach(ci::Timeline& timeline, const ci::Rectf& cell,
                std::shared_ptr<ci::gl::Texture2d> tex) override;

    [[nodiscard]] float alpha() const override { return m_alpha.value(); }
    [[nodiscard]] float scale() const override { return m_scale.value(); }
    [[nodiscard]] ci::vec2 offset() const override { return ci::vec2(0); }

  private:
    float m_fadeSec;
    float m_delaySec;
    // Scale must start at the tween's startValue — Anim holds 1.0 until the
    // delayed tween begins, and 1.0 -> 0.96 in one frame is a visible pop.
    ci::Anim<float> m_alpha{0.0F};
    ci::Anim<float> m_scale{0.96F};
};

/// Continuous gentle pan: drifts `dir` * `amplitudePx` from the cell center
/// over `periodSec`, ping-pong looping forever. Seeded direction/amplitude
/// make each cell's motion distinct yet deterministic.
class DriftAnimator final : public ICellAnimator {
  public:
    DriftAnimator(const ci::vec2& dir, float amplitudePx, float periodSec)
        : m_dir(dir), m_amplitudePx(amplitudePx), m_periodSec(periodSec)
    {
    }

    void attach(ci::Timeline& timeline, const ci::Rectf& cell,
                std::shared_ptr<ci::gl::Texture2d> tex) override;

    [[nodiscard]] float alpha() const override { return 1.0F; }
    [[nodiscard]] float scale() const override { return 1.0F; }
    [[nodiscard]] ci::vec2 offset() const override { return m_offset.value(); }

  private:
    ci::vec2 m_dir;
    float m_amplitudePx;
    float m_periodSec;
    ci::Anim<ci::vec2> m_offset{ci::vec2(0)};
};

/// Gentle rocking: angle ping-pongs -maxDeg..+maxDeg over `periodSec`,
/// looping. Used both for collage cells and (with a coverage margin) the
/// single-image scene's slight-rotation motion.
class RotateAnimator final : public ICellAnimator {
  public:
    RotateAnimator(float maxDeg, float periodSec)
        : m_maxDeg(maxDeg), m_periodSec(periodSec)
    {
    }

    void attach(ci::Timeline& timeline, const ci::Rectf& cell,
                std::shared_ptr<ci::gl::Texture2d> tex) override;

    [[nodiscard]] float alpha() const override { return 1.0F; }
    [[nodiscard]] float scale() const override { return 1.0F; }
    [[nodiscard]] ci::vec2 offset() const override { return ci::vec2(0); }
    [[nodiscard]] float rotation() const override { return m_angle.value(); }

  private:
    float m_maxDeg;
    float m_periodSec;
    ci::Anim<float> m_angle{0.0F};
};

/// Slow breathing zoom: scale ping-pongs 1.0..1.0+amount over `periodSec`,
/// looping. Collage-cell motion variant.
class ScalePulseAnimator final : public ICellAnimator {
  public:
    ScalePulseAnimator(float amount, float periodSec)
        : m_amount(amount), m_periodSec(periodSec)
    {
    }

    void attach(ci::Timeline& timeline, const ci::Rectf& cell,
                std::shared_ptr<ci::gl::Texture2d> tex) override;

    [[nodiscard]] float alpha() const override { return 1.0F; }
    [[nodiscard]] float scale() const override { return m_scale.value(); }
    [[nodiscard]] ci::vec2 offset() const override { return ci::vec2(0); }

  private:
    float m_amount;
    float m_periodSec;
    ci::Anim<float> m_scale{1.0F};
};

} // namespace slc::anim
