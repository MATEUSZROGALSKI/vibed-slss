#include "slc/layout/rows_strategy.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace slc::layout {

std::vector<ci::Rectf> RowsStrategy::compute(const ci::Rectf& bounds,
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

    // Row count mirrors GridStrategy's squareness heuristic on the
    // transposed aspect.
    const float aspect = bounds.getWidth() / bounds.getHeight();
    const auto rows = static_cast<std::size_t>(std::max(
        1.0F,
        std::round(std::sqrt(static_cast<float>(cellCount) / aspect))));

    // Assign cells to rows round-robin over a seeded shuffle.
    std::vector<std::size_t> order(cellCount);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);
    std::vector<std::vector<std::size_t>> rowCells(rows);
    for (std::size_t i = 0; i < cellCount; ++i) {
        rowCells[order[i] % rows].push_back(i);
    }
    // A row can end up empty for low counts; drop it and renormalize.
    std::erase_if(rowCells, [](const auto& v) { return v.empty(); });
    const auto liveRows = rowCells.size();

    std::uniform_real_distribution<float> jitter(0.70F, 1.30F);

    // Row heights: jittered, scaled to fill bounds exactly.
    std::vector<float> heights(liveRows);
    float totalH = 0.0F;
    for (auto& h : heights) {
        h = jitter(rng);
        totalH += h;
    }
    const float availH =
        bounds.getHeight() - m_gutter * static_cast<float>(liveRows - 1);
    const float hScale = totalH > 0.0F ? availH / totalH : 0.0F;

    std::vector<ci::Rectf> cells(cellCount);
    float y = bounds.getY1();
    for (std::size_t row = 0; row < liveRows; ++row) {
        const float h = heights[row] * hScale;
        // Cell widths inside the row: jittered, scaled to fill.
        const auto& rc = rowCells[row];
        std::vector<float> widths(rc.size());
        float totalW = 0.0F;
        for (auto& w : widths) {
            w = jitter(rng);
            totalW += w;
        }
        const float availW =
            bounds.getWidth() -
            m_gutter * static_cast<float>(rc.size() - 1);
        const float wScale = totalW > 0.0F ? availW / totalW : 0.0F;
        float x = bounds.getX1();
        for (std::size_t k = 0; k < rc.size(); ++k) {
            const float w = widths[k] * wScale;
            cells[rc[k]] = ci::Rectf(x, y, x + w, y + h);
            x += w + m_gutter;
        }
        y += h + m_gutter;
    }
    return cells;
}

} // namespace slc::layout
