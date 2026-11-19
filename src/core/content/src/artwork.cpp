#include "slc/content/artwork.h"

#include <cmath>
#include <limits>

namespace slc::content {

std::string_view toString(ArtworkKind kind) noexcept
{
    switch (kind) {
    case ArtworkKind::Hero:   return "hero";
    case ArtworkKind::Grid:   return "grid";
    case ArtworkKind::Logo:   return "logo";
    case ArtworkKind::Header: return "header";
    case ArtworkKind::Icon:   return "icon";
    }
    return "unknown";
}

std::optional<ArtworkKind> artworkKindFromString(std::string_view name) noexcept
{
    if (name == "hero")   return ArtworkKind::Hero;
    if (name == "grid")   return ArtworkKind::Grid;
    if (name == "logo")   return ArtworkKind::Logo;
    if (name == "header") return ArtworkKind::Header;
    if (name == "icon")   return ArtworkKind::Icon;
    return std::nullopt;
}

BestFit pickBestFitScored(const std::vector<Artwork>& candidates,
                          float slotAspect, float slotMaxDim)
{
    BestFit best{0, std::numeric_limits<float>::max()};
    if (candidates.empty() || slotAspect <= 0.0F) {
        return best;
    }
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        float score;
        if (c.width == 0 || c.height == 0) {
            score = 1.5F; // unknown dims: neutral guess, mildly penalized
        } else {
            const float imgAspect =
                static_cast<float>(c.width) / static_cast<float>(c.height);
            score = std::abs(std::log(imgAspect / slotAspect));
            // Penalize variants that must be upscaled well past their
            // native size to fill the slot (a 460px header at 4K).
            if (slotMaxDim > 0.0F) {
                const float cand =
                    static_cast<float>(std::max(c.width, c.height));
                const float need = slotMaxDim * 0.6F;
                if (cand < need) {
                    score += std::min(std::log(need / cand), 1.2F) * 0.5F;
                }
            }
        }
        score += static_cast<float>(i) * 0.05F; // rank order breaks near-ties
        if (score < best.score) {
            best.score = score;
            best.index = i;
        }
    }
    return best;
}

std::size_t pickBestFit(const std::vector<Artwork>& candidates,
                        float slotAspect, float slotMaxDim)
{
    return pickBestFitScored(candidates, slotAspect, slotMaxDim).index;
}

} // namespace slc::content
