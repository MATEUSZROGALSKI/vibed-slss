#include "slc/anim/matrix_choreographer.h"

#include <algorithm>
#include <cmath>

namespace slc::anim {

namespace {

float easeOutCubic(float t)
{
    const float u = 1.0F - std::clamp(t, 0.0F, 1.0F);
    return 1.0F - u * u * u;
}

float easeInOutCubic(float t)
{
    t = std::clamp(t, 0.0F, 1.0F);
    return t < 0.5F ? 4.0F * t * t * t
                    : 1.0F - 0.5F * (2.0F - 2.0F * t) * (2.0F - 2.0F * t) *
                          (2.0F - 2.0F * t);
}

} // namespace

void MatrixChoreographer::start(std::vector<MatrixColumn>& cols,
                                float scrollSec)
{
    m_cols = &cols;
    m_phase = Phase::Scroll;
    m_scrollTarget = scrollSec;
    m_scrollT = 0.0F;
    m_phaseT = 0.0F;
    m_heroT = 0.0F;
    m_flushQueued = false;
}

void MatrixChoreographer::beginHero(std::size_t column, std::size_t cell)
{
    if (m_phase != Phase::Scroll) {
        return;
    }
    m_heroCol = column;
    m_heroCell = cell;
    m_phase = Phase::ZoomIn;
    m_phaseT = 0.0F;
}

void MatrixChoreographer::step(float dtSec)
{
    if (m_cols == nullptr) {
        return;
    }
    const float scrollScale =
        m_phase == Phase::Scroll  ? 1.0F
        : m_phase == Phase::Flush ? kFlushScrollFactor
                                  : kHeroScrollFactor;
    for (auto& c : *m_cols) {
        c.offset += c.speed * dtSec * scrollScale;
        if (c.stackH > 0.0F) {
            // Keep the offset inside the wrap period so draw-side fmod
            // never sees unbounded growth on long sessions.
            c.offset -= c.stackH * std::floor(c.offset / c.stackH);
        }
    }
    switch (m_phase) {
    case Phase::Scroll:
        m_scrollT += dtSec;
        break;
    case Phase::ZoomIn:
        m_phaseT += dtSec;
        m_heroT = easeOutCubic(m_phaseT / kZoomInSec);
        if (m_phaseT >= kZoomInSec) {
            m_phase = Phase::Hero;
            m_phaseT = 0.0F;
            m_heroT = 1.0F;
        }
        break;
    case Phase::Hero:
        m_phaseT += dtSec;
        m_heroT = 1.0F;
        if (m_phaseT >= kHeroSec) {
            m_phase = m_flushQueued ? Phase::Flush : Phase::ZoomOut;
            m_phaseT = 0.0F;
        }
        break;
    case Phase::Flush:
        // heroT stays pinned — the card holds while the marquee races
        // underneath and the app reseeds wrapped cells with the next
        // scene's entries.
        m_phaseT += dtSec;
        m_heroT = 1.0F;
        if (m_phaseT >= kFlushSec) {
            m_phase = Phase::ZoomOut;
            m_phaseT = 0.0F;
            m_flushQueued = false;
        }
        break;
    case Phase::ZoomOut:
        m_phaseT += dtSec;
        m_heroT = 1.0F - easeInOutCubic(m_phaseT / kZoomOutSec);
        if (m_phaseT >= kZoomOutSec) {
            m_phase = Phase::Scroll;
            m_phaseT = 0.0F;
            m_scrollT = 0.0F;
            m_heroT = 0.0F;
        }
        break;
    }
}

} // namespace slc::anim
