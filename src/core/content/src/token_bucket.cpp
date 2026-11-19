#include "slc/content/token_bucket.h"

#include <algorithm>

namespace slc::content {

TokenBucket::TokenBucket(double ratePerSec, double burst)
    : m_ratePerSec(ratePerSec),
      m_burst(burst),
      m_tokens(burst),
      m_lastRefill(std::chrono::steady_clock::now())
{
}

void TokenBucket::refill()
{
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - m_lastRefill).count();
    m_tokens = std::min(m_burst, m_tokens + elapsed * m_ratePerSec);
    m_lastRefill = now;
}

bool TokenBucket::tryAcquire()
{
    std::lock_guard lock(m_mutex);
    refill();
    if (m_tokens < 1.0) {
        return false;
    }
    m_tokens -= 1.0;
    return true;
}

bool TokenBucket::acquire(std::stop_token stop)
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        refill();
        if (m_tokens >= 1.0) {
            m_tokens -= 1.0;
            return true;
        }
        if (stop.stop_requested()) {
            return false;
        }
        // Wait until the next token should be ready, then recheck.
        const double waitSec = (1.0 - m_tokens) / m_ratePerSec;
        m_cv.wait_for(lock, std::chrono::duration<double>(waitSec),
                      [&] { return stop.stop_requested(); });
    }
}

} // namespace slc::content
