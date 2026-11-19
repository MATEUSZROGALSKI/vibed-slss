#include "slc/layout/masonry_strategy.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace slc::layout {

std::vector<ci::Rectf> MasonryStrategy::compute(const ci::Rectf& bounds,
                                               std::size_t cellCount,
                                               std::mt19937& rng) const
{
    if (cellCount == 0 || bounds.getWidth() <= 0.0F || bounds.getHeight() <= 0.0F) {
        return {};
    }

    const float aspect = bounds.getWidth() / bounds.getHeight();
    const auto cols = static_cast<std::size_t>(std::max(
        1.0F, std::round(std::sqrt(static_cast<float>(cellCount) * aspect))));
    const float colW =
        (bounds.getWidth() - m_gutter * static_cast<float>(cols - 1)) /
        static_cast<float>(cols);

    // Assign cells to columns round-robin after a seeded shuffle so tall cells
    // spread evenly across columns.
    std::vector<std::size_t> order(cellCount);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    // Heights: base is bounds/cellsPerCol, jittered ±35% deterministically.
    const std::size_t cellsPerCol = (cellCount + cols - 1) / cols;
    const float availH = bounds.getHeight() -
                         m_gutter * static_cast<float>(cellsPerCol > 0 ? cellsPerCol - 1 : 0);
    const float baseH = availH / static_cast<float>(cellsPerCol);
    std::uniform_real_distribution<float> jitter(0.65F, 1.35F);

    std::vector<std::vector<std::size_t>> columns(cols);
    for (std::size_t i = 0; i < cellCount; ++i) {
        columns[order[i] % cols].push_back(i);
    }

    std::vector<ci::Rectf> cells(cellCount);
    for (std::size_t col = 0; col < cols; ++col) {
        const float x = bounds.getX1() + static_cast<float>(col) * (colW + m_gutter);
        // Scale each column's jittered heights so they fill the column exactly.
        std::vector<float> heights(columns[col].size());
        float totalH = 0.0F;
        for (auto& h : heights) {
            h = baseH * jitter(rng);
            totalH += h;
        }
        const float columnGutters =
            m_gutter * static_cast<float>(heights.empty() ? 0 : heights.size() - 1);
        const float scale = totalH > 0.0F
                                ? (bounds.getHeight() - columnGutters) / totalH
                                : 0.0F;
        float y = bounds.getY1();
        for (std::size_t k = 0; k < columns[col].size(); ++k) {
            const float h = heights[k] * scale;
            cells[columns[col][k]] = ci::Rectf(x, y, x + colW, y + h);
            y += h + m_gutter;
        }
    }
    return cells;
}

} // namespace slc::layout
