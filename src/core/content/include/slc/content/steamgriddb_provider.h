#pragma once

#include "slc/cache/artwork_cache.h"
#include "slc/content/content_provider.h"
#include "slc/content/ihttp_transport.h"
#include "slc/content/token_bucket.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace slc::content {

/**
 * Tunables for SteamGridDbProvider — defaults match doc 6.2 (2 req/s burst
 * 5, 2 retries, 500 ms base backoff, 7-day TTL via ArtworkCache).
 */
struct SgdbOptions {
    std::string apiKey;                 // Bearer token; empty disables fetches
    std::filesystem::path cacheDir;     // payload + etag/negative-cache meta
    std::unordered_set<std::string> appIds;               // games to enrich
    std::unordered_set<ArtworkKind> includeKinds;         // empty = hero+grid
    double ratePerSec = 2.0;
    double burst = 5.0;
    int maxRetries = 2;
    std::chrono::milliseconds backoffBase{500};
    std::chrono::milliseconds maxRetryAfter{5000};
};

/**
 * Doc 6.2 opt-in enrichment provider. Fetches SteamGridDB grids/heroes/
 * logos/icons for locally-installed appIds, caches payloads in an
 * ArtworkCache (7-day TTL, ETag-aware revalidation) and emits Artwork
 * records with source="steamgriddb". Every failure degrades silently to a
 * warn log — enrichment never surfaces errors (doc: "Failure mode: silent
 * degradation to local-only content").
 *
 * The transport is injected, so the whole behaviour is testable without
 * cpr/network: token-bucket pacing, 429/5xx retry, malformed JSON,
 * negative caching, ETag 304 handling.
 */
class SteamGridDbProvider final : public IContentProvider {
  public:
    static constexpr std::string_view kBaseUrl =
        "https://www.steamgriddb.com/api/v2";

    SteamGridDbProvider(std::unique_ptr<IHttpTransport> transport,
                        SgdbOptions options);

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "steamgriddb";
    }

    std::vector<Artwork> discover(const DiscoveryContext& ctx) override;

    /// Doc 6.2 endpoint mapping; Header reuses "heroes" (closest kind).
    [[nodiscard]] static std::string_view endpointFor(ArtworkKind kind) noexcept;

  private:
    struct MetaEntry {
        std::string etag;
        bool negative = false; // known-empty result; skip until TTL
    };

    /// Token-bucket-paced GET with 429/5xx retry per doc 6.2.
    [[nodiscard]] Result<HttpResponse> request(const HttpRequest& req,
                                               std::stop_token stop);

    /// First usable CDN url in a {"data":[...]} body; nullopt on
    /// malformed/empty — treated as a negative result.
    [[nodiscard]] static std::optional<std::string> pickImageUrl(
        const std::string& body);

    [[nodiscard]] Result<std::filesystem::path> downloadToCache(
        const std::string& key, const std::string& url, std::stop_token stop);

    void loadMeta();
    void saveMeta() const;

    std::unique_ptr<IHttpTransport> m_transport;
    SgdbOptions m_options;
    TokenBucket m_bucket;
    cache::ArtworkCache m_cache;
    std::unordered_map<std::string, MetaEntry> m_meta;
};

} // namespace slc::content
