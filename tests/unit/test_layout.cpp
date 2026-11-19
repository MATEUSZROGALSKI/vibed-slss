#include "slc/layout/bsp_strategy.h"
#include "slc/layout/grid_strategy.h"
#include "slc/layout/layout_engine.h"
#include "slc/layout/masonry_strategy.h"
#include "slc/layout/mosaic_strategy.h"
#include "slc/layout/rows_strategy.h"
#include "slc/layout/spiral_strategy.h"
#include "slc/layout/spotlight_strategy.h"

#include <gtest/gtest.h>

#include <random>

using slc::layout::GridStrategy;
using slc::layout::LayoutEngine;
using slc::layout::MasonryStrategy;
using slc::layout::SpiralStrategy;
using slc::layout::SpotlightStrategy;

namespace {
const ci::Rectf kBounds(0.0F, 0.0F, 1920.0F, 1080.0F);

// ci::RectT has no operator==; compare corners directly.
bool rectsEqual(const ci::Rectf& a, const ci::Rectf& b)
{
    return a.getX1() == b.getX1() && a.getY1() == b.getY1() &&
           a.getX2() == b.getX2() && a.getY2() == b.getY2();
}
} // namespace

TEST(GridStrategy, CoversBounds)
{
    GridStrategy grid(/*gutter=*/12.0F);
    std::mt19937 rng(42);
    const auto cells = grid.compute(kBounds, 12, rng);
    ASSERT_EQ(cells.size(), 12u);
    for (const auto& c : cells) {
        EXPECT_GE(c.getX1(), kBounds.getX1());
        EXPECT_GE(c.getY1(), kBounds.getY1());
        EXPECT_LE(c.getX2(), kBounds.getX2() + 0.5F);
        EXPECT_LE(c.getY2(), kBounds.getY2() + 0.5F);
        EXPECT_GT(c.getWidth(), 0.0F);
        EXPECT_GT(c.getHeight(), 0.0F);
    }
}

TEST(GridStrategy, DegenerateInputs)
{
    GridStrategy grid;
    std::mt19937 rng(1);
    EXPECT_TRUE(grid.compute(kBounds, 0, rng).empty());
    EXPECT_TRUE(grid.compute(ci::Rectf(0, 0, 0, 0), 5, rng).empty());
    EXPECT_EQ(grid.compute(kBounds, 1, rng).size(), 1u);
}

TEST(MasonryStrategy, DeterministicPerSeed)
{
    MasonryStrategy masonry;
    std::mt19937 rngA(1337);
    std::mt19937 rngB(1337);
    const auto a = masonry.compute(kBounds, 12, rngA);
    const auto b = masonry.compute(kBounds, 12, rngB);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(rectsEqual(a[i], b[i]));
    }
}

TEST(MasonryStrategy, DifferentSeedDiffers)
{
    MasonryStrategy masonry;
    std::mt19937 rngA(1);
    std::mt19937 rngB(2);
    const auto a = masonry.compute(kBounds, 12, rngA);
    const auto b = masonry.compute(kBounds, 12, rngB);
    ASSERT_EQ(a.size(), b.size());
    bool differs = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        differs = differs || !rectsEqual(a[i], b[i]);
    }
    EXPECT_TRUE(differs);
}

TEST(LayoutEngine, FallsBackToGridForUnknown)
{
    LayoutEngine engine;
    std::mt19937 rng(7);
    EXPECT_EQ(engine.compute("no-such-strategy", kBounds, 4, rng).size(), 4u);
    EXPECT_TRUE(engine.compute("masonry", kBounds, 0, rng).empty());
}

TEST(SpiralStrategy, EmitsAllCellsInsideBounds)
{
    SpiralStrategy spiral(12.0F);
    std::mt19937 rng(42);
    const auto cells = spiral.compute(kBounds, 12, rng);
    ASSERT_GE(cells.size(), 12u);
    for (const auto& c : cells) {
        EXPECT_GE(c.getX1(), kBounds.getX1());
        EXPECT_GE(c.getY1(), kBounds.getY1());
        EXPECT_LE(c.getX2(), kBounds.getX2() + 0.5F);
        EXPECT_LE(c.getY2(), kBounds.getY2() + 0.5F);
        EXPECT_GT(c.getWidth(), 0.0F);
        EXPECT_GT(c.getHeight(), 0.0F);
    }
}

