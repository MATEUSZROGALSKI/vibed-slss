#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace slc::content {

/**
 * Client-side rate limiter (doc 6.2: 2 req/s, burst 5). Tokens refill
 * continuously; acquire() blocks until a token is available or the stop
 * token fires (returns false then).
 */
class TokenBucket {
  public:
    TokenBucket(double ratePerSec, double burst);

    /// Try to take a token without waiting.
    [[nodiscard]] bool tryAcquire();

    /// Wait for a token. Returns false when `stop` is requested first.
    [[nodiscard]] bool acquire(std::stop_token stop = {});

  private:
    void refill();

    std::mutex m_mutex;
    std::condition_variable_any m_cv;
    double m_ratePerSec;
    double m_burst;
    double m_tokens;
    std::chrono::steady_clock::time_point m_lastRefill;
};

} // namespace slc::content
