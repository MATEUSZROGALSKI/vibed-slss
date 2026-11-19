#include "slc/cache/artwork_cache.h"

#include "slc/common/logging.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace slc::cache {
namespace {

constexpr std::string_view kManifestName = "manifest.json";
constexpr int kManifestVersion = 1;

std::chrono::system_clock::time_point fromEpoch(std::int64_t seconds)
{
    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

std::int64_t toEpoch(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

// Cache keys are logical (e.g. "sgdb:440:hero"); on-disk names must not
// contain characters that are illegal on Windows or path separators on
// any platform.
std::string filenameFor(std::string_view key)
{
    std::string out(key);
    for (auto& c : out) {
        if (c == ':' || c == '/' || c == '\\' || c == '?' || c == '*' ||
            c == '<' || c == '>' || c == '|' || c == '"') {
            c = '_';
        }
    }
    return out;
}

} // namespace

ArtworkCache::ArtworkCache(std::filesystem::path dir, CacheLimits limits)
    : m_dir(std::move(dir)), m_limits(limits)
{
    std::error_code ec;
    std::filesystem::create_directories(m_dir, ec);
    if (auto res = loadManifest(); !res) {
        SLC_LOG_WARN("cache manifest unusable ({}), starting empty",
                     res.error().message);
        m_entries.clear();
    }
}

std::optional<std::filesystem::path> ArtworkCache::lookup(const std::string& key)
{
    std::lock_guard lock(m_mutex);
    const auto it = std::ranges::find(m_entries, key, &CacheEntry::key);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    const auto absolute = m_dir / it->file;
    if (!std::filesystem::exists(absolute)) {
        m_entries.erase(it);
        return std::nullopt;
    }
    it->lastAccess = std::chrono::system_clock::now();
    std::rotate(m_entries.begin(), it, it + 1); // move to MRU front
    return absolute;
}

Result<std::filesystem::path> ArtworkCache::store(const std::string& key,
                                                  const std::filesystem::path& payload)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(payload, ec);
    if (ec) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, "cannot stat " + payload.string()));
    }

    std::lock_guard lock(m_mutex);
    const auto relName =
        std::filesystem::path(filenameFor(key) + payload.extension().string());
    const auto dest = m_dir / relName;
    std::error_code eqEc;
    if (!std::filesystem::equivalent(payload, dest, eqEc)) {
        std::filesystem::copy_file(payload, dest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, "cache write failed: " + ec.message()));
    }

    const auto now = std::chrono::system_clock::now();
    const auto it = std::ranges::find(m_entries, key, &CacheEntry::key);
    if (it != m_entries.end()) {
        it->sizeBytes = size;
        it->lastAccess = now;
        std::rotate(m_entries.begin(), it, it + 1);
    } else {
        m_entries.insert(m_entries.begin(),
                         CacheEntry{key, relName, size, now});
    }
    evictIfNeeded();
    return dest;
}

void ArtworkCache::evictIfNeeded()
{
    const auto now = std::chrono::system_clock::now();
    std::uint64_t total = 0;
    for (const auto& e : m_entries) {
        total += e.sizeBytes;
    }

    for (auto it = m_entries.end(); it != m_entries.begin();) {
        --it; // iterate LRU -> MRU
        const bool expired = (now - it->lastAccess) > m_limits.ttl;
        const bool overBudget = total > m_limits.maxBytes ||
                                m_entries.size() > m_limits.maxEntries;
        if (!expired && !overBudget) {
            continue;
        }
        std::error_code ec;
        std::filesystem::remove(m_dir / it->file, ec);
        total -= it->sizeBytes;
        it = m_entries.erase(it);
    }
}

void ArtworkCache::removeMissingFiles()
{
    std::lock_guard lock(m_mutex);
    std::erase_if(m_entries, [this](const CacheEntry& e) {
        return !std::filesystem::exists(m_dir / e.file);
    });
}

Result<void> ArtworkCache::flush()
{
    std::lock_guard lock(m_mutex);
    return saveManifest();
}

Result<void> ArtworkCache::loadManifest()
{
    const auto manifestPath = m_dir / kManifestName;
    std::ifstream in(manifestPath);
    if (!in) {
        return std::unexpected(
            Error::make(ErrorCode::NotFound, "no manifest at " + manifestPath.string()));
    }
    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(
            Error::make(ErrorCode::Malformed, std::string("manifest json: ") + e.what()));
    }
    if (doc.value("version", 0) != kManifestVersion) {
        return std::unexpected(
            Error::make(ErrorCode::Unsupported, "manifest version mismatch"));
    }

    std::vector<CacheEntry> entries;
    for (const auto& item : doc.value("entries", nlohmann::json::array())) {
        CacheEntry e;
        e.key = item.value("key", "");
        e.file = item.value("file", "");
        e.sizeBytes = item.value("sizeBytes", 0ull);
        e.lastAccess = fromEpoch(item.value("lastAccess", 0ll));
        if (!e.key.empty()) {
            entries.push_back(std::move(e));
        }
    }
    // Manifest serialises MRU-first, same as in-memory order.
    m_entries = std::move(entries);
    return {};
}

Result<void> ArtworkCache::saveManifest() const
{
    nlohmann::json doc;
    doc["version"] = kManifestVersion;
    doc["entries"] = nlohmann::json::array();
    for (const auto& e : m_entries) {
        doc["entries"].push_back({
            {"key", e.key},
            {"file", e.file.generic_string()},
            {"sizeBytes", e.sizeBytes},
            {"lastAccess", toEpoch(e.lastAccess)},
        });
    }
    const auto manifestPath = m_dir / kManifestName;
    const auto realTmp = m_dir / "manifest.json.tmp";
    {
        std::ofstream out(realTmp, std::ios::trunc);
        if (!out) {
            return std::unexpected(
                Error::make(ErrorCode::IoError, "cannot write " + realTmp.string()));
        }
        out << doc.dump(2);
    }
    std::error_code ec;
    std::filesystem::rename(realTmp, manifestPath, ec);
    if (ec) {
        return std::unexpected(
            Error::make(ErrorCode::IoError, "manifest rename failed: " + ec.message()));
    }
    return {};
}

} // namespace slc::cache
