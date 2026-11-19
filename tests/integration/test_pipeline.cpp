// doc §9.3 pipeline test: synthetic Steam tree → ContentService →
// LayoutEngine → CollageChoreographer. Asserts on frame-level state (cell
// rects, alphas) rather than pixels — fast and platform-independent.
#include "slc/anim/collage_choreographer.h"
#include "slc/content/content_service.h"
#include "slc/content/steam_local_provider.h"
#include "slc/layout/layout_engine.h"
#include "slc/layout/rows_strategy.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <random>
#include <set>
#include <vector>

namespace {

using slc::anim::CollageChoreographer;
using slc::content::Artwork;
using slc::content::ContentService;
using slc::content::DiscoveryContext;
using slc::content::SteamLocalProvider;
using slc::layout::LayoutEngine;
using slc::layout::RowsStrategy;

constexpr float kDt = 1.0F / 60.0F;
const ci::Rectf kBounds{0.0F, 0.0F, 1920.0F, 1080.0F};

DiscoveryContext fixtureContext()
{
    DiscoveryContext ctx;
    ctx.steamRootOverride =
        std::filesystem::path(SLC_FIXTURES_DIR) / "steam-trees" / "basic";
    return ctx;
}

std::vector<Artwork> collectFixture(const DiscoveryContext& ctx)
{
    ContentService service;
    service.addProvider(std::make_unique<SteamLocalProvider>());
    auto result = service.collect(ctx);
    EXPECT_TRUE(result.has_value());
    return result.value_or(std::vector<Artwork>{});
}

// Distinct games from the artwork list, ordered — what a scene would lay out.
std::vector<std::string> uniqueAppIds(const std::vector<Artwork>& items)
{
    std::set<std::string> ids;
    for (const auto& a : items) {
        ids.insert(a.appId);
    }
    return {ids.begin(), ids.end()};
}

} // namespace

TEST(Pipeline, DiscoveryFeedsLayout)
{
    const auto items = collectFixture(fixtureContext());
    ASSERT_EQ(items.size(), 10u); // known fixture inventory (unit test pins this)

    const auto appIds = uniqueAppIds(items);
    ASSERT_GE(appIds.size(), 4u); // 440, 570, 730, 999

    LayoutEngine engine;
    engine.registerStrategy(std::make_unique<RowsStrategy>());

    for (const char* name : {"grid", "masonry", "spiral", "spotlight", "rows"}) {
        std::mt19937 rng(1234);
        const auto cells =
            engine.compute(name, kBounds, appIds.size(), rng);
        // Frame-covering strategies (spiral, spotlight) may emit more
        // cells than requested to avoid holes.
        ASSERT_GE(cells.size(), appIds.size()) << "strategy " << name;
        for (const auto& cell : cells) {
            EXPECT_TRUE(kBounds.contains(cell.getUpperLeft())) << name;
            EXPECT_GT(cell.getWidth(), 0.0F) << name;
            EXPECT_GT(cell.getHeight(), 0.0F) << name;
        }
    }
}

TEST(Pipeline, SeededLayoutIsDeterministic)
{
    const auto appIds = uniqueAppIds(collectFixture(fixtureContext()));
    LayoutEngine engine;

    for (const char* name : {"grid", "masonry", "spiral", "spotlight"}) {
        std::mt19937 rngA(777), rngB(777);
        const auto a = engine.compute(name, kBounds, appIds.size(), rngA);
        const auto b = engine.compute(name, kBounds, appIds.size(), rngB);
        ASSERT_EQ(a.size(), b.size()) << name;
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_FLOAT_EQ(a[i].x1, b[i].x1) << name << " cell " << i;
            EXPECT_FLOAT_EQ(a[i].y1, b[i].y1) << name << " cell " << i;
            EXPECT_FLOAT_EQ(a[i].x2, b[i].x2) << name << " cell " << i;
            EXPECT_FLOAT_EQ(a[i].y2, b[i].y2) << name << " cell " << i;
        }
    }
}

TEST(Pipeline, DiscoveryToChoreography)
{
    const auto appIds = uniqueAppIds(collectFixture(fixtureContext()));
    LayoutEngine engine;

    std::mt19937 rng(42);
    const auto cells = engine.compute("grid", kBounds, appIds.size(), rng);
    ASSERT_EQ(cells.size(), appIds.size());

    CollageChoreographer choreo(600, 40, rng);
    for (const auto& cell : cells) {
        choreo.addCell(cell, nullptr); // no GL context in tests
    }
    choreo.start();
    ASSERT_EQ(choreo.cellCount(), cells.size());

    // Nothing visible at t=0; everything opaque once staggers elapse.
    EXPECT_NEAR(choreo.cellVisual(0).alpha, 0.0F, 1e-4F);
    for (int i = 0; i < 240; ++i) { // ~4 s
        choreo.step(kDt);
    }
    for (std::size_t i = 0; i < choreo.cellCount(); ++i) {
        EXPECT_GT(choreo.cellVisual(i).alpha, 0.99F) << "cell " << i;
    }
    EXPECT_LT(choreo.spotlightIndex(), choreo.cellCount());
}

TEST(Pipeline, ExclusionsFlowThroughToEmpty)
{
    // Excluding every fixture appId must yield a clean empty result — the
    // graceful-fallback path, not an error.
    auto ctx = fixtureContext();
    for (const char* id : {"440", "570", "730", "999", "555555", "666666"}) {
        ctx.excludeAppIds.insert(id);
    }
    const auto items = collectFixture(ctx);
    EXPECT_TRUE(items.empty());

    // An empty artwork set lays out to zero cells and an empty choreographer —
    // the host renders the graceful gradient for this state.
    LayoutEngine engine;
    std::mt19937 rng(1);
    const auto cells = engine.compute("grid", kBounds, items.size(), rng);
    EXPECT_TRUE(cells.empty());

    CollageChoreographer choreo(600, 40, rng);
    EXPECT_EQ(choreo.cellCount(), 0u);
}

TEST(Pipeline, SingleExclusionDropsOnlyThatGame)
{
    auto ctx = fixtureContext();
    ctx.excludeAppIds.insert("440");
    const auto items = collectFixture(ctx);
    EXPECT_EQ(items.size(), 6u); // 10 minus 440's grid+hero+header+logo
    for (const auto& a : items) {
        EXPECT_NE(a.appId, "440");
    }
}
