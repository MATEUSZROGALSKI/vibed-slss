#include "slc/layout/spiral_strategy.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace slc::layout {

std::vector<ci::Rectf> SpiralStrategy::compute(const ci::Rectf& bounds,
                                               std::size_t cellCount,
                                               std::mt19937& rng) const
{
    if (cellCount == 0 || bounds.getWidth() <= 0.0F ||
        bounds.getHeight() <= 0.0F) {
        return {};
    }

    // Rectangular dimX x dimY grid sized to the window aspect so tiles
    // stay near-square and cover the frame — the old square span left
    // permanent black bars on wide displays, and emitting only cellCount
    // spiral positions left interior holes for non-square counts.
    const float aspect = bounds.getWidth() / bounds.getHeight();
    const auto dimX = static_cast<std::size_t>(std::max(
        1.0F, std::ceil(std::sqrt(
                  static_cast<float>(cellCount) * aspect))));
    const auto dimY =
        std::max<std::size_t>(1, (cellCount + dimX - 1) / dimX);
    const std::size_t gridN = dimX * dimY;

    const float tileW =
        (bounds.getWidth() - m_gutter * static_cast<float>(dimX - 1)) /
        static_cast<float>(dimX);
    const float tileH =
        (bounds.getHeight() - m_gutter * static_cast<float>(dimY - 1)) /
        static_cast<float>(dimY);
    if (tileW <= 0.0F || tileH <= 0.0F) {
        return {};
    }
    const float ox = bounds.getX1();
    const float oy = bounds.getY1();

    // Spiral walk from the centre tile. Direction cycle turns on seeded
    // handedness; the starting direction is seeded too.
    std::uniform_int_distribution<int> dirPick(0, 3);
    std::uniform_int_distribution<int> handedPick(0, 1);
    // Right, Down, Left, Up (clockwise); mirrored order counter-clockwise.
    const int cw = handedPick(rng) == 0 ? 1 : -1;
    int dirIdx = dirPick(rng);

    auto stepDir = [&](int idx) -> ci::ivec2 {
        // CW sequence: (1,0) -> (0,1) -> (-1,0) -> (0,-1).
        // CCW flips the odd entries.
        switch (idx & 3) {
        case 0:
            return {1, 0};
        case 1:
            return {0, cw};
        case 2:
            return {-1, 0};
        default:
            return {0, -cw};
        }
    };

    // Start on the centre-most tile; even dims take the lower-right of
    // the centre candidates (deterministic).
    int x = static_cast<int>(dimX / 2);
    int y = static_cast<int>(dimY / 2);
    if (dimX % 2 == 0) {
        x -= 1;
    }
    if (dimY % 2 == 0) {
        y -= 1;
    }

    std::vector<std::vector<bool>> visited(
        dimY, std::vector<bool>(dimX, false));
    std::vector<ci::Rectf> cells;
    cells.reserve(gridN);

    int runLen = 1;
    int stepsInRun = 0;
    int runsAtLen = 0;
    const auto inBounds = [&](int cx, int cy) {
        return cx >= 0 && cy >= 0 && cx < static_cast<int>(dimX) &&
               cy < static_cast<int>(dimY);
    };

    // Emit centre first, then walk until every slot has a cell.
    auto emit = [&](int cx, int cy) {
        visited[static_cast<std::size_t>(cy)][static_cast<std::size_t>(cx)] =
            true;
        const float fx = ox + static_cast<float>(cx) * (tileW + m_gutter);
        const float fy = oy + static_cast<float>(cy) * (tileH + m_gutter);
        cells.emplace_back(fx, fy, fx + tileW, fy + tileH);
    };

    emit(x, y);
    while (cells.size() < gridN) {
        const auto d = stepDir(dirIdx);
        x += d.x;
        y += d.y;
        ++stepsInRun;
        if (inBounds(x, y) &&
            !visited[static_cast<std::size_t>(y)]
                    [static_cast<std::size_t>(x)]) {
            emit(x, y);
        }
        if (stepsInRun == runLen) {
            stepsInRun = 0;
            dirIdx = (dirIdx + 1) & 3;
            if (++runsAtLen == 2) {
                runsAtLen = 0;
                ++runLen;
            }
        }
    }
    return cells;
}

} // namespace slc::layout
