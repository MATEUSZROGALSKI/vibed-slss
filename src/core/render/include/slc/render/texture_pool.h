#pragma once

#include <cinder/Surface.h>
#include <cinder/gl/Texture.h>

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace slc::render {

/**
 * Owns the decode->upload pipeline budget (doc section 5.6).
 * Decode threads push CPU-side Surfaces; the GL thread drains at most
 * `budgetMs` per frame into Texture2d. At most `2 * visibleCells` textures
 * stay resident; eviction is least-recently-used.
 *
 * Threading: enqueueDecoded()/queuedCount() may be called from any thread.
 * drainUploads()/get()/residentCount() are GL-thread only.
 */
class TexturePool {
  public:
    explicit TexturePool(std::size_t visibleCells);

    // Decode pool -> main thread handoff (CPU surface, no GL calls).
    void enqueueDecoded(const std::string& key, ci::Surface surface);

    // Main thread per-frame drain; returns milliseconds spent (<= budgetMs).
    float drainUploads(float budgetMs);

    // Resident texture lookup; touching a key marks it most-recently-used.
    [[nodiscard]] ci::gl::Texture2dRef get(const std::string& key);

    // Tiny (~48px) version of the same image, for dim/blurred backdrop fills.
    // Upscaled it reads as a soft blurred version of the artwork. Null while
    // the pad is still queued or after eviction.
    [[nodiscard]] ci::gl::Texture2dRef getPad(const std::string& key);

    // Residency check without touching the LRU.
    [[nodiscard]] bool isResident(const std::string& key) const
    {
        return m_resident.contains(key);
    }

    // A decoded surface whose Texture2d::create failed — the key can
    // never become resident; callers should treat it as failed so it
    // doesn't wait on (or retry) it forever.
    [[nodiscard]] bool isUploadFailed(const std::string& key) const
    {
        return m_uploadFailed.contains(key);
    }

    // Keys that must never be evicted (the playhead window). GL-thread only.
    void setProtectedKeys(std::unordered_set<std::string> keys);

    [[nodiscard]] std::size_t residentCount() const { return m_resident.size(); }
    [[nodiscard]] std::size_t queuedCount() const;

  private:
    void evictIfNeeded();

    std::size_t m_residentLimit;
    std::unordered_set<std::string> m_uploadFailed;
    std::queue<std::pair<std::string, ci::Surface>> m_uploadQueue;
    mutable std::mutex m_queueMutex;
    std::list<std::string> m_lru; // front = least recently used
    std::unordered_map<std::string, ci::gl::Texture2dRef> m_resident;
    std::unordered_map<std::string, ci::gl::Texture2dRef> m_pads;
    std::unordered_set<std::string> m_protected;
};

} // namespace slc::render
