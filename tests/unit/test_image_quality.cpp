#include "slc/content/artwork.h"
#include "slc/content/image_size.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using slc::content::Artwork;
using slc::content::ArtworkKind;
using slc::content::pickBestFit;
using slc::content::probeImageSize;

namespace {

class TmpFile {
  public:
    TmpFile() = default;
    TmpFile(const TmpFile&) = delete;
    TmpFile& operator=(const TmpFile&) = delete;
    TmpFile(TmpFile&& o) noexcept : path(std::move(o.path))
    {
        o.path.clear();
    }
    ~TmpFile()
    {
        if (!path.empty()) {
            std::filesystem::remove(path);
        }
    }
    std::filesystem::path path;
};

TmpFile writeTmp(const std::string& name,
                 const std::vector<std::uint8_t>& bytes)
{
    TmpFile f;
    f.path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(f.path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return f;
}

// Minimal PNG: signature + IHDR carrying width/height big-endian.
std::vector<std::uint8_t> png(std::uint32_t w, std::uint32_t h)
{
    std::vector<std::uint8_t> b = {0x89, 'P', 'N', 'G',
                                   0x0D, 0x0A, 0x1A, 0x0A};
    const auto push = [&b](std::uint32_t v) {
        b.push_back(static_cast<std::uint8_t>((v >> 24u) & 0xFFu));
        b.push_back(static_cast<std::uint8_t>((v >> 16u) & 0xFFu));
        b.push_back(static_cast<std::uint8_t>((v >> 8u) & 0xFFu));
        b.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    };
    push(13u); // IHDR length
    for (const char c : {'I', 'H', 'D', 'R'}) {
        b.push_back(static_cast<std::uint8_t>(c));
    }
    push(w);
    push(h);
    b.resize(b.size() + 9, 0); // bit depth..CRC (probe doesn't read these)
    return b;
}

// Minimal JPEG: SOI + APP0 + SOF0 carrying height/width big-endian.
std::vector<std::uint8_t> jpeg(std::uint32_t w, std::uint32_t h)
{
    std::vector<std::uint8_t> b = {0xFF, 0xD8, 0xFF, 0xE0,
                                   0x00, 0x10}; // APP0 len 16
    b.resize(2 + 18, 0);                       // pad APP0 payload
    const std::uint8_t sof[] = {0xFF, 0xC0, 0x00, 0x0F, 0x08,
                                static_cast<std::uint8_t>((h >> 8u) & 0xFFu),
                                static_cast<std::uint8_t>(h & 0xFFu),
                                static_cast<std::uint8_t>((w >> 8u) & 0xFFu),
                                static_cast<std::uint8_t>(w & 0xFFu)};
    b.insert(b.end(), sof, sof + sizeof(sof));
    b.resize(b.size() + 8, 0);
    return b;
}

Artwork art(ArtworkKind kind, std::uint32_t w, std::uint32_t h)
{
    Artwork a;
    a.appId = "1";
    a.kind = kind;
    a.width = w;
    a.height = h;
    return a;
}

TEST(ImageSize, Png)
{
    const auto f = writeTmp("slc_probe.png", png(1920, 620));
    const auto r = probeImageSize(f.path);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, 1920u);
    EXPECT_EQ(r->second, 620u);
}

TEST(ImageSize, Jpeg)
{
    const auto f = writeTmp("slc_probe.jpg", jpeg(600, 900));
    const auto r = probeImageSize(f.path);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first, 600u);
    EXPECT_EQ(r->second, 900u);
}

TEST(ImageSize, NotAnImage)
{
    const auto f = writeTmp("slc_probe.bin",
                            {'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r',
                             'l', 'd', '!', 'h', 'e', 'l', 'l', 'o', ' ',
                             'w', 'o', 'r', 'l', 'd', '!', 'h', 'e', 'l',
                             'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd'});
    EXPECT_FALSE(probeImageSize(f.path).has_value());
    EXPECT_FALSE(
        probeImageSize(f.path.parent_path() / "does_not_exist.png")
            .has_value());
}

TEST(PickBestFit, LandscapeSlotPrefersHeroOverTinyHeader)
{
    const std::vector<Artwork> v = {art(ArtworkKind::Hero, 1920, 620),
                                    art(ArtworkKind::Grid, 600, 900),
                                    art(ArtworkKind::Header, 460, 215)};
    // 16:9 slot at 1920px: the header's aspect (2.14) is nearest, but it
    // would upscale ~4x — the native-res hero wins.
    EXPECT_EQ(pickBestFit(v, 16.0F / 9.0F, 1920.0F), 0u);
    // At thumbnail size resolution doesn't matter -> header wins on aspect.
    EXPECT_EQ(pickBestFit(v, 16.0F / 9.0F, 300.0F), 2u);
}

TEST(PickBestFit, PortraitSlotPrefersCapsule)
{
    const std::vector<Artwork> v = {art(ArtworkKind::Hero, 1920, 620),
                                    art(ArtworkKind::Grid, 600, 900),
                                    art(ArtworkKind::Header, 460, 215)};
    // Tall cell (masonry column): the 2:3 capsule wins.
    EXPECT_EQ(pickBestFit(v, 0.5F), 1u);
}

TEST(PickBestFit, NearSquareCellPicksHeader)
{
    const std::vector<Artwork> v = {art(ArtworkKind::Hero, 1920, 620),
                                    art(ArtworkKind::Grid, 600, 900),
                                    art(ArtworkKind::Header, 460, 215)};
    // ~2:1 cell: header (2.14) beats hero (3.1) and capsule (0.67).
    EXPECT_EQ(pickBestFit(v, 2.0F), 2u);
}

TEST(PickBestFit, UnknownDimsFallBackToRankOrder)
{
    std::vector<Artwork> v = {art(ArtworkKind::Hero, 0, 0),
                              art(ArtworkKind::Grid, 0, 0)};
    EXPECT_EQ(pickBestFit(v, 1.78F), 0u);
    EXPECT_EQ(pickBestFit(v, 0.5F), 0u);
}

TEST(PickBestFit, EmptyAndInvalid)
{
    const std::vector<Artwork> v;
    EXPECT_EQ(pickBestFit(v, 1.78F), 0u);
    const std::vector<Artwork> one = {art(ArtworkKind::Hero, 1920, 620)};
    EXPECT_EQ(pickBestFit(one, 0.0F), 0u); // invalid slot aspect
}

} // namespace
