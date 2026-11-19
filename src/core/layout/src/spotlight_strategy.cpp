#include "slc/layout/spotlight_strategy.h"

#include <algorithm>
#include <vector>

namespace slc::layout {

std::vector<ci::Rectf> SpotlightStrategy::compute(const ci::Rectf& bounds,
                                                  std::size_t cellCount,
                                                  std::mt19937& rng) const
{
    if (cellCount == 0 || bounds.getWidth() <= 0.0F ||
        bounds.getHeight() <= 0.0F) {
        return {};
    }
    if (cellCount == 1) {
        return {bounds};
    }

    const bool landscape = bounds.getWidth() >= bounds.getHeight();
    std::uniform_int_distribution<int> sidePick(0, 1);
    const bool heroFirst = sidePick(rng) == 0;
    const float frac = std::clamp(m_heroFraction, 0.2F, 0.8F);

    ci::Rectf hero;
    ci::Rectf strip;
    if (landscape) {
        const float heroW = bounds.getWidth() * frac - m_gutter * 0.5F;
        if (heroFirst) {
            hero = {bounds.getX1(), bounds.getY1(),
                    bounds.getX1() + heroW, bounds.getY2()};
            strip = {bounds.getX1() + heroW + m_gutter, bounds.getY1(),
                     bounds.getX2(), bounds.getY2()};
        } else {
            hero = {bounds.getX2() - heroW, bounds.getY1(),
                    bounds.getX2(), bounds.getY2()};
            strip = {bounds.getX1(), bounds.getY1(),
                     bounds.getX2() - heroW - m_gutter, bounds.getY2()};
        }
    } else {
        const float heroH = bounds.getHeight() * frac - m_gutter * 0.5F;
        if (heroFirst) {
            hero = {bounds.getX1(), bounds.getY1(),
                    bounds.getX2(), bounds.getY1() + heroH};
            strip = {bounds.getX1(), bounds.getY1() + heroH + m_gutter,
                     bounds.getX2(), bounds.getY2()};
        } else {
            hero = {bounds.getX1(), bounds.getY2() - heroH,
                    bounds.getX2(), bounds.getY2()};
            strip = {bounds.getX1(), bounds.getY1(),
                     bounds.getX2(), bounds.getY2() - heroH - m_gutter};
        }
    }

    std::vector<ci::Rectf> cells;
    cells.reserve(cellCount);
    cells.push_back(hero);

    const std::size_t n = cellCount - 1;
    if (landscape) {
        const float rowH =
            (strip.getHeight() - m_gutter * static_cast<float>(n - 1)) /
            static_cast<float>(n);
        float y = strip.getY1();
        for (std::size_t i = 0; i < n; ++i) {
            const float bottom =
                (i + 1 == n) ? strip.getY2() : y + rowH;
            cells.emplace_back(strip.getX1(), y, strip.getX2(), bottom);
            y = bottom + m_gutter;
        }
    } else {
        const float colW =
            (strip.getWidth() - m_gutter * static_cast<float>(n - 1)) /
            static_cast<float>(n);
        float x = strip.getX1();
        for (std::size_t i = 0; i < n; ++i) {
            const float right =
                (i + 1 == n) ? strip.getX2() : x + colW;
            cells.emplace_back(x, strip.getY1(), right, strip.getY2());
            x = right + m_gutter;
        }
    }
    return cells;
}

} // namespace slc::layout
