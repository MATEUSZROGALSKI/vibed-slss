#include "slc/content/steam_locator.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {
std::filesystem::path fixtureRoot()
{
    return std::filesystem::path(SLC_FIXTURES_DIR) / "steam-trees" / "basic";
}
} // namespace

using slc::content::parseLibraryFolders;
using slc::content::parseMostRecentUser;
using slc::content::enumerateInstalledApps;
using slc::content::locateInstall;
using slc::content::resolveSteamRoot;

TEST(SteamLocator, ResolvePrefersOverride)
{
    auto root = resolveSteamRoot(std::filesystem::path("X:/custom/steam"));
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(*root, std::filesystem::path("X:/custom/steam"));
}

TEST(SteamLocator, LibraryFoldersFindsAllRoots)
{
    auto libs = parseLibraryFolders(fixtureRoot());
    ASSERT_TRUE(libs.has_value());
    ASSERT_EQ(libs->size(), 2u);
    EXPECT_EQ((*libs)[0], std::filesystem::weakly_canonical(fixtureRoot()));
    EXPECT_EQ((*libs)[1],
              std::filesystem::weakly_canonical(fixtureRoot() / "library2"));
}

TEST(SteamLocator, MostRecentUserDecodesAccountId)
{
    auto user = parseMostRecentUser(fixtureRoot());
    ASSERT_TRUE(user.has_value());
    // 76561198000000042 - 76561197960265728 = 39734314
    EXPECT_EQ(*user, "39734314");
}

TEST(SteamLocator, EnumeratesManifestsAcrossLibraries)
{
    auto apps = enumerateInstalledApps(
        {fixtureRoot(), fixtureRoot() / "library2"});
    ASSERT_EQ(apps.size(), 3u);
    EXPECT_EQ(apps.at("440").name, "Team Fortress 2");
    EXPECT_EQ(apps.at("570").name, "Dota 2");
    EXPECT_EQ(apps.at("730").name, "Counter-Strike 2");
    EXPECT_EQ(apps.at("730").libraryRoot, fixtureRoot() / "library2");
}

TEST(SteamLocator, LocateInstallOnFixture)
{
    auto install = locateInstall(fixtureRoot());
    ASSERT_TRUE(install.has_value());
    EXPECT_EQ(install->installedApps.size(), 3u);
    ASSERT_TRUE(install->mostRecentUserId.has_value());
}

TEST(SteamLocator, ReadsUserAppStats)
{
    auto install = locateInstall(fixtureRoot());
    ASSERT_TRUE(install.has_value());
    ASSERT_EQ(install->userStats.size(), 2u);
    EXPECT_EQ(install->userStats.at("440").playtimeMin, 225745u);
    EXPECT_EQ(install->userStats.at("440").lastPlayed, 1741093355u);
    EXPECT_EQ(install->userStats.at("730").playtimeMin, 38u);
    EXPECT_EQ(install->userStats.count("570"), 0u); // never played
}

TEST(SteamLocator, ReadsUserAchievements)
{
    auto install = locateInstall(fixtureRoot());
    ASSERT_TRUE(install.has_value());
    // Fixture stats blob: 2 unlocked bits in group "1" + 1 in "2";
    // schema blob: 5 bits total.
    const auto& s = install->userStats.at("440");
    EXPECT_EQ(s.achEarned, 3u);
    EXPECT_EQ(s.achTotal, 5u);
}

TEST(SteamLocator, MissingRootFails)
{
    auto install = locateInstall(std::filesystem::path("Z:/no/such/steam"));
    ASSERT_FALSE(install.has_value());
}
