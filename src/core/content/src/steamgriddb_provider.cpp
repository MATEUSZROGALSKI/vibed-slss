#include "slc/content/steamgriddb_provider.h"

#include "slc/common/logging.h"
#include "slc/content/steam_locator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <system_error>
#include <thread>

namespace slc::content {
namespace {

constexpr std::string_view kSource = "steamgriddb";
constexpr std::string_view kMetaFile = "sgdb_meta.json";

std::string makeKey(const std::string& appId, ArtworkKind kind)
{
    return std::string("sgdb:") + appId + ":" +
           std::string(toString(kind));
}

std::optional<std::string> headerValue(const HttpResponse& r,
                                       const std::string& name)
{
    const auto it = r.headers.find(name);
    if (it == r.headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace

std::string_view SteamGridDbProvider::endpointFor(ArtworkKind kind) noexcept
{
    switch (kind) {
    case ArtworkKind::Grid:
        return "grids";
    case ArtworkKind::Hero:
    case ArtworkKind::Header:
        return "heroes"; // no header endpoint; heroes are closest
    case ArtworkKind::Logo:
        return "logos";
    case ArtworkKind::Icon:
        return "icons";
    }
    return "grids";
}

SteamGridDbProvider::SteamGridDbProvider(
    std::unique_ptr<IHttpTransport> transport, SgdbOptions options)
    : m_transport(std::move(transport)),
      m_options(std::move(options)),
      m_bucket(m_options.ratePerSec, m_options.burst),
      m_cache(m_options.cacheDir / "sgdb",
              cache::CacheLimits{.maxBytes = 256ull * 1024 * 1024,
                                 .maxEntries = 2000,
                                 .ttl = std::chrono::hours(24 * 7)})
{
    loadMeta();
}

void SteamGridDbProvider::loadMeta()
{
    const auto path = m_options.cacheDir / kMetaFile;
    std::ifstream in(path);
    if (!in) {
        return;
    }
    try {
        const auto j = nlohmann::json::parse(in);
        for (const auto& [key, v] : j.items()) {
            MetaEntry e;
            e.etag = v.value("etag", "");
            e.negative = v.value("negative", false);
            m_meta[key] = std::move(e);
        }
    } catch (const std::exception& e) {
        SLC_LOG_WARN("sgdb meta corrupt, starting fresh: {}", e.what());
        m_meta.clear();
    }
}

void SteamGridDbProvider::saveMeta() const
{
    const auto path = m_options.cacheDir / kMetaFile;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    nlohmann::json j;
    for (const auto& [key, e] : m_meta) {
        j[key] = {{"etag", e.etag}, {"negative", e.negative}};
    }
    const auto tmp = path.parent_path() / "sgdb_meta.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << j.dump();
    }
    std::filesystem::rename(tmp, path, ec);
}

std::optional<std::string> SteamGridDbProvider::pickImageUrl(
    const std::string& body)
{
    try {
        const auto j = nlohmann::json::parse(body);
        if (!j.value("success", false) || !j.contains("data") ||
            !j["data"].is_array()) {
            return std::nullopt;
        }
        for (const auto& item : j["data"]) {
            if (item.is_object() && item.contains("url") &&
                item["url"].is_string()) {
                return item["url"].get<std::string>();
            }
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}

Result<HttpResponse> SteamGridDbProvider::request(const HttpRequest& req,
                                                  std::stop_token stop)
{
    Result<HttpResponse> last =
        std::unexpected(Error::make(ErrorCode::IoError, "not attempted"));
    for (int attempt = 0; attempt <= m_options.maxRetries; ++attempt) {
        if (stop.stop_requested()) {
            return std::unexpected(
                Error::make(ErrorCode::Timeout, "cancelled"));
        }
        if (!m_bucket.acquire(stop)) {
            return std::unexpected(
                Error::make(ErrorCode::Timeout, "rate-limit wait cancelled"));
        }
        auto r = m_transport->get(req, stop);
        if (!r) {
            last = std::unexpected(r.error());
        } else {
            const int s = r->status;
            if (s >= 200 && s < 300) {
                return r;
            }
            if (s == 304 || (s >= 400 && s < 500 && s != 429)) {
                return r; // definitive: not modified, or client error
            }
            last = std::move(r);
        }

        if (attempt == m_options.maxRetries) {
            break;
        }
        // 429: honour Retry-After (clamped); 5xx/transport: exp. backoff.
        auto wait = m_options.backoffBase * (1 << attempt);
        if (last && last->status == 429) {
            if (const auto ra = headerValue(*last, "retry-after")) {
                try {
                    const auto ms = std::stoll(*ra) * 1000;
                    wait = std::chrono::milliseconds(std::clamp(
                        ms, 0LL,
                        static_cast<long long>(
                            m_options.maxRetryAfter.count())));
                } catch (const std::exception&) {
                }
            }
        }
        std::stop_source sleepStop;
        const auto wake = std::chrono::steady_clock::now() + wait;
        while (!stop.stop_requested() &&
               std::chrono::steady_clock::now() < wake) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    return last;
}

Result<std::filesystem::path> SteamGridDbProvider::downloadToCache(
    const std::string& key, const std::string& url, std::stop_token stop)
{
    HttpRequest imgReq;
    imgReq.url = url;
    imgReq.totalTimeout = std::chrono::milliseconds(15000);
    auto img = request(imgReq, stop);
    if (!img) {
        return std::unexpected(img.error());
    }
    if (img->status != 200 || img->body.empty()) {
        return std::unexpected(
            Error::make(ErrorCode::Malformed, "image download failed"));
    }
    // Write to a temp file, then let the cache take ownership.
    std::error_code ec;
    std::filesystem::create_directories(m_options.cacheDir / "tmp", ec);
    std::string safe = key;
    std::replace(safe.begin(), safe.end(), ':', '_'); // ':' illegal on MSW
    const auto tmp = m_options.cacheDir / "tmp" / (safe + ".img");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(img->body.data(),
                  static_cast<std::streamsize>(img->body.size()));
    }
    auto stored = m_cache.store(key, tmp);
    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc); // payload now lives in the cache
    return stored;
}

std::vector<Artwork>
SteamGridDbProvider::discover(const DiscoveryContext& ctx)
{
    std::vector<Artwork> out;
    if (m_options.apiKey.empty()) {
        return out;
    }

    std::unordered_set<ArtworkKind> kinds = m_options.includeKinds;
    if (kinds.empty()) {
        kinds = {ArtworkKind::Hero, ArtworkKind::Grid};
    }

    // Installed games: explicit list wins; otherwise reuse the locator so the
    // provider is self-contained inside ContentService::collect.
    std::unordered_map<std::string, std::string> names;
    std::unordered_set<std::string> appIds = m_options.appIds;
    if (appIds.empty()) {
        if (auto install = locateInstall(ctx.steamRootOverride)) {
            for (const auto& [id, app] : install->installedApps) {
                if (ctx.excludeAppIds.contains(id)) {
                    continue;
                }
                // Games only — same rule as the local provider: known
                // non-game types (DLC/tools/videos) are skipped, unknown
                // types fail open.
                if (const auto infoIt = install->appInfo.find(id);
                    infoIt != install->appInfo.end() &&
                    !infoIt->second.type.empty()) {
                    std::string t = infoIt->second.type;
                    std::ranges::transform(t, t.begin(), [](unsigned char c) {
                        return std::tolower(c);
                    });
                    if (t != "game") {
                        continue;
                    }
                }
                appIds.insert(id);
                names[id] = app.name;
            }
        } else {
            SLC_LOG_WARN("sgdb: no steam install, enrichment skipped");
            return out;
        }
    }

    for (const auto& appId : appIds) {
        for (const auto kind : kinds) {
            if (ctx.stopToken.stop_requested()) {
                return out;
            }
            const std::string key = makeKey(appId, kind);

            // Negative cache: known-empty within TTL.
            if (const auto it = m_meta.find(key);
                it != m_meta.end() && it->second.negative) {
                continue;
            }

            if (auto hit = m_cache.lookup(key)) {
                out.push_back(Artwork{appId, names.count(appId) ? names[appId] : appId, kind, *hit, 0, 0,
                                      std::string(kSource)});
                continue;
            }

            HttpRequest req;
            req.url = std::string(kBaseUrl) + "/" +
                      std::string(endpointFor(kind)) + "/game/" + appId;
            req.headers.emplace_back("Authorization",
                                     "Bearer " + m_options.apiKey);
            if (const auto it = m_meta.find(key);
                it != m_meta.end() && !it->second.etag.empty()) {
                req.headers.emplace_back("If-None-Match",
                                         it->second.etag);
            }

            auto r = request(req, ctx.stopToken);
            if (!r) {
                SLC_LOG_WARN("sgdb {} for {} failed: {}", toString(kind),
                             appId, r.error().message);
                continue; // silent degradation (doc 6.2)
            }
            if (r->status == 304) {
                if (auto hit2 = m_cache.lookup(key)) {
                    out.push_back(Artwork{appId, names.count(appId) ? names[appId] : appId, kind, *hit2, 0, 0,
                                          std::string(kSource)});
                }
                continue;
            }
            if (r->status == 404) {
                m_meta[key].negative = true;
                continue;
            }
            if (r->status != 200) {
                SLC_LOG_WARN("sgdb {} for {}: HTTP {}", toString(kind),
                             appId, r->status);
                continue;
            }

            if (const auto et = headerValue(*r, "etag")) {
                m_meta[key].etag = *et;
            }
            const auto url = pickImageUrl(r->body);
            if (!url) {
                m_meta[key].negative = true;
                continue;
            }
            auto path = downloadToCache(key, *url, ctx.stopToken);
            if (!path) {
                SLC_LOG_WARN("sgdb image for {} failed: {}", key,
                             path.error().message);
                continue;
            }
            out.push_back(Artwork{appId, names.count(appId) ? names[appId] : appId, kind, *path, 0, 0,
                                  std::string(kSource)});
        }
    }
    saveMeta();
    return out;
}

} // namespace slc::content
