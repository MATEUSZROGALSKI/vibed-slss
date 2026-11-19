#pragma once

#include "slc/common/error.h"
#include "slc/content/appinfo_names.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slc::content {

/// A game installed in one of the Steam libraries (from appmanifest_*.acf).
struct InstalledApp {
    std::string appId;
    std::string name;
    std::filesystem::path libraryRoot; // the library that contains steamapps/
};

/// Per-user stats for one app, from userdata/<id>/config/localconfig.vdf.
struct UserAppStats {
    std::uint64_t playtimeMin = 0; ///< total playtime in minutes
    std::uint64_t lastPlayed = 0;  ///< unix timestamp, 0 = never
    /// Achievement progress from the client's appcache/stats cache; both 0
    /// when the game has no achievements or Steam never cached them.
    std::uint32_t achEarned = 0;
    std::uint32_t achTotal = 0;
};

struct UserAchievements {
    std::uint32_t earned = 0;
    std::uint32_t total = 0;
};

struct SteamInstall {
    std::filesystem::path root; // e.g. C:/Program Files (x86)/Steam
    std::vector<std::filesystem::path> libraryRoots;
    /// SteamID3-ish user directory name under userdata/ for the most recent
    /// login (loginusers.vdf "mostrecent" = "1"). Empty when unknown.
    std::optional<std::string> mostRecentUserId;
    std::unordered_map<std::string, InstalledApp> installedApps; // appId -> app
    /// appId -> {name, type} for every app known to appcache/appinfo.vdf
    /// (includes the whole library, not only installed games). Empty when
    /// the file is missing or unparsable.
    std::unordered_map<std::string, AppInfoEntry> appInfo;
    /// appId -> playtime/last-played for the most-recent user. Empty when
    /// no user is known or localconfig.vdf is unreadable.
    std::unordered_map<std::string, UserAppStats> userStats;
};

/**
 * Resolves the Steam install root: explicit override -> SLC_STEAM_ROOT env ->
 * platform detection (registry / well-known paths).
 */
[[nodiscard]] Result<std::filesystem::path> resolveSteamRoot(
    const std::optional<std::filesystem::path>& override);

/** Parses <root>/steamapps/libraryfolders.vdf into all library roots. */
[[nodiscard]] Result<std::vector<std::filesystem::path>> parseLibraryFolders(
    const std::filesystem::path& steamRoot);

/** Most-recently-logged-in user id from <root>/config/loginusers.vdf. */
[[nodiscard]] std::optional<std::string> parseMostRecentUser(
    const std::filesystem::path& steamRoot);

/** Per-app playtime/last-played from
 * <root>/userdata/<userId>/config/localconfig.vdf. Empty map when the file
 * or user is absent. */
[[nodiscard]] std::unordered_map<std::string, UserAppStats> readUserAppStats(
    const std::filesystem::path& steamRoot, std::string_view userId);

/** Per-app achievement counts from <root>/appcache/stats/: unlocked count
 * from UserGameStats_<userId>_<appid>.bin (AchievementTimes entries) and the
 * total from UserGameStatsSchema_<appid>.bin (achievement "bits"). The cache
 * only exists for games the user has played or browsed; absent files yield
 * no entry rather than zeroed ones. */
[[nodiscard]] std::unordered_map<std::string, UserAchievements>
readUserAchievements(const std::filesystem::path& steamRoot,
                     std::string_view userId);

/** Reads steamapps/appmanifest_*.acf under every library root. */
[[nodiscard]] std::unordered_map<std::string, InstalledApp> enumerateInstalledApps(
    const std::vector<std::filesystem::path>& libraryRoots);

/** Full install snapshot; fails only when the root itself is unusable. */
[[nodiscard]] Result<SteamInstall> locateInstall(
    const std::optional<std::filesystem::path>& steamRootOverride);

} // namespace slc::content
