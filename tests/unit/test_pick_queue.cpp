// Unit tests for slc::app::PickQueue — weighted sampling, recents
// cooldown, and queue membership integrity. pick_queue.cpp is compiled
// into this test binary directly (see tests/CMakeLists.txt) so the
// queue is testable without linking slc_app/Cinder.
#include "slc/app/pick_queue.h"

#include <gtest/gtest.h>

#include <random>
#include <unordered_set>
#include <vector>

using slc::app::PickQueue;

namespace {

TEST(PickQueue, RefillFillsToTargetWithoutDuplicates)
{
    PickQueue q;
    std::mt19937 rng(42);
    q.ensureSize(50);
    q.refill(12, rng);

    ASSERT_EQ(q.size(), 12u);
    std::unordered_set<std::size_t> seen;
    while (!q.empty()) {
        const std::size_t idx = q.front();
        EXPECT_LT(idx, 50u);
        EXPECT_TRUE(seen.insert(idx).second) << "duplicate " << idx;
        q.popFront();
    }
}

TEST(PickQueue, TinyPlaylistStopsAtLibrarySize)
{
    PickQueue q;
    std::mt19937 rng(1);
    q.ensureSize(3);
    q.refill(12, rng);

    ASSERT_EQ(q.size(), 3u);
    std::unordered_set<std::size_t> seen;
    while (!q.empty()) {
        EXPECT_TRUE(seen.insert(q.front()).second);
        q.popFront();
    }
}

TEST(PickQueue, TakenIndexCanBeRequeued)
{
    PickQueue q;
    std::mt19937 rng(7);
    q.ensureSize(4);
    q.refill(4, rng); // all 4 indices queued

    const std::size_t idx = q.takeAt(1);
    q.refill(4, rng); // idx is now the only unqueued index

    ASSERT_EQ(q.size(), 4u);
    EXPECT_EQ(q.at(3), idx); // re-appended at the back
}

TEST(PickQueue, RecentsAreSkippedWhileOthersAvailable)
{
    PickQueue q;
    std::mt19937 rng(3);
    q.ensureSize(4);
    q.markShown({0, 1, 2});

    q.refill(4, rng);
    EXPECT_EQ(q.front(), 3u); // the only non-recent index
}

TEST(PickQueue, RecentWindowSlidesAndReadmits)
{
    PickQueue q;
    std::mt19937 rng(3);
    q.ensureSize(4);
    q.markShown({0, 1, 2});
    q.refill(4, rng);
    while (!q.empty()) {
        q.popFront();
    }
    // Show index 3 as well: the recents window (cap 3 for n=4) slides,
    // evicting index 0 — now the only non-recent index.
    q.markShown({3});
    q.refill(4, rng);
    EXPECT_EQ(q.front(), 0u);
}

TEST(PickQueue, EnsureSizeKeepsStateOnSameSizeResetsOnChange)
{
    PickQueue q;
    std::mt19937 rng(11);
    q.ensureSize(10);
    q.refill(5, rng);
    ASSERT_EQ(q.size(), 5u);

    q.ensureSize(10); // same size — queue preserved
    EXPECT_EQ(q.size(), 5u);

    q.ensureSize(11); // size changed — stale indices must not survive
    EXPECT_TRUE(q.empty());
}

TEST(PickQueue, ResetClearsEverything)
{
    PickQueue q;
    std::mt19937 rng(5);
    q.ensureSize(8);
    q.refill(6, rng);
    q.markShown({0, 1});
    q.reset();

    EXPECT_TRUE(q.empty());
    // After reset + ensureSize the queue refills from scratch.
    q.ensureSize(8);
    q.refill(3, rng);
    EXPECT_EQ(q.size(), 3u);
}

TEST(PickQueue, EmptyPlaylistYieldsEmptyQueue)
{
    PickQueue q;
    std::mt19937 rng(9);
    q.ensureSize(0);
    q.refill(10, rng);
    EXPECT_TRUE(q.empty());
}

} // namespace
