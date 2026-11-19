#pragma once

#include <cstddef>
#include <deque>
#include <random>
#include <unordered_set>
#include <vector>

namespace slc::app {

/// Weighted pick queue of playlist indices. Replaces a fixed
/// shuffle+playhead: every entry starts at weight 1.0; showing it drops
/// the weight ~8x and parks the index in a recents window so it can't
/// reappear until enough other entries have been shown; weights then
/// recover toward 1 over ~15-20 scenes. Owns no playlist/GL state — the
/// app feeds it sizes and consumes indices. Not thread-safe: driven by
/// the UI thread only.
class PickQueue {
  public:
    /// Sizes the weight table to `n`, clearing queue + recents when the
    /// size changed — a reshuffled playlist must not keep stale indices.
    void ensureSize(std::size_t n);

    /// Efraimidis–Spirakis weighted sampling without replacement
    /// (key = u^(1/w), highest key wins; w=1 gives a uniform draw and a
    /// shown entry at w=0.12 is ~8x less likely). Fills the queue to
    /// `target`, preferring non-recent entries and relaxing to all
    /// unqueued ones once they run out. Stops early on tiny playlists.
    void refill(std::size_t target, std::mt19937& rng);

    /// Drops shown entries ~8x in weight and parks them in the recents
    /// window (~1/4 of the library, min 24); all weights then recover
    /// 5% toward 1.0.
    void markShown(const std::vector<std::size_t>& indices);

    /// Drops queue membership, weights, and recents — for wholesale
    /// playlist rebuilds (config restart).
    void reset();

    [[nodiscard]] bool empty() const noexcept { return m_queue.empty(); }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_queue.size();
    }
    [[nodiscard]] std::size_t front() const { return m_queue.front(); }
    /// Playlist index at queue position `pos`.
    [[nodiscard]] std::size_t at(std::size_t pos) const
    {
        return m_queue[pos];
    }

    /// Removes the queue entry at position `pos`; returns the playlist
    /// index, which can re-enter the queue on later refills unless
    /// burned (see burn()).
    std::size_t takeAt(std::size_t pos);
    /// Removes the queue front; the index can re-enter the queue later.
    void popFront();

    /// Marks `idx` as consumed for the current scene build: refill()
    /// never re-draws a burned index, so a single hot favourite can't
    /// flood every slot of a large scene. Burned indices are released
    /// by markShown() (weights + recents then carry the suppression)
    /// or by reset()/ensureSize().
    void burn(std::size_t idx) { m_burned.insert(idx); }
    [[nodiscard]] bool burned(std::size_t idx) const
    {
        return m_burned.contains(idx);
    }

  private:
    std::vector<double> m_weights;       // parallel to the playlist
    std::deque<std::size_t> m_queue;     // playlist indices, pick order
    std::unordered_set<std::size_t> m_queued;
    std::deque<std::size_t> m_recentOrder;
    std::unordered_set<std::size_t> m_recent;
    std::unordered_set<std::size_t> m_burned; // consumed this build
};

} // namespace slc::app
