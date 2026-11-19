#include "slc/layout/bsp_strategy.h"

#include <algorithm>
#include <vector>

namespace slc::layout {

std::vector<ci::Rectf> BspStrategy::compute(const ci::Rectf& bounds,
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

    std::uniform_real_distribution<float> frac(0.30F, 0.70F);
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);

    // Recursive split: always split the largest leaf so sizes stay balanced;
    // the split axis is biased toward the leaf's longer side.
    std::vector<ci::Rectf> leaves{bounds};
    while (leaves.size() < cellCount) {
        const auto it = std::max_element(
            leaves.begin(), leaves.end(),
            [](const ci::Rectf& a, const ci::Rectf& b) {
                return a.getWidth() * a.getHeight() <
                       b.getWidth() * b.getHeight();
            });
        const ci::Rectf r = *it;
        leaves.erase(it);

        const float w = r.getWidth();
        const float h = r.getHeight();
        // A rect is too small to split when halving its shorter side would
        // drop it under the gutter — fall back to a plain halving.
        const bool vertical =
            (w > h) ? (u01(rng) < 0.75F) : (u01(rng) < 0.25F);
        const float f = frac(rng);
        if (vertical) {
            const float leftW = (w - m_gutter) * f;
            const float rightX = r.getX1() + leftW + m_gutter;
            leaves.emplace_back(r.getX1(), r.getY1(), r.getX1() + leftW,
                                r.getY2());
            leaves.emplace_back(rightX, r.getY1(), r.getX2(), r.getY2());
        } else {
            const float topH = (h - m_gutter) * f;
            const float botY = r.getY1() + topH + m_gutter;
            leaves.emplace_back(r.getX1(), r.getY1(), r.getX2(),
                                r.getY1() + topH);
            leaves.emplace_back(r.getX1(), botY, r.getX2(), r.getY2());
        }
    }
    return leaves;
}

} // namespace slc::layout
