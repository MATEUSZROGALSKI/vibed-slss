#pragma once

#include "slc/content/content_provider.h"

namespace slc::content {

/**
 * Discovers artwork shipped in the local Steam install, following the
 * precedence in doc appendix A:
 *   1. <steam>/userdata/<uid>/config/grid/<appid>*  (user-custom art)
 *   2. <steam>/appcache/librarycache/<appid>_*      (official cache)
 * Only artwork for *installed* games (appmanifest_*.acf) is emitted.
 */
class SteamLocalProvider : public IContentProvider {
  public:
    [[nodiscard]] std::string_view name() const noexcept override { return "steam-local"; }
    std::vector<Artwork> discover(const DiscoveryContext& ctx) override;
};

} // namespace slc::content
