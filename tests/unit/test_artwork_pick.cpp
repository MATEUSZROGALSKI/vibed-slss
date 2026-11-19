// pickBestFitScored: same winner as pickBestFit, plus a comparable score
// so collage cells can rank-fit entries across different candidate sets.
#include "slc/content/artwork.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

using slc::content::Artwork;
using slc::content::ArtworkKind;
using slc::content::pickBestFit;
using slc::content::pickBestFitScored;

namespace {

Artwork art(float w, float h)
{
    Artwork a;
    a.kind = ArtworkKind::Hero;
    a.width = static_cast<std::uint32_t>(w);
    a.height = static_cast<std::uint32_t>(h);
    return a;
}

} // namespace

TEST(ArtworkPick, ScoredMatchesUnscoredWinner)
{
    const std::vector<Artwork> cands{art(3840, 1240), art(600, 900),
                                     art(1920, 1080)};
    for (const float aspect : {0.5F, 1.0F, 1.78F, 3.1F}) {
        EXPECT_EQ(pickBestFitScored(cands, aspect, 1000.0F).index,
                  pickBestFit(cands, aspect, 1000.0F));
    }
}

TEST(ArtworkPick, ScoreOrdersFitAcrossCandidateSets)
{
    // The matching loop in buildCells ranks ENTRIES by the score their
    // best variant earns for the cell — a square cell must prefer the
    // entry owning a capsule over one owning only a wide hero.
    const std::vector<Artwork> wideOnly{art(3840, 1240)};
    const std::vector<Artwork> squareish{art(600, 900), art(1024, 1024)};
    const float wideFit =
        pickBestFitScored(wideOnly, 1.0F, 800.0F).score;
    const float squareFit =
        pickBestFitScored(squareish, 1.0F, 800.0F).score;
    EXPECT_LT(squareFit, wideFit);
}

TEST(ArtworkPick, AspectDistanceDrivesScore)
{
    // Same candidate, different slot aspects: the score is the log
    // distance, so a well-matched slot beats a mismatched one.
    const std::vector<Artwork> hero{art(3840, 1240)}; // ~3.1 aspect
    EXPECT_LT(pickBestFitScored(hero, 3.0F, 0.0F).score,
              pickBestFitScored(hero, 1.0F, 0.0F).score);
}

TEST(ArtworkPick, EmptyVectorReturnsInfScore)
{
    const std::vector<Artwork> none;
    const auto fit = pickBestFitScored(none, 1.0F, 0.0F);
    EXPECT_EQ(fit.index, 0U);
    EXPECT_EQ(fit.score, std::numeric_limits<float>::max());
}

TEST(ArtworkPick, HeavyUpscaleIsPenalized)
{
    // A 460px banner filling a 2400px slot scores worse than a
    // native-size piece of the same aspect.
    const std::vector<Artwork> tiny{art(460, 215)};
    const std::vector<Artwork> big{art(3840, 1793)}; // same ~2.14 aspect
    EXPECT_LT(pickBestFitScored(big, 2.14F, 2400.0F).score,
              pickBestFitScored(tiny, 2.14F, 2400.0F).score);
}
