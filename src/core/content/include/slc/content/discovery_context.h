#pragma once

#include "slc/content/artwork.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <unordered_set>

namespace slc::content {

struct DiscoveryContext {
    /// Explicit Steam root; nullopt = auto-detect (env override, then OS probe).
    std::optional<std::filesystem::path> steamRootOverride;

    /// AppIds to skip entirely (e.g. "228980" Steamworks redistributables).
    std::unordered_set<std::string> excludeAppIds;

    /// Kinds to emit; empty set means "all kinds".
    std::unordered_set<ArtworkKind> includeKinds;

    /// Discard artworks narrower than this when the width is known (0 = keep).
    std::uint32_t minImageWidth = 0;

    /// true = keep only apps with a local appmanifest (installed). false
    /// (default) = everything Steam has artwork-cached, i.e. the whole
    /// library including non-installed games.
    bool installedOnly = false;

    std::stop_token stopToken;
};

} // namespace slc::content
