#pragma once

#include "slc/common/error.h"
#include "slc/content/ihttp_transport.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace slc::content {

/**
 * HowLongToBeat lookup with an on-disk cache — howlongtobeat.com has no
 * public API, so this uses the site's own /api/search/site endpoint the
 * same way the community clients do: a session token from
 * /api/search/site/init, then a JSON POST. Results are cached under
 * <cacheDir>/hltb.json forever (times rarely change); misses are
 * negative-cached for 30 days so a lookup failure doesn't re-hit the
 * network every launch. No transport / POST failure → lookup() still
 * serves the cache, resolve() returns nullopt.
 */
class HltbResolver {
  public:
    HltbResolver(std::unique_ptr<IHttpTransport> transport,
                 std::filesystem::path cacheDir);
    HltbResolver(HltbResolver&&) = default;
    HltbResolver& operator=(HltbResolver&&) = default;
    HltbResolver(const HltbResolver&) = delete;
    HltbResolver& operator=(const HltbResolver&) = delete;
    ~HltbResolver();

    /// Cached main-story minutes for the app; nullopt when unknown.
    [[nodiscard]] std::optional<std::uint32_t>
    lookup(const std::string& appId) const;

    /// True when the app has no fresh cache entry and a network resolve
    /// would be attempted.
    [[nodiscard]] bool needsResolve(const std::string& appId) const;

    /// Cache-first resolve; on miss queries the site (rate: caller should
    /// pace calls — one request each, no batching API). Caches positives
    /// and negatives; returns main-story minutes when known.
    std::optional<std::uint32_t> resolve(const std::string& appId,
                                         const std::string& gameName,
                                         std::stop_token stop);

    /// Persists the cache (atomic rename); safe to call repeatedly.
    void save() const;

  private:
    struct Entry {
        std::uint32_t mainMin = 0;
        bool negative = false;
        std::int64_t fetchedAt = 0; // unix seconds
    };

    std::optional<std::string> searchToken(std::stop_token stop);
    std::optional<std::uint32_t> query(const std::string& gameName,
                                       std::stop_token stop);

    std::unique_ptr<IHttpTransport> m_transport;
    std::filesystem::path m_cacheFile;
    std::unordered_map<std::string, Entry> m_cache;
    std::string m_token;
    bool m_tokenTried = false;
};

} // namespace slc::content
