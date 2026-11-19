#include "slc/render/texture_pool.h"

#include <cinder/Filter.h>
#include <cinder/ip/Resize.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace slc::render {
namespace {

// Texture format for artwork: trilinear-filtered mipmaps kill the shimmer
// that point/linear sampling produces when Ken Burns downscales 4K art,
// and capped anisotropy keeps shallow-angle pans sharp.
ci::gl::Texture2d::Format artworkFormat()
{
    ci::gl::Texture2d::Format fmt;
    fmt.mipmap();
    fmt.maxAnisotropy(4.0F);
    return fmt;
}

constexpr int kPadMaxDim = 48;

// Tiny downscaled copy used as a dim "blurred" backdrop behind letterboxed
// artwork. Upscaled to cover the frame it reads as a defocused duplicate.
ci::gl::Texture2dRef makePad(const ci::Surface& surface)
{
    const int longest = std::max(surface.getWidth(), surface.getHeight());
    if (longest <= kPadMaxDim) {
        return nullptr;
    }
    const float scale = static_cast<float>(kPadMaxDim) / longest;
    const int w = std::max(1, static_cast<int>(surface.getWidth() * scale));
    const int h = std::max(1, static_cast<int>(surface.getHeight() * scale));
    const ci::Surface small = ci::ip::resizeCopy(
        surface, ci::Area(surface.getBounds()), ci::ivec2(w, h),
        ci::FilterCatmullRom());
    ci::gl::Texture2d::Format fmt; // always upscaled -> linear is enough
    return ci::gl::Texture2d::create(small, fmt);
}

} // namespace

TexturePool::TexturePool(std::size_t visibleCells)
    : m_residentLimit(visibleCells * 2)
{
}

void TexturePool::enqueueDecoded(const std::string& key, ci::Surface surface)
{
    std::lock_guard lock(m_queueMutex);
    m_uploadQueue.emplace(key, std::move(surface));
}

float TexturePool::drainUploads(float budgetMs)
{
    const auto start = std::chrono::steady_clock::now();
    while (true) {
        std::pair<std::string, ci::Surface> item;
        {
            std::lock_guard lock(m_queueMutex);
            if (m_uploadQueue.empty()) {
                break;
            }
            item = std::move(m_uploadQueue.front());
            m_uploadQueue.pop();
        }

        ci::gl::Texture2dRef tex;
        try {
            tex = ci::gl::Texture2d::create(item.second, artworkFormat());
        } catch (const std::exception&) {
            tex = nullptr;
        }
        if (tex) {
            m_pads[item.first] = makePad(item.second);
            m_resident[item.first] = std::move(tex);
            m_lru.remove(item.first);
            m_lru.push_back(item.first);
            evictIfNeeded();
        } else {
            // Upload failed (driver/GPU refusal): record it so the caller
            // can fail the key instead of waiting on it forever — an
            // unmarked drop orphans the decode pipeline's inflight set.
            m_uploadFailed.insert(item.first);
        }

        const float elapsed =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        if (elapsed >= budgetMs) {
            return elapsed;
        }
    }
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

ci::gl::Texture2dRef TexturePool::get(const std::string& key)
{
    const auto it = m_resident.find(key);
    if (it == m_resident.end()) {
        return nullptr;
    }
    m_lru.remove(key);
    m_lru.push_back(key);
    return it->second;
}

ci::gl::Texture2dRef TexturePool::getPad(const std::string& key)
{
    const auto it = m_pads.find(key);
    return it == m_pads.end() ? nullptr : it->second;
}

void TexturePool::setProtectedKeys(std::unordered_set<std::string> keys)
{
    m_protected = std::move(keys);
}

std::size_t TexturePool::queuedCount() const
{
    std::lock_guard lock(m_queueMutex);
    return m_uploadQueue.size();
}

void TexturePool::evictIfNeeded()
{
    for (auto it = m_lru.begin();
         it != m_lru.end() && m_resident.size() > m_residentLimit;) {
        if (m_protected.contains(*it)) {
            ++it;
            continue;
        }
        m_resident.erase(*it);
        m_pads.erase(*it);
        it = m_lru.erase(it);
    }
}

} // namespace slc::render
