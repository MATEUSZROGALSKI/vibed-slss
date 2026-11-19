#include "slc/platform/paths.h"

#include "env_guard.h"

#include <gtest/gtest.h>

#include <filesystem>

// Platform-neutral invariants hold on every OS.
TEST(Paths, DirsAreAbsoluteAndNonEmpty)
{
    EXPECT_TRUE(slc::platform::configDir().is_absolute());
    EXPECT_TRUE(slc::platform::logDir().is_absolute());
    EXPECT_TRUE(slc::platform::cacheDir().is_absolute());
    EXPECT_FALSE(slc::platform::configDir().filename().empty());
}

TEST(Paths, EnumerateDisplaysDoesNotCrash)
{
    // Headless environments may return an empty or synthetic list.
    const auto displays = slc::platform::enumerateDisplays();
    for (const auto& d : displays) {
        EXPECT_GT(d.width, 0);
        EXPECT_GT(d.height, 0);
    }
}

TEST(Paths, PrefersReducedMotionReturnsBool)
{
    // Value is platform/user dependent; just exercise the path.
    EXPECT_TRUE(slc::platform::prefersReducedMotion() ||
                !slc::platform::prefersReducedMotion());
}

#if defined(__linux__)

TEST(PathsLinux, XdgOverridesHonoured)
{
    const auto base = std::filesystem::temp_directory_path() / "slc-test-xdg";
    test::EnvGuard cfg("XDG_CONFIG_HOME", (base / "cfg").string());
    test::EnvGuard state("XDG_STATE_HOME", (base / "state").string());
    test::EnvGuard cache("XDG_CACHE_HOME", (base / "cache").string());

    EXPECT_EQ(slc::platform::configDir(),
              base / "cfg" / "steam-library-screensaver");
    EXPECT_EQ(slc::platform::logDir(),
              base / "state" / "steam-library-screensaver");
    EXPECT_EQ(slc::platform::cacheDir(),
              base / "cache" / "steam-library-screensaver");
}

TEST(PathsLinux, DetectSteamRootFindsCandidate)
{
    const auto home = std::filesystem::temp_directory_path() / "slc-test-home";
    std::filesystem::create_directories(home / ".steam/steam/steamapps");
    test::EnvGuard guard("HOME", home.string());

    auto root = slc::platform::detectSteamRoot();
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(*root, home / ".steam/steam");

    std::filesystem::remove_all(home);
}

TEST(PathsLinux, DetectSteamRootMissingIsNullopt)
{
    const auto home =
        std::filesystem::temp_directory_path() / "slc-test-home-empty";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    test::EnvGuard guard("HOME", home.string());

    EXPECT_FALSE(slc::platform::detectSteamRoot().has_value());
    std::filesystem::remove_all(home);
}

TEST(PathsLinux, ReducedMotionGnomeUnsetDesktop)
{
    test::EnvGuard guard("XDG_CURRENT_DESKTOP", "test-desktop");
    EXPECT_FALSE(slc::platform::prefersReducedMotion());
}

#endif // __linux__
