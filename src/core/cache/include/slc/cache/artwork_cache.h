#pragma once

#include "slc/common/error.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace slc::cache {

struct CacheEntry {
    std::string key;             // stable content key, e.g. "<appId>:<kind>"
    std::filesystem::path file;  // cached payload, relative to cache dir
    std::uint64_t sizeBytes = 0;
    std::chrono::system_clock::time_point lastAccess;
};

struct CacheLimits {
    std::uint64_t maxBytes = 512ull * 1024 * 1024; // 512 MiB
    std::size_t maxEntries = 4000;
    std::chrono::hours ttl{24 * 7};                // 7 days
};

/**
 * Manifest-backed disk cache (doc M1). The manifest is a JSON document written
 * atomically (tmp + rename); a corrupt or missing manifest is rebuilt empty
 * and logged — never fatal.
 *
 * Thread safety: all public methods take m_mutex; payloads are immutable once
 * stored so reads outside the lock are safe.
 */
class ArtworkCache {
  public:
    ArtworkCache(std::filesystem::path dir, CacheLimits limits = {});

    /** Path of the cached payload for `key`, refreshing LRU position. */
    [[nodiscard]] std::optional<std::filesystem::path> lookup(const std::string& key);

    /** Copies `payload` into the cache under `key` and returns its new path. */
    [[nodiscard]] Result<std::filesystem::path> store(const std::string& key,
                                                      const std::filesystem::path& payload);

    void evictIfNeeded();
    void removeMissingFiles();

    [[nodiscard]] Result<void> flush();
    [[nodiscard]] const std::vector<CacheEntry>& entries() const { return m_entries; }

  private:
    [[nodiscard]] Result<void> loadManifest();
    [[nodiscard]] Result<void> saveManifest() const;

    std::filesystem::path m_dir;
    CacheLimits m_limits;
    std::vector<CacheEntry> m_entries; // MRU first
    mutable std::mutex m_mutex;
};

} // namespace slc::cache
