// Collage overscan: the scale returned by cellOverscanScale must keep the
// clip rect covered by the drawn quad under worst-case drift + rotation.
// Regression: a flat longest-side margin under-covers elongated cells —
// the short axis must absorb rotation lift produced by the long axis.
#include "slc/anim/overscan.h"

#include <cinder/CinderMath.h>
#include <cinder/Rect.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using slc::anim::cellOverscanScale;

namespace {

// Simulate the draw transform: quad = `drawn` scaled by `s` about a pivot
// displaced by `offset` from the clip centre and rotated by `rotDeg`.
// Returns true when every clip corner lands inside the quad's own frame.
bool coversAt(const ci::Rectf& drawn, const ci::Rectf& clip, float s,
              const ci::vec2& offset, float rotDeg)
{
    const ci::vec2 pivot = clip.getCenter() + offset;
    const float sn = std::sin(ci::toRadians(rotDeg));
    const float cs = std::cos(ci::toRadians(rotDeg));
    const float halfW = drawn.getWidth() * s * 0.5F;
    const float halfH = drawn.getHeight() * s * 0.5F;
    for (const ci::vec2 corner :
         {clip.getUpperLeft(), clip.getUpperRight(),
          clip.getLowerLeft(), clip.getLowerRight()}) {
        const ci::vec2 d = corner - pivot;
        // Into the quad's frame (undo the rotation).
        const float qx = d.x * cs + d.y * sn;
        const float qy = -d.x * sn + d.y * cs;
        if (std::fabs(qx) > halfW + 1e-3F ||
            std::fabs(qy) > halfH + 1e-3F) {
            return false;
        }
    }
    return true;
}

// Exhaust the motion extremes: drift at each axis bound combined with the
// max rotation in both directions.
bool coversAtExtremes(const ci::Rectf& drawn, const ci::Rectf& clip,
                      float s, float driftPx, float maxRotDeg)
{
    for (const float sx : {-1.0F, 1.0F}) {
        for (const float sy : {-1.0F, 1.0F}) {
            for (const float rot : {-maxRotDeg, maxRotDeg}) {
                if (!coversAt(drawn, clip, s,
                              {sx * driftPx, sy * driftPx}, rot)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The margin the old draw code used: 1 + 2*drift/longestSide + 0.08.
float legacyInflate(const ci::Rectf& clip, float driftPx)
{
    const float side =
        std::max(clip.getWidth(), clip.getHeight());
    return 1.0F + 2.0F * driftPx / side + 0.08F;
}

} // namespace

TEST(CellOverscan, NoMotionStillCovers)
{
    const ci::Rectf clip(0, 0, 800, 600);
    const float s = cellOverscanScale(clip, clip, 0.0F, 0.0F);
    EXPECT_TRUE(coversAtExtremes(clip, clip, s, 0.0F, 0.0F));
}

TEST(CellOverscan, SquareCellDriftAndRotation)
{
    const ci::Rectf clip(0, 0, 750, 700);
    const float s = cellOverscanScale(clip, clip, 40.0F, 1.6F);
    EXPECT_TRUE(coversAtExtremes(clip, clip, s, 40.0F, 1.6F));
    // And the margin is not wildly oversized.
    EXPECT_LT(s, 1.4F);
}

TEST(CellOverscan, WideCellIsNotUndercovered)
{
    // 4:1 cell with aspect-matched art: the worst case. The legacy
    // longest-side margin leaves a band exposed at the top/bottom edges.
    const ci::Rectf clip(0, 0, 1600, 400);
    const float legacy = legacyInflate(clip, 40.0F);
    EXPECT_FALSE(coversAtExtremes(clip, clip, legacy, 40.0F, 1.6F))
        << "legacy margin unexpectedly covers — test assumptions changed";

    const float s = cellOverscanScale(clip, clip, 40.0F, 1.6F);
    EXPECT_TRUE(coversAtExtremes(clip, clip, s, 40.0F, 1.6F));
}

TEST(CellOverscan, TallCellIsNotUndercovered)
{
    const ci::Rectf clip(0, 0, 400, 1600);
    const float s = cellOverscanScale(clip, clip, 40.0F, 1.6F);
    EXPECT_TRUE(coversAtExtremes(clip, clip, s, 40.0F, 1.6F));
}

TEST(CellOverscan, DriftOnlyWideCell)
{
    // Cells seeded with the drift animator do not rotate, but still pan
    // the full driftPx on the short axis.
    const ci::Rectf clip(0, 0, 1600, 400);
    const float s = cellOverscanScale(clip, clip, 40.0F, 0.0F);
    EXPECT_TRUE(coversAtExtremes(clip, clip, s, 40.0F, 0.0F));
    EXPECT_GE(s, (400.0F + 2.0F * 40.0F) / 400.0F);
}

TEST(CellOverscan, OverscanLargerThanCellCoversTrivially)
{
    // Cover-fit art often overflows the cell on one axis — the margin
    // must never shrink the quad below its plain cover size.
    const ci::Rectf drawn(-200, 0, 1800, 400); // covers 1600x400 + slack
    const ci::Rectf clip(0, 0, 1600, 400);
    const float s = cellOverscanScale(drawn, clip, 40.0F, 1.6F);
    EXPECT_GE(s, 1.0F);
    EXPECT_TRUE(coversAtExtremes(drawn, clip, s, 40.0F, 1.6F));
}
