#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slc::content {

enum class ArtworkKind {
    Hero,
    Grid,
    Logo,
    Header,
    Icon,
};

[[nodiscard]] std::string_view toString(ArtworkKind kind) noexcept;
[[nodiscard]] std::optional<ArtworkKind> artworkKindFromString(std::string_view name) noexcept;

struct Artwork {
    std::string appId;            // "440"
    std::string gameName;         // "Team Fortress 2"
    ArtworkKind kind{};           // Hero, Grid, Logo, Header, Icon
    std::filesystem::path path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string source = "steam-local"; // "steam-local" | "steamgriddb"
    std::uint64_t playtimeMin = 0;  // user stats, 0 = never played/unknown
    std::uint64_t lastPlayed = 0;   // unix timestamp, 0 = never/unknown
    std::uint32_t achEarned = 0;    // unlocked achievements (0 = unknown)
    std::uint32_t achTotal = 0;     // total achievements (0 = unknown)
};

/// Result of scored best-fit picking: `index` into the candidate list and
/// the fit `score` (lower is better; log-aspect distance plus penalties).
struct BestFit {
    std::size_t index = 0;
    float score = 0.0F;
};

/**
 * Scored variant of pickBestFit: same winner, plus the score so callers can
 * compare fit quality across DIFFERENT candidate sets — e.g. matching the
 * best playlist entry to a collage cell rather than just the best variant
 * within an entry. Returns {0, +inf} for an empty vector.
 */
[[nodiscard]] BestFit pickBestFitScored(
    const std::vector<Artwork>& candidates, float slotAspect,
    float slotMaxDim = 0.0F);

/**
 * Picks the index of the candidate whose aspect ratio best matches the slot
 * (log-space distance), preferring higher-resolution variants and earlier
 * (better-ranked) entries on near-ties. `candidates` must be ordered
 * best-first. `slotMaxDim` is the slot's longest side in pixels — variants
 * that would be heavily upscaled to fill it are penalized (0 disables the
 * check). Unknown dimensions count as a mild penalty. Returns 0 for an
 * empty vector.
 */
[[nodiscard]] std::size_t pickBestFit(const std::vector<Artwork>& candidates,
                                      float slotAspect,
                                      float slotMaxDim = 0.0F);

} // namespace slc::content
