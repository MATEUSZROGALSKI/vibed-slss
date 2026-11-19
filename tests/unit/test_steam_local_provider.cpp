#include "slc/content/steam_local_provider.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <set>

namespace {

std::filesystem::path fixtureRoot()
{
    return std::filesystem::path(SLC_FIXTURES_DIR) / "steam-trees" / "basic";
}

slc::content::DiscoveryContext fixtureContext()
{
    slc::content::DiscoveryContext ctx;
    ctx.steamRootOverride = fixtureRoot();
    return ctx;
}

const slc::content::Artwork* findFor(const std::vector<slc::content::Artwork>& items,
                                     std::string_view appId,
                                     slc::content::ArtworkKind kind)
{
    for (const auto& a : items) {
        if (a.appId == appId && a.kind == kind) {
            return &a;
        }
    }
    return nullptr;
}

} // namespace

using slc::content::Artwork;
using slc::content::ArtworkKind;
using slc::content::SteamLocalProvider;

TEST(SteamLocalProvider, DiscoversInstalledArtwork)
{
    SteamLocalProvider provider;
    auto items = provider.discover(fixtureContext());

    // 440: grid+hero+header+logo, 570: grid+hero+icon, 730: grid+header,
    // 999: header = 10 (installedOnly=false keeps non-installed 999;
    // 999_unknown_game_library_hero and 440_library_hero_blur are skipped
    // by filename rules; 555555/666666 have headers but are filtered as
    // appinfo type dlc/tool)
    EXPECT_EQ(items.size(), 10u);
    EXPECT_EQ(findFor(items, "555555", ArtworkKind::Header), nullptr);
    EXPECT_EQ(findFor(items, "666666", ArtworkKind::Header), nullptr);

    for (const auto& a : items) {
        EXPECT_FALSE(a.gameName.empty());
        EXPECT_TRUE(std::filesystem::exists(a.path));
        EXPECT_EQ(a.source, "steam-local");
    }
}

TEST(SteamLocalProvider, UserCustomArtWins)
{
    SteamLocalProvider provider;
    auto items = provider.discover(fixtureContext());

    const Artwork* hero = findFor(items, "440", ArtworkKind::Hero);
    ASSERT_NE(hero, nullptr);
    EXPECT_EQ(hero->path.filename(), "440_hero.png");
    EXPECT_NE(hero->path.string().find("userdata"), std::string::npos);
    EXPECT_EQ(hero->gameName, "Team Fortress 2");
}

TEST(SteamLocalProvider, GameNamesComeFromManifests)
{
    SteamLocalProvider provider;
    auto items = provider.discover(fixtureContext());
    EXPECT_EQ(findFor(items, "730", ArtworkKind::Header)->gameName,
              "Counter-Strike 2");
}

TEST(SteamLocalProvider, GameNamesFallBackToAppInfo)
{
    SteamLocalProvider provider;
    auto items = provider.discover(fixtureContext());
    // 999 has no appmanifest; its name comes from appcache/appinfo.vdf.
    const Artwork* header = findFor(items, "999", ArtworkKind::Header);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(header->gameName, "Fixture Hidden Gem");
}

TEST(SteamLocalProvider, InstalledOnlyDropsNonInstalledApps)
{
    auto ctx = fixtureContext();
    ctx.installedOnly = true;
    SteamLocalProvider provider;
    auto items = provider.discover(ctx);
    EXPECT_EQ(items.size(), 9u); // 999's header gone
    EXPECT_EQ(findFor(items, "999", ArtworkKind::Header), nullptr);
}

TEST(SteamLocalProvider, ExcludeAppIdsFilter)
{
    auto ctx = fixtureContext();
    ctx.excludeAppIds.insert("440");
    SteamLocalProvider provider;
    auto items = provider.discover(ctx);
    EXPECT_EQ(items.size(), 6u); // 440's four pieces gone
    EXPECT_EQ(findFor(items, "440", ArtworkKind::Grid), nullptr);
}

TEST(SteamLocalProvider, IncludeKindsFilter)
{
    auto ctx = fixtureContext();
    ctx.includeKinds = {ArtworkKind::Hero};
    SteamLocalProvider provider;
    auto items = provider.discover(ctx);
    EXPECT_EQ(items.size(), 2u); // 440 + 570 heroes
    for (const auto& a : items) {
        EXPECT_EQ(a.kind, ArtworkKind::Hero);
    }
}

TEST(SteamLocalProvider, MissingSteamYieldsEmptyNotCrash)
{
    slc::content::DiscoveryContext ctx;
    ctx.steamRootOverride = std::filesystem::path("Z:/no/steam/here");
    SteamLocalProvider provider;
    EXPECT_TRUE(provider.discover(ctx).empty());
}

TEST(SteamLocalProvider, OutputIsSorted)
{
    SteamLocalProvider provider;
    auto items = provider.discover(fixtureContext());
    EXPECT_TRUE(std::ranges::is_sorted(items, {}, &Artwork::appId));
}
