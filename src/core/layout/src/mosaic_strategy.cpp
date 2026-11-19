#include "slc/layout/mosaic_strategy.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace slc::layout {

std::vector<ci::Rectf> MosaicStrategy::compute(const ci::Rectf& bounds,
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

    const float aspect = bounds.getWidth() / bounds.getHeight();
    // A merged cell consumes 2 grid slots while emitting 1 tile, so the
    // grid is sized for cellCount + merge budget to never run short.
    const std::size_t mergeBudget =
        std::min(cellCount / 3, cellCount > 1 ? cellCount - 1 : 0);
    const std::size_t slotTarget = cellCount + mergeBudget;
    const auto cols = static_cast<std::size_t>(std::max(
        2.0F, std::round(std::sqrt(static_cast<float>(slotTarget) * aspect))));
    const std::size_t rows = (slotTarget + cols - 1) / cols;
    const float cellW =
        (bounds.getWidth() - m_gutter * static_cast<float>(cols - 1)) /
        static_cast<float>(cols);
    const float cellH =
        (bounds.getHeight() - m_gutter * static_cast<float>(rows - 1)) /
        static_cast<float>(rows);
    if (cellW <= 0.0F || cellH <= 0.0F) {
        return {};
    }

    const auto tile = [&](std::size_t c, std::size_t r) {
        const float x =
            bounds.getX1() + static_cast<float>(c) * (cellW + m_gutter);
        const float y =
            bounds.getY1() + static_cast<float>(r) * (cellH + m_gutter);
        return ci::Rectf(x, y, x + cellW, y + cellH);
    };

    // Seeded merge pass: pick random grid cells to claim a free right or
    // bottom neighbour, turning them into 2x1/1x2 spans.
    const std::size_t gridN = cols * rows;
    std::vector<std::size_t> slots(gridN);
    std::iota(slots.begin(), slots.end(), 0);
    std::shuffle(slots.begin(), slots.end(), rng);
    std::vector<bool> claimed(gridN, false);
    std::uniform_real_distribution<float> u01(0.0F, 1.0F);
    std::size_t mergesLeft = mergeBudget;

    // Every grid slot must emit a cell — stopping at cellCount leaves
    // unclaimed slots as permanent black holes in the collage. Merges are
    // taken when the seeded draw hits, and forced once the remaining
    // slots can no longer absorb the merge budget, so the emitted count
    // stays near cellCount while coverage stays complete.
    std::vector<ci::Rectf> cells;
    cells.reserve(gridN);
    std::size_t slotsLeft = gridN;
    for (const std::size_t s : slots) {
        if (claimed[s]) {
            --slotsLeft;
            continue;
        }
        const std::size_t c = s % cols;
        const std::size_t r = s / cols;
        const bool canR = c + 1 < cols && !claimed[s + 1];
        const bool canD = r + 1 < rows && !claimed[s + cols];
        const bool mustMerge = mergesLeft > 0 && slotsLeft <= mergesLeft;
        if (mergesLeft > 0 && (canR || canD) &&
            (mustMerge || u01(rng) < 0.55F)) {
            --mergesLeft;
            if (canR && (!canD || u01(rng) < 0.5F)) {
                ci::Rectf a = tile(c, r);
                ci::Rectf b = tile(c + 1, r);
                a.include(b);
                cells.push_back(a);
                claimed[s + 1] = true;
            } else {
                ci::Rectf a = tile(c, r);
                ci::Rectf b = tile(c, r + 1);
                a.include(b);
                cells.push_back(a);
                claimed[s + cols] = true;
            }
        } else {
            cells.push_back(tile(c, r));
        }
        claimed[s] = true;
        --slotsLeft;
    }
    return cells;
}

} // namespace slc::layout