TEST(SpiralStrategy, FirstCellIsCentral)
{
    SpiralStrategy spiral(12.0F);
    std::mt19937 rng(42);
    const auto cells = spiral.compute(kBounds, 12, rng);
    const ci::vec2 centre = kBounds.getCenter();
    // Cell 0 must contain (or be closest to) the bounds centre.
    float best = 1e30F;
    for (const auto& c : cells) {
        const ci::vec2 d = c.getCenter() - centre;
        best = std::min(best, d.x * d.x + d.y * d.y);
    }
    const ci::vec2 d0 = cells[0].getCenter() - centre;
    EXPECT_NEAR(d0.x * d0.x + d0.y * d0.y, best, 1.0F);
}

TEST(SpiralStrategy, DeterministicPerSeed)
{
    SpiralStrategy spiral(12.0F);
    std::mt19937 rngA(11), rngB(11);
    const auto a = spiral.compute(kBounds, 9, rngA);
    const auto b = spiral.compute(kBounds, 9, rngB);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(rectsEqual(a[i], b[i]));
    }
}

TEST(SpotlightStrategy, HeroPlusStripFillsBounds)
{
    SpotlightStrategy spot(12.0F);
    std::mt19937 rng(42);
    const auto cells = spot.compute(kBounds, 5, rng);
    ASSERT_EQ(cells.size(), 5u);
    const auto& hero = cells[0];
    EXPECT_GT(hero.getWidth(), kBounds.getWidth() * 0.4F);
    for (const auto& c : cells) {
        EXPECT_GE(c.getX1(), kBounds.getX1());
        EXPECT_GE(c.getY1(), kBounds.getY1());
        EXPECT_LE(c.getX2(), kBounds.getX2() + 0.5F);
        EXPECT_LE(c.getY2(), kBounds.getY2() + 0.5F);
        EXPECT_GT(c.getWidth(), 0.0F);
        EXPECT_GT(c.getHeight(), 0.0F);
    }
    // Strip cells are narrower than the hero.
    for (std::size_t i = 1; i < cells.size(); ++i) {
        EXPECT_LT(cells[i].getWidth(), hero.getWidth());
    }
}

TEST(SpotlightStrategy, SingleCellAndPortrait)
{
    SpotlightStrategy spot(12.0F);
    std::mt19937 rng(42);
    const auto one = spot.compute(kBounds, 1, rng);
    ASSERT_EQ(one.size(), 1u);
    EXPECT_TRUE(rectsEqual(one[0], kBounds));

    const ci::Rectf portrait(0, 0, 600, 1200);
    const auto cells = spot.compute(portrait, 4, rng);
    ASSERT_EQ(cells.size(), 4u);
    // Portrait: hero is the tall top/bottom band, strip cells are columns.
    EXPECT_GT(cells[0].getHeight(), portrait.getHeight() * 0.4F);
}

TEST(SpotlightStrategy, DeterministicPerSeed)
{
    SpotlightStrategy spot(12.0F);
    std::mt19937 rngA(3), rngB(3);
    const auto a = spot.compute(kBounds, 6, rngA);
    const auto b = spot.compute(kBounds, 6, rngB);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_TRUE(rectsEqual(a[i], b[i]));
    }
}

namespace {

void expectAllInsideAndPositive(const std::vector<ci::Rectf>& cells,
                                const ci::Rectf& bounds)
{
    for (const auto& c : cells) {
        EXPECT_GE(c.getX1(), bounds.getX1() - 0.5F);
        EXPECT_GE(c.getY1(), bounds.getY1() - 0.5F);
        EXPECT_LE(c.getX2(), bounds.getX2() + 0.5F);
        EXPECT_LE(c.getY2(), bounds.getY2() + 0.5F);
        EXPECT_GT(c.getWidth(), 0.0F);
        EXPECT_GT(c.getHeight(), 0.0F);
    }
}

// Cells must not overlap (gutter may shrink to 0 after snapping).
void expectNoOverlap(const std::vector<ci::Rectf>& cells)
{
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            EXPECT_FALSE(cells[i].intersects(cells[j]))
                << "cells " << i << " and " << j << " overlap";
        }
    }
}

// The union of emitted cells must cover essentially the whole frame —
// uncovered regions render as permanent black holes in the collage.
void expectCoversBounds(const std::vector<ci::Rectf>& cells,
                        const ci::Rectf& bounds)
{
    float covered = 0.0F;
    for (const auto& c : cells) {
        covered += c.getWidth() * c.getHeight();
    }
    const float total = bounds.getWidth() * bounds.getHeight();
    EXPECT_GE(covered, total * 0.92F)
        << "cells cover only " << covered / total * 100.0F << "% of bounds";
}

} // namespace

