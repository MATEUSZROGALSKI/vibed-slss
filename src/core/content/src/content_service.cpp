#include "slc/content/content_service.h"

#include "slc/common/logging.h"

#include <set>
#include <tuple>

namespace slc::content {

void ContentService::addProvider(std::unique_ptr<IContentProvider> provider)
{
    m_providers.push_back(std::move(provider));
}

Result<std::vector<Artwork>> ContentService::collect(const DiscoveryContext& ctx)
{
    if (m_providers.empty()) {
        return std::unexpected(
            Error::make(ErrorCode::NotFound, "no content providers registered"));
    }
    std::vector<Artwork> all;
    std::set<std::tuple<std::string, int, std::string>> seen;
    for (auto& provider : m_providers) {
        auto items = provider->discover(ctx);
        for (auto& item : items) {
            auto key = std::make_tuple(item.appId, static_cast<int>(item.kind),
                                       item.path.string());
            if (seen.insert(std::move(key)).second) {
                all.push_back(std::move(item));
            }
        }
        if (ctx.stopToken.stop_requested()) {
            SLC_LOG_INFO("discovery cancelled after provider {}", provider->name());
            break;
        }
    }
    return all;
}

} // namespace slc::content
