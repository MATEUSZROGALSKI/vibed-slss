#include "slc/content/hltb.h"

#include "slc/common/logging.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace slc::content {
namespace {

constexpr std::string_view kInitUrl =
    "https://howlongtobeat.com/api/search/site/init?t=";
constexpr std::string_view kSearchUrl =
    "https://howlongtobeat.com/api/search/site";
constexpr std::string_view kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";
constexpr std::int64_t kNegativeTtlSec = 30LL * 24 * 3600;

std::int64_t nowSec()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

HttpRequest makeReq(std::string url)
{
    HttpRequest req;
    req.url = std::move(url);
    req.headers.emplace_back("User-Agent", std::string(kUserAgent));
    req.headers.emplace_back("Referer", "https://howlongtobeat.com/");
    req.totalTimeout = std::chrono::milliseconds(12000);
    return req;
}

/// Splits "Some Game" into the word array the site's search wants.
nlohmann::json searchTerms(const std::string& name)
{
    nlohmann::json terms = nlohmann::json::array();
    std::string cur;
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        } else if (!cur.empty()) {
            terms.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        terms.push_back(cur);
    }
    if (terms.empty()) {
        terms.push_back(name);
    }
    return terms;
}

std::string searchBody(const std::string& name)
{
    nlohmann::json body = {
        {"searchType", "games"},
        {"searchTerms", searchTerms(name)},
        {"searchPage", 1},
        {"size", 10},
        {"searchOptions",
         {{"games",
           {{"userId", 0},
            {"platform", ""},
            {"sortCategory", "popular"},
            {"rangeCategory", "main"},
            {"rangeTime", {{"min", nullptr}, {"max", nullptr}}},
            {"gameplay",
             {{"perspective", ""},
              {"flow", ""},
              {"genre", ""},
              {"difficulty", ""}}},
            {"rangeYear", {{"min", ""}, {"max", ""}}},
            {"modifier", ""}}},
          {"users", {{"sortCategory", "postcount"}}},
          {"lists", {{"sortCategory", "follows"}}}}},
        {"filter", ""},
        {"randomizer", 0}};
    return body.dump();
}

/// Picks the best match: exact case-insensitive name match wins,
/// otherwise the first (relevance-sorted) result.
std::optional<std::uint32_t> pickMinutes(const nlohmann::json& data,
                                         const std::string& name)
{
    const auto minsOf = [](const nlohmann::json& g) -> std::uint32_t {
        // HLTB reports seconds; main -> main+extras -> all-styles fallback.
        for (const char* k : {"comp_main", "comp_plus", "comp_all"}) {
            if (const auto v = g.value(k, 0); v > 0) {
                return static_cast<std::uint32_t>(v) / 60u;
            }
        }
        return 0;
    };
    std::optional<std::uint32_t> first;
    for (const auto& g : data) {
        const auto mins = minsOf(g);
        if (!first) {
            first = mins;
        }
        const std::string gn = g.value("game_name", "");
        if (!gn.empty() && gn.size() == name.size() &&
            std::equal(gn.begin(), gn.end(), name.begin(),
                       [](char a, char b) {
                           return std::tolower(
                                      static_cast<unsigned char>(a)) ==
                                  std::tolower(
                                      static_cast<unsigned char>(b));
                       })) {
            return mins;
        }
    }
    return first;
}

} // namespace

HltbResolver::HltbResolver(std::unique_ptr<IHttpTransport> transport,
                           std::filesystem::path cacheDir)
    : m_transport(std::move(transport)),
      m_cacheFile(std::move(cacheDir) / "hltb.json")
{
    std::ifstream in(m_cacheFile);
    if (!in) {
        return;
    }
    try {
        const auto j = nlohmann::json::parse(in);
        for (const auto& [appId, v] : j.items()) {
            Entry e;
            e.mainMin = v.value("m", 0u);
            e.negative = v.value("neg", false);
            e.fetchedAt = v.value("ts", 0LL);
            m_cache[appId] = e;
        }
    } catch (const std::exception& e) {
        SLC_LOG_WARN("hltb cache corrupt, starting fresh: {}", e.what());
        m_cache.clear();
    }
}

