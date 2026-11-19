#pragma once

#include "slc/content/artwork.h"
#include "slc/content/discovery_context.h"

#include <string_view>
#include <vector>

namespace slc::content {

class IContentProvider {
  public:
    virtual ~IContentProvider() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    virtual std::vector<Artwork> discover(const DiscoveryContext& ctx) = 0;
};

} // namespace slc::content
