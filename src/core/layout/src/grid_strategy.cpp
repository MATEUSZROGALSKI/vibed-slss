#include "slc/layout/grid_strategy.h"

#include <algorithm>
#include <cmath>

namespace slc::layout {

std::vector<ci::Rectf> GridStrategy::compute(const ci::Rectf& bounds,
                                             std::size_t cellCount,
                                             std::mt19937& /*rng*/) const
{
    if (cellCount == 0 || bounds.getWidth() <= 0.0F || bounds.getHeight() <= 0.0F) {
        return {};
    }

    const float aspect = bounds.getWidth() / bounds.getHeight();
    // cols ~= sqrt(n * aspect) keeps cells close to square.
    const auto cols = static_cast<std::size_t>(std::max(
        1.0F, std::round(std::sqrt(static_cast<float>(cellCount) * aspect))));
    const std::size_t rows = (cellCount + cols - 1) / cols;

    std::vector<ci::Rectf> cells;
    cells.reserve(cellCount);
    std::size_t placed = 0;
    for (std::size_t row = 0; row < rows && placed < cellCount; ++row) {
        const std::size_t inRow = std::min(cols, cellCount - placed);
        // Last row may be partially filled; stretch it to keep coverage even.
        const float rowCellW =
            (bounds.getWidth() - m_gutter * static_cast<float>(inRow - 1)) /
            static_cast<float>(inRow);
        const float cellH =
            (bounds.getHeight() - m_gutter * static_cast<float>(rows - 1)) /
            static_cast<float>(rows);
        const float y = bounds.getY1() + static_cast<float>(row) * (cellH + m_gutter);
        for (std::size_t col = 0; col < inRow; ++col, ++placed) {
            const float x =
                bounds.getX1() + static_cast<float>(col) * (rowCellW + m_gutter);
            cells.emplace_back(x, y, x + rowCellW, y + cellH);
        }
    }
    return cells;
}

} // namespace slc::layout