HltbResolver::~HltbResolver() = default;

std::optional<std::uint32_t>
HltbResolver::lookup(const std::string& appId) const
{
    const auto it = m_cache.find(appId);
    if (it == m_cache.end() || it->second.negative ||
        it->second.mainMin == 0) {
        return std::nullopt;
    }
    return it->second.mainMin;
}

bool HltbResolver::needsResolve(const std::string& appId) const
{
    const auto it = m_cache.find(appId);
    if (it == m_cache.end()) {
        return true;
    }
    return it->second.negative &&
           nowSec() - it->second.fetchedAt > kNegativeTtlSec;
}

std::optional<std::string> HltbResolver::searchToken(std::stop_token stop)
{
    if (m_tokenTried) {
        return m_token.empty() ? std::nullopt
                               : std::optional<std::string>(m_token);
    }
    m_tokenTried = true;
    if (!m_transport) {
        return std::nullopt;
    }
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    auto r = m_transport->get(
        makeReq(std::string(kInitUrl) + std::to_string(ms)), stop);
    if (r && r->status == 200) {
        try {
            m_token = nlohmann::json::parse(r->body).value("token", "");
        } catch (const std::exception&) {
        }
    }
    if (m_token.empty()) {
        SLC_LOG_WARN("hltb: search token unavailable (status {})",
                     r ? r->status : 0);
    }
    return m_token.empty() ? std::nullopt
                           : std::optional<std::string>(m_token);
}

std::optional<std::uint32_t> HltbResolver::query(const std::string& gameName,
                                                 std::stop_token stop)
{
    if (!m_transport) {
        return std::nullopt;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto token = searchToken(stop);
        if (!token) {
            return std::nullopt;
        }
        auto req = makeReq(std::string(kSearchUrl));
        req.headers.emplace_back("x-auth-token", *token);
        req.headers.emplace_back("Content-Type", "application/json");
        auto r = m_transport->post(req, searchBody(gameName), stop);
        if (!r) {
            return std::nullopt;
        }
        if (r->status == 403) {
            // Session tokens are short-lived — refetch once, then give up.
            m_token.clear();
            m_tokenTried = false;
            continue;
        }
        if (r->status != 200) {
            SLC_LOG_WARN("hltb search for \"{}\": HTTP {}", gameName,
                         r->status);
            return std::nullopt;
        }
        try {
            const auto j = nlohmann::json::parse(r->body);
            if (j.contains("data") && j["data"].is_array() &&
                !j["data"].empty()) {
                return pickMinutes(j["data"], gameName);
            }
        } catch (const std::exception&) {
        }
        return std::nullopt; // valid response, no match — negative
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
HltbResolver::resolve(const std::string& appId, const std::string& gameName,
                      std::stop_token stop)
{
    if (auto cached = lookup(appId); cached || !needsResolve(appId)) {
        return cached;
    }
    if (!m_transport || gameName.empty() || stop.stop_requested()) {
        return std::nullopt;
    }
    Entry e;
    e.fetchedAt = nowSec();
    if (auto mins = query(gameName, stop); mins && *mins > 0) {
        e.mainMin = *mins;
    } else {
        e.negative = true;
    }
    m_cache[appId] = e;
    return e.negative ? std::nullopt : std::optional<std::uint32_t>(e.mainMin);
}

void HltbResolver::save() const
{
    std::error_code ec;
    std::filesystem::create_directories(m_cacheFile.parent_path(), ec);
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [appId, e] : m_cache) {
        j[appId] = {{"m", e.mainMin},
                    {"neg", e.negative},
                    {"ts", e.fetchedAt}};
    }
    const auto tmp = m_cacheFile.parent_path() / "hltb.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << j.dump();
    }
    std::filesystem::rename(tmp, m_cacheFile, ec);
}

} // namespace slc::content
