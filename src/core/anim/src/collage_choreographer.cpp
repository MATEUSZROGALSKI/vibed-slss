#include "slc/anim/collage_choreographer.h"

#include "slc/anim/cell_animators.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace slc::anim {

CollageChoreographer::CollageChoreographer(int fadeInMs, int driftPx,
                                           std::mt19937& rng)
    : m_fadeInMs(fadeInMs), m_driftPx(driftPx), m_rng(rng)
{
}

void CollageChoreographer::addCell(const ci::Rectf& cell,
                                   std::shared_ptr<ci::gl::Texture2d> tex)
{
    m_cells.push_back({cell, std::move(tex), nullptr, nullptr});
}

void CollageChoreographer::start()
{
    const float fadeSec =
        std::max(0, m_fadeInMs) / 1000.0F;
    const float staggerSec = fadeSec * kStaggerRatio;

    std::uniform_real_distribution<float> u01(0.0F, 1.0F);

    for (std::size_t i = 0; i < m_cells.size(); ++i) {
        auto& node = m_cells[i];
        const auto delay = static_cast<float>(i) * staggerSec;

        if (fadeSec > 0.0F) {
            node.fade =
                std::make_unique<FadeScaleAnimator>(fadeSec, delay);
            node.fade->attach(*m_timeline, node.rect, node.tex);
        }

        if (m_driftPx > 0) {
            // Seeded per-cell motion: drift 50%, rotate 25%, pulse 25%.
            const float pick = u01(m_rng);
            if (pick < 0.5F) {
                const float angle = u01(m_rng) * glm::two_pi<float>();
                const ci::vec2 dir(std::cos(angle), std::sin(angle));
                const float amp =
                    static_cast<float>(m_driftPx) * (0.6F + 0.4F * u01(m_rng));
                const float period = 18.0F + 14.0F * u01(m_rng);
                node.motion =
                    std::make_unique<DriftAnimator>(dir, amp, period);
            } else if (pick < 0.75F) {
                const float maxDeg =
                    0.7F + (kMaxRotationDeg - 0.7F) * u01(m_rng);
                const float period = 14.0F + 10.0F * u01(m_rng);
                node.motion =
                    std::make_unique<RotateAnimator>(maxDeg, period);
            } else {
                const float amount = 0.04F + 0.05F * u01(m_rng);
                const float period = 12.0F + 10.0F * u01(m_rng);
                node.motion =
                    std::make_unique<ScalePulseAnimator>(amount, period);
            }
            node.motion->attach(*m_timeline, node.rect, node.tex);
        }
    }

    if (!m_cells.empty()) {
        m_timeline->apply(&m_spotPhase, 0.0F, 1.0F, kSpotlightPeriodSec,
                          ci::easeNone)
            .loop();
        m_spotIndex =
            static_cast<std::size_t>(u01(m_rng) * m_cells.size()) %
            m_cells.size();
    }
}

void CollageChoreographer::pause() { m_paused = true; }

void CollageChoreographer::reset()
{
    m_timeline->clear();
    m_spotPhase = 0.0F;
    m_prevSpotPhase = 0.0F;
    m_paused = false;
    // Re-create animator state so fades restart from zero.
    for (auto& node : m_cells) {
        node.fade.reset();
        node.motion.reset();
    }
    start();
}

void CollageChoreographer::step(float deltaSeconds)
{
    if (m_paused) {
        return;
    }
    m_timeline->step(deltaSeconds);
    const float phase = m_spotPhase.value();
    if (phase < m_prevSpotPhase && !m_cells.empty()) {
        // Wrapped: advance the spotlight to a new seeded cell.
        std::uniform_int_distribution<std::size_t> pick(0,
                                                        m_cells.size() - 1);
        m_spotIndex = pick(m_rng);
    }
    m_prevSpotPhase = phase;
}

CellVisual CollageChoreographer::cellVisual(std::size_t index) const
{
    CellVisual v;
    if (index >= m_cells.size()) {
        return v;
    }
    const auto& node = m_cells[index];
    if (node.fade) {
        v.alpha = node.fade->alpha();
        v.scale = node.fade->scale();
    }
    if (node.motion) {
        v.offset = node.motion->offset();
        v.scale *= node.motion->scale();
        v.rotationDeg = node.motion->rotation();
    }
    if (index == m_spotIndex) {
        const float f = m_spotPhase.value();
        v.spotBoost = 4.0F * f * (1.0F - f); // 0 at both wrap edges
        v.scale *= 1.0F + kSpotScale * v.spotBoost;
    }
    return v;
}

} // namespace slc::anim
