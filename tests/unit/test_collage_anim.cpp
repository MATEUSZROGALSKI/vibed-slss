// M4 collage choreography: staggered fade-in, drift bounds, spotlight,
// determinism, pause/reset, and no-pop frame deltas at a fixed timestep.
#include "slc/anim/collage_choreographer.h"

#include <cinder/Rect.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>

using slc::anim::CellVisual;
using slc::anim::CollageChoreographer;

namespace {

constexpr float kDt = 1.0F / 60.0F;

std::unique_ptr<CollageChoreographer> makeChoreo(std::mt19937& rng,
                                                 int cells = 6)
{
    auto c = std::make_unique<CollageChoreographer>(600, 40, rng);
    for (int i = 0; i < cells; ++i) {
        c->addCell(ci::Rectf(i * 100.0F, 0.0F, i * 100.0F + 90.0F, 90.0F),
                   nullptr);
    }
    c->start();
    return c;
}

} // namespace

TEST(CollageChoreo, CellsFadeInStaggered)
{
    std::mt19937 rng(42);
    auto c = makeChoreo(rng, 6);

    // At t=0 nothing is visible.
    EXPECT_NEAR(c->cellVisual(0).alpha, 0.0F, 1e-4F);

    // After 0.6s cell 0 is fully in; cell 5 (delay 5*0.24=1.2s) has not
    // started.
    for (int i = 0; i < 40; ++i) {
        c->step(kDt); // ~0.667s
    }
    EXPECT_GT(c->cellVisual(0).alpha, 0.95F);
    EXPECT_NEAR(c->cellVisual(5).alpha, 0.0F, 1e-3F);

    // After all staggers elapse every cell is opaque.
    for (int i = 0; i < 120; ++i) {
        c->step(kDt);
    }
    for (std::size_t i = 0; i < c->cellCount(); ++i) {
        EXPECT_GT(c->cellVisual(i).alpha, 0.99F);
        EXPECT_NEAR(c->cellVisual(i).scale, 1.0F,
                    0.07F + 1e-3F); // spotlight bump <= 5%
    }
}

TEST(CollageChoreo, DriftBoundedAndMoving)
{
    std::mt19937 rng(7);
    auto c = makeChoreo(rng, 4);

    bool sawMotion = false;
    for (int i = 0; i < 600; ++i) { // 10 s
        c->step(kDt);
        const auto v = c->cellVisual(0);
        const float len = std::sqrt(v.offset.x * v.offset.x +
                                    v.offset.y * v.offset.y);
        EXPECT_LE(len, 40.0F + 1e-3F); // never exceeds driftPx
        if (len > 1.0F) {
            sawMotion = true;
        }
    }
    EXPECT_TRUE(sawMotion);
}

TEST(CollageChoreo, DeterministicPerSeed)
{
    std::mt19937 r1(123), r2(123);
    auto a = makeChoreo(r1);
    auto b = makeChoreo(r2);

    for (int i = 0; i < 300; ++i) {
        a->step(kDt);
        b->step(kDt);
    }
    for (std::size_t i = 0; i < a->cellCount(); ++i) {
        const auto va = a->cellVisual(i);
        const auto vb = b->cellVisual(i);
        EXPECT_FLOAT_EQ(va.alpha, vb.alpha);
        EXPECT_FLOAT_EQ(va.scale, vb.scale);
        EXPECT_FLOAT_EQ(va.offset.x, vb.offset.x);
        EXPECT_FLOAT_EQ(va.offset.y, vb.offset.y);
    }
    EXPECT_EQ(a->spotlightIndex(), b->spotlightIndex());
}

TEST(CollageChoreo, NoPopsAcrossSpotlightWrap)
{
    std::mt19937 rng(9);
    auto c = makeChoreo(rng, 6);

    // Run well past one full spotlight period (12s) plus the whole intro.
    const int steps = static_cast<int>((3.0 + 2.0 * 12.0) / kDt);
    std::vector<CellVisual> prev(c->cellCount());
    for (int s = 0; s < steps; ++s) {
        c->step(kDt);
        for (std::size_t i = 0; i < c->cellCount(); ++i) {
            const auto v = c->cellVisual(i);
            if (s > 0) {
                // easeOutCubic steepest slope: 3/fadeSec per sec ->
                // ~0.083 alpha per 60Hz frame at fadeSec=0.6.
                EXPECT_LE(std::fabs(v.alpha - prev[i].alpha), 0.1F)
                    << "alpha pop cell " << i << " step " << s;
                EXPECT_LE(std::fabs(v.scale - prev[i].scale), 0.02F)
                    << "scale pop cell " << i << " step " << s;
                const float dx = v.offset.x - prev[i].offset.x;
                const float dy = v.offset.y - prev[i].offset.y;
                EXPECT_LE(std::sqrt(dx * dx + dy * dy), 4.0F)
                    << "drift pop cell " << i << " step " << s;
            }
            prev[i] = v;
        }
    }
}

TEST(CollageChoreo, SpotlightPulseIsSmooth)
{
    std::mt19937 rng(5);
    auto c = makeChoreo(rng, 4);

    bool sawBoost = false;
    for (int i = 0; i < static_cast<int>(13.0 / kDt); ++i) {
        c->step(kDt);
        const std::size_t idx = c->spotlightIndex();
        const auto v = c->cellVisual(idx);
        EXPECT_GE(v.spotBoost, 0.0F);
        EXPECT_LE(v.spotBoost, 1.0F);
        // Non-spotlit cells get zero boost.
        for (std::size_t j = 0; j < c->cellCount(); ++j) {
            if (j != idx) {
                EXPECT_EQ(c->cellVisual(j).spotBoost, 0.0F);
            }
        }
        if (v.spotBoost > 0.5F) {
            sawBoost = true;
        }
    }
    EXPECT_TRUE(sawBoost); // pulse peaks at 1.0 mid-period
}

TEST(CollageChoreo, PauseFreezesAndResetRestarts)
{
    std::mt19937 rng(77);
    auto c = makeChoreo(rng, 4);

    for (int i = 0; i < 30; ++i) {
        c->step(kDt);
    }
    const auto before = c->cellVisual(0);
    c->pause();
    for (int i = 0; i < 120; ++i) {
        c->step(kDt);
    }
    const auto paused = c->cellVisual(0);
    EXPECT_FLOAT_EQ(paused.alpha, before.alpha);
    EXPECT_FLOAT_EQ(paused.offset.x, before.offset.x);

    c->reset();
    EXPECT_NEAR(c->cellVisual(0).alpha, 0.0F, 1e-4F);
}

TEST(CollageChoreo, DisabledFadeStartsOpaque)
{
    std::mt19937 rng(3);
    CollageChoreographer c(0, 40, rng);
    c.addCell(ci::Rectf(0, 0, 100, 100), nullptr);
    c.start();
    c.step(kDt);
    EXPECT_FLOAT_EQ(c.cellVisual(0).alpha, 1.0F);
}