TEST(BspStrategy, CountAndCoverage)
{
    slc::layout::BspStrategy bsp(12.0F);
    std::mt19937 rng(7);
    for (std::size_t n : {2u, 5u, 9u, 14u}) {
        const auto cells = bsp.compute(kBounds, n, rng);
        ASSERT_EQ(cells.size(), n);
        expectAllInsideAndPositive(cells, kBounds);
        expectNoOverlap(cells);
    }
}

TEST(BspStrategy, DeterministicAndVaried)
{
    slc::layout::BspStrategy bsp(12.0F);
    std::mt19937 a(11), b(11), c(99);
    const auto ca = bsp.compute(kBounds, 8, a);
    const auto cb = bsp.compute(kBounds, 8, b);
    const auto cc = bsp.compute(kBounds, 8, c);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); ++i) {
        EXPECT_TRUE(rectsEqual(ca[i], cb[i]));
    }
    bool anyDiff = false;
    for (std::size_t i = 0; i < std::min(ca.size(), cc.size()); ++i) {
        anyDiff = anyDiff || !rectsEqual(ca[i], cc[i]);
    }
    EXPECT_TRUE(anyDiff);
}

TEST(RowsStrategy, CountAndCoverage)
{
    slc::layout::RowsStrategy rows(12.0F);
    std::mt19937 rng(5);
    for (std::size_t n : {2u, 4u, 7u, 12u}) {
        const auto cells = rows.compute(kBounds, n, rng);
        ASSERT_EQ(cells.size(), n);
        expectAllInsideAndPositive(cells, kBounds);
        expectNoOverlap(cells);
    }
}

TEST(RowsStrategy, DeterministicPerSeed)
{
    slc::layout::RowsStrategy rows(12.0F);
    std::mt19937 a(21), b(21);
    const auto ca = rows.compute(kBounds, 6, a);
    const auto cb = rows.compute(kBounds, 6, b);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); ++i) {
        EXPECT_TRUE(rectsEqual(ca[i], cb[i]));
    }
}

TEST(MosaicStrategy, CountAndCoverage)
{
    slc::layout::MosaicStrategy mosaic(12.0F);
    std::mt19937 rng(9);
    for (std::size_t n : {2u, 4u, 6u, 10u, 15u}) {
        const auto cells = mosaic.compute(kBounds, n, rng);
        // Merges may underperform their budget — more cells than asked is
        // fine, but every grid slot must be covered (no black holes).
        ASSERT_GE(cells.size(), n);
        expectAllInsideAndPositive(cells, kBounds);
        expectNoOverlap(cells);
        expectCoversBounds(cells, kBounds);
    }
}

TEST(SpiralStrategy, CoversBounds)
{
    std::mt19937 rng(3);
    for (std::size_t n : {2u, 4u, 5u, 6u, 8u, 12u}) {
        const auto cells = slc::layout::SpiralStrategy(12.0F).compute(
            kBounds, n, rng);
        ASSERT_GE(cells.size(), n);
        expectAllInsideAndPositive(cells, kBounds);
        expectNoOverlap(cells);
        expectCoversBounds(cells, kBounds);
    }
}

TEST(SpiralStrategy, CoversUltrawideBounds)
{
    // 21:9 — the old square-span spiral left ~1000px black bars per side.
    const ci::Rectf ultrawide(0.0F, 0.0F, 3440.0F, 1440.0F);
    std::mt19937 rng(11);
    for (std::size_t n : {4u, 6u, 10u}) {
        const auto cells = slc::layout::SpiralStrategy(12.0F).compute(
            ultrawide, n, rng);
        ASSERT_GE(cells.size(), n);
        expectAllInsideAndPositive(cells, ultrawide);
        expectNoOverlap(cells);
        expectCoversBounds(cells, ultrawide);
    }
}

TEST(MosaicStrategy, CoversUltrawideBounds)
{
    const ci::Rectf ultrawide(0.0F, 0.0F, 3440.0F, 1440.0F);
    std::mt19937 rng(5);
    for (std::size_t n : {4u, 6u, 10u}) {
        const auto cells =
            slc::layout::MosaicStrategy(12.0F).compute(ultrawide, n, rng);
        ASSERT_GE(cells.size(), n);
        expectAllInsideAndPositive(cells, ultrawide);
        expectNoOverlap(cells);
        expectCoversBounds(cells, ultrawide);
    }
}

TEST(MosaicStrategy, DeterministicPerSeed)
{
    slc::layout::MosaicStrategy mosaic(12.0F);
    std::mt19937 a(13), b(13);
    const auto ca = mosaic.compute(kBounds, 9, a);
    const auto cb = mosaic.compute(kBounds, 9, b);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); ++i) {
        EXPECT_TRUE(rectsEqual(ca[i], cb[i]));
    }
}
