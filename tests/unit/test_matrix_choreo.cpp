#include <gtest/gtest.h>

#include "slc/anim/matrix_choreographer.h"

using slc::anim::MatrixChoreographer;
using slc::anim::MatrixColumn;

namespace {

std::vector<MatrixColumn> makeCols(std::size_t n, float stackH = 1000.0F)
{
    std::vector<MatrixColumn> cols(n);
    for (std::size_t i = 0; i < n; ++i) {
        cols[i].x1 = static_cast<float>(i) * 100.0F;
        cols[i].x2 = cols[i].x1 + 90.0F;
        cols[i].speed = (i % 2 == 0 ? -1.0F : 1.0F) * 40.0F;
        cols[i].stackH = stackH;
    }
    return cols;
}

} // namespace

TEST(MatrixChoreo, ColumnsScrollAtOwnSpeedsAndWrap)
{
    auto cols = makeCols(3);
    MatrixChoreographer choreo;
    choreo.start(cols);
    choreo.step(1.0F);
    EXPECT_FLOAT_EQ(cols[0].offset, 960.0F); // -40 wrapped into [0,1000)
    EXPECT_FLOAT_EQ(cols[1].offset, 40.0F);
    choreo.step(26.0F);
    EXPECT_FLOAT_EQ(cols[1].offset, 80.0F); // 40 + 40*26 = 1080 -> 80
}

TEST(MatrixChoreo, HeroCycleRunsItsPhases)
{
    auto cols = makeCols(2);
    MatrixChoreographer choreo;
    choreo.start(cols);

    // Scroll phase: wantsHero only after the scroll window elapses.
    choreo.step(MatrixChoreographer::kScrollSec - 0.5F);
    EXPECT_FALSE(choreo.wantsHero());
    choreo.step(1.0F);
    EXPECT_TRUE(choreo.wantsHero());
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Scroll);

    choreo.beginHero(1, 3);
    EXPECT_EQ(choreo.heroColumn(), 1u);
    EXPECT_EQ(choreo.heroCell(), 3u);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::ZoomIn);

    // Zoom-in eases heroT 0 -> 1, then holds.
    choreo.step(MatrixChoreographer::kZoomInSec * 0.5F);
    const float mid = choreo.heroT();
    EXPECT_GT(mid, 0.0F);
    EXPECT_LT(mid, 1.0F);
    choreo.step(MatrixChoreographer::kZoomInSec);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Hero);
    EXPECT_FLOAT_EQ(choreo.heroT(), 1.0F);

    // Hero holds, then zooms back out and returns to scrolling.
    choreo.step(MatrixChoreographer::kHeroSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::ZoomOut);
    choreo.step(MatrixChoreographer::kZoomOutSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Scroll);
    EXPECT_FLOAT_EQ(choreo.heroT(), 0.0F);
    EXPECT_FALSE(choreo.wantsHero()); // scroll window restarted
}

TEST(MatrixChoreo, ColumnsSlowButKeepScrollingDuringHero)
{
    auto cols = makeCols(2);
    MatrixChoreographer choreo;
    choreo.start(cols);
    choreo.beginHero(0, 0);
    choreo.step(1.0F); // one second during ZoomIn
    // speed 40 * 1s * kHeroScrollFactor (0.25) = 10
    EXPECT_NEAR(cols[1].offset, 10.0F, 0.001F);
}

TEST(MatrixChoreo, BeginHeroIgnoredOutsideScrollPhase)
{
    auto cols = makeCols(2);
    MatrixChoreographer choreo;
    choreo.start(cols);
    choreo.beginHero(0, 0);
    choreo.beginHero(1, 5); // already ZoomIn — must not re-target
    EXPECT_EQ(choreo.heroColumn(), 0u);
    EXPECT_EQ(choreo.heroCell(), 0u);
}

TEST(MatrixChoreo, FlushArmedRunsFastPhaseBeforeZoomOut)
{
    auto cols = makeCols(2);
    MatrixChoreographer choreo;
    choreo.start(cols);
    choreo.beginHero(0, 0);
    choreo.step(MatrixChoreographer::kZoomInSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Hero);

    choreo.setFlushAtHoldEnd(true);
    choreo.step(MatrixChoreographer::kHeroSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Flush);
    EXPECT_FLOAT_EQ(choreo.heroT(), 1.0F); // card stays pinned

    // Columns race at kFlushScrollFactor during the flush.
    const float before = cols[1].offset;
    choreo.step(1.0F);
    EXPECT_NEAR(cols[1].offset - before,
                40.0F * MatrixChoreographer::kFlushScrollFactor, 0.5F);

    choreo.step(MatrixChoreographer::kFlushSec);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::ZoomOut);
    choreo.step(MatrixChoreographer::kZoomOutSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::Scroll);
}

TEST(MatrixChoreo, UnarmedHoldGoesStraightToZoomOut)
{
    auto cols = makeCols(2);
    MatrixChoreographer choreo;
    choreo.start(cols);
    choreo.beginHero(0, 0);
    choreo.step(MatrixChoreographer::kZoomInSec + 0.01F);
    choreo.step(MatrixChoreographer::kHeroSec + 0.01F);
    EXPECT_EQ(choreo.phase(), MatrixChoreographer::Phase::ZoomOut);
}
