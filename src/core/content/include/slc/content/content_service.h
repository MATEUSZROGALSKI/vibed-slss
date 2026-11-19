#pragma once

#include "slc/common/error.h"
#include "slc/content/content_provider.h"

#include <memory>
#include <vector>

namespace slc::content {

/**
 * Aggregates IContentProvider instances into a single artwork list.
 * Providers are queried in registration order; later providers may overwrite
 * nothing — each provider emits complete Artwork records and the service only
 * concatenates and de-duplicates identical (appId, kind, path) triples.
 */
class ContentService {
  public:
    void addProvider(std::unique_ptr<IContentProvider> provider);

    [[nodiscard]] Result<std::vector<Artwork>> collect(const DiscoveryContext& ctx);

  private:
    std::vector<std::unique_ptr<IContentProvider>> m_providers;
};

} // namespace slc::content
