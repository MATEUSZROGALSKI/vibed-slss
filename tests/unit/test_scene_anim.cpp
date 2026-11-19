// Scene system pieces: RotateAnimator / ScalePulseAnimator bounds and
// determinism, the KenBurns traverse variant, and the scene-transition
// transform math (scene_transition.h).
#include "slc/anim/cell_animators.h"
#include "slc/anim/collage_choreographer.h"
#include "slc/anim/ken_burns_animator.h"
#include "slc/anim/scene_transition.h"

#include <cinder/Rect.h>
#include <cinder/Timeline.h>

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace slc::anim;

namespace {

constexpr float kDt = 1.0F / 60.0F;

float drive(ICellAnimator& an, int frames, float dt = kDt)
{
    auto tl = ci::Timeline::create();
    an.attach(*tl, ci::Rectf(0, 0, 100, 100), nullptr);
    float last = an.rotation();
    for (int i = 0; i < frames; ++i) {
        tl->step(dt);
        last = an.rotation();
    }
    return last;
}

} // namespace

TEST(RotateAnimator, StaysWithinMaxDeg)
{
    RotateAnimator an(1.5F, 4.0F);
    auto tl = ci::Timeline::create();
    an.attach(*tl, ci::Rectf(0, 0, 10, 10), nullptr);
    float minA = 0.0F, maxA = 0.0F;
    for (int i = 0; i < 60 * 10; ++i) { // 10s > one full ping-pong
        tl->step(kDt);
        minA = std::min(minA, an.rotation());
        maxA = std::max(maxA, an.rotation());
    }
    EXPECT_GE(minA, -1.5F - 1e-3F);
    EXPECT_LE(maxA, 1.5F + 1e-3F);
    EXPECT_GT(maxA - minA, 2.0F); // actually oscillates
}

TEST(ScalePulseAnimator, BreathesWithinBounds)
{
    ScalePulseAnimator an(0.06F, 5.0F);
    auto tl = ci::Timeline::create();
    an.attach(*tl, ci::Rectf(0, 0, 10, 10), nullptr);
    float minS = 2.0F, maxS = 0.0F;
    for (int i = 0; i < 60 * 11; ++i) {
        tl->step(kDt);
        minS = std::min(minS, an.scale());
        maxS = std::max(maxS, an.scale());
    }
    EXPECT_GE(minS, 1.0F - 1e-3F);
    EXPECT_LE(maxS, 1.06F + 1e-3F);
    EXPECT_GT(maxS - minS, 0.03F);
}

TEST(KenBurnsTraverse, PansEdgeToEdge)
{
    KenBurnsAnimator an(1.18F, 1.18F, ci::vec2(-1, 0), ci::vec2(1, 0), 4000);
    auto tl = ci::Timeline::create();
    an.attach(*tl, ci::Rectf(0, 0, 100, 100), nullptr);

    EXPECT_NEAR(an.pan().x, -1.0F, 1e-3F);
    for (int i = 0; i < 240; ++i) { // full 4s
        tl->step(kDt);
    }
    EXPECT_NEAR(an.pan().x, 1.0F, 0.05F);
    EXPECT_NEAR(an.zoom(), 1.18F, 1e-3F);
}

TEST(SceneTransition, CrossfadeAlphasComplement)
{
    const ci::vec2 view(1920, 1080);
    for (float f : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        const auto o = transitionOut(SceneTransition::Crossfade, f,
                                     ci::vec2(1, 0), view);
        const auto in = transitionIn(SceneTransition::Crossfade, f,
                                     ci::vec2(1, 0), view);
        EXPECT_FLOAT_EQ(o.alpha, 1.0F);
        EXPECT_FLOAT_EQ(in.alpha, f);
    }
}

TEST(SceneTransition, FadeBlackMeetsAtZero)
{
    const ci::vec2 view(1920, 1080);
    const auto outMid =
        transitionOut(SceneTransition::FadeBlack, 0.5F, ci::vec2(), view);
    const auto inMid =
        transitionIn(SceneTransition::FadeBlack, 0.5F, ci::vec2(), view);
    EXPECT_FLOAT_EQ(outMid.alpha, 0.0F);
    EXPECT_FLOAT_EQ(inMid.alpha, 0.0F);
    // Monotonic halves.
    EXPECT_GT(transitionOut(SceneTransition::FadeBlack, 0.25F, ci::vec2(),
                            view)
                  .alpha,
              0.4F);
    EXPECT_GT(transitionIn(SceneTransition::FadeBlack, 0.75F, ci::vec2(),
                           view)
                  .alpha,
              0.4F);
}

TEST(SceneTransition, SlideOffsetsReachEdges)
{
    const ci::vec2 view(1920, 1080);
    const ci::vec2 dir(1, 0);
    const auto out0 =
        transitionOut(SceneTransition::Slide, 0.0F, dir, view);
    const auto out1 =
        transitionOut(SceneTransition::Slide, 1.0F, dir, view);
    const auto in0 = transitionIn(SceneTransition::Slide, 0.0F, dir, view);
    const auto in1 = transitionIn(SceneTransition::Slide, 1.0F, dir, view);
    EXPECT_FLOAT_EQ(out0.offset.x, 0.0F);
    EXPECT_FLOAT_EQ(out1.offset.x, -1920.0F);
    EXPECT_FLOAT_EQ(in0.offset.x, 1920.0F);
    EXPECT_FLOAT_EQ(in1.offset.x, 0.0F);
    EXPECT_FLOAT_EQ(in0.alpha, 1.0F); // slide scenes stay opaque
}

TEST(SceneTransition, ZoomInShrinksToOne)
{
    const auto in0 =
        transitionIn(SceneTransition::Zoom, 0.0F, ci::vec2(), {100, 100});
    const auto in1 =
        transitionIn(SceneTransition::Zoom, 1.0F, ci::vec2(), {100, 100});
    EXPECT_NEAR(in0.scale, 1.15F, 1e-3F);
    EXPECT_FLOAT_EQ(in1.scale, 1.0F);
    EXPECT_FLOAT_EQ(in1.alpha, 1.0F);
}

TEST(SceneTransition, FromStringRoundTrips)
{
    EXPECT_EQ(sceneTransitionFromString("slide", SceneTransition::Zoom),
              SceneTransition::Slide);
    EXPECT_EQ(sceneTransitionFromString("bogus", SceneTransition::Zoom),
              SceneTransition::Zoom);
}

TEST(CollageChoreo, MotionVarietySeeded)
{
    // Two rngs with the same seed produce identical per-cell motion;
    // different seeds should pick different animator mixes.
    auto mixFor = [](std::uint32_t seed) {
        std::mt19937 rng(seed);
        CollageChoreographer c(600, 40, rng);
        for (int i = 0; i < 8; ++i) {
            c.addCell(ci::Rectf(i * 10.0F, 0, i * 10.0F + 8, 8), nullptr);
        }
        c.start();
        // Probe each cell's motion signature after 1s.
        int flags = 0;
        for (int i = 0; i < 400; ++i) {
            c.step(kDt);
        }
        for (std::size_t i = 0; i < 8; ++i) {
            const CellVisual v = c.cellVisual(i);
            if (v.offset != ci::vec2(0)) flags |= 1;
            if (v.rotationDeg != 0.0F) flags |= 2;
            if (std::abs(v.scale - 1.0F) > 1e-3F) flags |= 4;
        }
        return flags;
    };
    const int a = mixFor(7);
    EXPECT_EQ(a, mixFor(7)); // deterministic
    EXPECT_NE(a, 0);         // some motion present
}
