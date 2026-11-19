#include "slc/anim/ken_burns_animator.h"

#include <cinder/Timeline.h>

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using slc::anim::KenBurnsAnimator;

TEST(KenBurns, SameSeedSameAnchor)
{
    std::mt19937 a(42), b(42);
    KenBurnsAnimator ka(1.35F, 1.0F, 8000, a);
    KenBurnsAnimator kb(1.35F, 1.0F, 8000, b);
    EXPECT_EQ(ka.anchor(), kb.anchor());
}

TEST(KenBurns, DifferentSeedDifferentAnchor)
{
    std::mt19937 a(1), b(2);
    KenBurnsAnimator ka(1.35F, 1.0F, 8000, a);
    KenBurnsAnimator kb(1.35F, 1.0F, 8000, b);
    EXPECT_NE(ka.anchor(), kb.anchor());
}

TEST(KenBurns, AnchorLiesOnEdge)
{
    for (unsigned seed = 0; seed < 64; ++seed) {
        std::mt19937 rng(seed);
        KenBurnsAnimator k(1.35F, 1.0F, 8000, rng);
        const float extent =
            std::max(std::abs(k.anchor().x), std::abs(k.anchor().y));
        EXPECT_NEAR(extent, 1.0F, 1e-5F) << "seed " << seed;
    }
}

TEST(KenBurns, EasesFromZoomFromToZoomTo)
{
    std::mt19937 rng(7);
    KenBurnsAnimator k(1.35F, 1.0F, 8000, rng);
    auto tl = ci::Timeline::create();
    k.attach(*tl, ci::Rectf(), nullptr);

    tl->step(0.0F);
    EXPECT_NEAR(k.zoom(), 1.35F, 1e-4F);

    tl->step(4.0F); // halfway in time -> easeOutCubic is well past midpoint
    EXPECT_LT(k.zoom(), 1.35F);
    EXPECT_GT(k.zoom(), 1.0F);

    tl->step(4.1F); // past the end -> clamps at zoomTo, centred
    EXPECT_NEAR(k.zoom(), 1.0F, 1e-3F);
    EXPECT_NEAR(k.pan().x, 0.0F, 1e-3F);
    EXPECT_NEAR(k.pan().y, 0.0F, 1e-3F);
}

TEST(KenBurns, EaseOutCubicIsFrontLoaded)
{
    std::mt19937 rng(3);
    KenBurnsAnimator k(1.35F, 1.0F, 8000, rng);
    auto tl = ci::Timeline::create();
    k.attach(*tl, ci::Rectf(), nullptr);

    // easeOutCubic(0.25) = 1 - (0.75)^3 ≈ 0.578 > 0.25 linear progress.
    tl->step(2.0F);
    const float progress = (1.35F - k.zoom()) / (1.35F - 1.0F);
    EXPECT_GT(progress, 0.5F);
}

TEST(KenBurns, PanStartsAtAnchor)
{
    std::mt19937 rng(11);
    KenBurnsAnimator k(1.35F, 1.0F, 8000, rng);
    auto tl = ci::Timeline::create();
    k.attach(*tl, ci::Rectf(), nullptr);

    tl->step(0.0F);
    EXPECT_NEAR(k.pan().x, k.anchor().x, 1e-4F);
    EXPECT_NEAR(k.pan().y, k.anchor().y, 1e-4F);
}
