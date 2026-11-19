#include "slc/app/pick_queue.h"

#include <algorithm>
#include <cmath>

namespace slc::app {

void PickQueue::ensureSize(std::size_t n)
{
    if (m_weights.size() == n) {
        return;
    }
    m_weights.assign(n, 1.0);
    m_queue.clear();
    m_queued.clear();
    m_recentOrder.clear();
    m_recent.clear();
    m_burned.clear();
}

void PickQueue::refill(std::size_t target, std::mt19937& rng)
{
    const std::size_t n = m_weights.size();
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    while (m_queue.size() < target) {
        // Efraimidis–Spirakis weighted sampling without replacement:
        // key = u^(1/w), highest key wins. w=1 gives a uniform draw;
        // a shown entry at w=0.12 is ~8x less likely. Burned indices
        // are never re-drawn — the exclusion is absolute for the whole
        // scene build, not relaxed like the recents window.
        double bestKey = -1.0;
        std::size_t bestIdx = n;
        for (int relax = 0; relax < 2 && bestIdx == n; ++relax) {
            for (std::size_t i = 0; i < n; ++i) {
                if (m_queued.contains(i) || m_burned.contains(i)) {
                    continue;
                }
                if (relax == 0 && m_recent.contains(i)) {
                    continue;
                }
                const double w = std::max(1e-3, m_weights[i]);
                const double k = std::pow(u01(rng), 1.0 / w);
                if (k > bestKey) {
                    bestKey = k;
                    bestIdx = i;
                }
            }
        }
        if (bestIdx == n) {
            break; // everything is queued (tiny playlist)
        }
        m_queue.push_back(bestIdx);
        m_queued.insert(bestIdx);
    }
}

void PickQueue::markShown(const std::vector<std::size_t>& indices)
{
    const std::size_t n = m_weights.size();
    if (n == 0) {
        return;
    }
    // Recents window: an entry can't be re-picked until this many later
    // entries have been consumed (~1/4 of the library, min 24).
    const std::size_t cap =
        n > 1 ? std::min<std::size_t>(n - 1, std::max<std::size_t>(24, n / 4))
              : 0;
    for (const auto i : indices) {
        if (i < n) {
            m_weights[i] *= 0.12;
        }
        m_recentOrder.push_back(i);
        m_recent.insert(i);
    }
    while (m_recentOrder.size() > cap) {
        m_recent.erase(m_recentOrder.front());
        m_recentOrder.pop_front();
    }
    // Gentle recovery toward 1.0: a dropped entry is ~8x less likely on
    // the next pick and climbs back over ~15-20 scenes.
    for (auto& w : m_weights) {
        w = std::min(1.0, w + (1.0 - w) * 0.05);
    }
    // Scene adopted — weights+recents now carry the suppression, so
    // burn markers can be released for the next build.
    m_burned.clear();
}

void PickQueue::reset()
{
    m_weights.clear();
    m_queue.clear();
    m_queued.clear();
    m_recentOrder.clear();
    m_recent.clear();
    m_burned.clear();
}

std::size_t PickQueue::takeAt(std::size_t pos)
{
    const std::size_t idx = m_queue[pos];
    m_queue.erase(m_queue.begin() + static_cast<std::ptrdiff_t>(pos));
    m_queued.erase(idx);
    return idx;
}

void PickQueue::popFront()
{
    m_queued.erase(m_queue.front());
    m_queue.pop_front();
}

} // namespace slc::app
