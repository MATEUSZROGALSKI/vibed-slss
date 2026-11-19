#include "slc/content/steam_locator.h"

#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/content/appinfo_names.h"
#include "slc/content/vdf.h"
#include "slc/platform/paths.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace slc::content {
namespace {

Result<std::string> readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(Error::make(ErrorCode::NotFound,
                                         "cannot open " + path.string()));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Minimal binary-VDF reader (type u8 + cstring key + value), the same
// encoding used by appinfo.vdf v39/v40 and by the appcache/stats blobs.
// The stats files have no magic header: the buffer IS the root entry list.
struct BinReader {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* end = nullptr;

    std::optional<std::string_view> cstr()
    {
        const void* nul = std::memchr(p, 0, static_cast<std::size_t>(end - p));
        if (nul == nullptr) {
            return std::nullopt;
        }
        const auto* term = static_cast<const std::uint8_t*>(nul);
        std::string_view out(reinterpret_cast<const char*>(p),
                             static_cast<std::size_t>(term - p));
        p = term + 1;
        return out;
    }

    bool skip(std::size_t n)
    {
        if (end - p < static_cast<std::ptrdiff_t>(n)) {
            return false;
        }
        p += n;
        return true;
    }

    bool skipValue(std::uint8_t type, int depth)
    {
        if (depth > 8) {
            return false;
        }
        switch (type) {
        case 0x00:
            return skipSection(depth + 1);
        case 0x01:
            return cstr().has_value();
        case 0x02: // int32
        case 0x03: // float32
        case 0x04: // pointer
        case 0x06: // color
            return skip(4);
        case 0x05: { // wstring: u16 chars + utf-16 payload
            if (end - p < 2) {
                return false;
            }
            const std::uint16_t n =
                static_cast<std::uint16_t>(p[0] | (p[1] << 8));
            p += 2;
            return skip(static_cast<std::size_t>(n) * 2u);
        }
        case 0x07: // uint64
        case 0x09: // int64
            return skip(8);
        default:
            return false;
        }
    }

    bool skipSection(int depth)
    {
        while (p < end) {
            const std::uint8_t type = *p++;
            if (type == 0x08 || type == 0x0A) {
                return true;
            }
            if (!cstr() || !skipValue(type, depth)) {
                return false;
            }
        }
        return false;
    }

    /// Consumes entries until this section's END marker, counting each.
    void countSection(std::uint32_t& out, int depth)
    {
        while (p < end) {
            const std::uint8_t type = *p++;
            if (type == 0x08 || type == 0x0A) {
                return;
            }
            if (!cstr() || !skipValue(type, depth)) {
                return;
            }
            ++out;
        }
    }

    /// Walks every section recursively; when a section's key matches
    /// `target`, its entry count is added to `out`.
    void scan(std::string_view target, std::uint32_t& out, int depth)
    {
        while (p < end) {
            const std::uint8_t type = *p++;
            if (type == 0x08 || type == 0x0A) {
                return;
            }
            const auto key = cstr();
            if (!key) {
                return;
            }
            if (type == 0x00) {
                if (*key == target) {
                    countSection(out, depth + 1);
                } else if (depth < 8) {
                    scan(target, out, depth + 1);
                }
            } else if (!skipValue(type, depth)) {
                return;
            }
        }
    }
};

std::optional<std::vector<std::uint8_t>> readWholeFile(
    const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > std::uintmax_t{64} * 1024 * 1024) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> buf(size);
    in.read(reinterpret_cast<char*>(buf.data()),
            static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size)) {
        return std::nullopt;
    }
    return buf;
}

std::uint32_t countSectionEntries(const std::vector<std::uint8_t>& buf,
                                  std::string_view sectionName)
{
    std::uint32_t count = 0;
    BinReader r{buf.data(), buf.data() + buf.size()};
    r.scan(sectionName, count, 0);
    return count;
}

} // namespace

Result<std::filesystem::path> resolveSteamRoot(
    const std::optional<std::filesystem::path>& override)
{
    if (override) {
        return *override;
    }
    if (auto fromEnv = env::getPath(env::kSteamRootVar)) {
        SLC_LOG_INFO("steam root from {}: {}", env::kSteamRootVar, fromEnv->string());
        return *fromEnv;
    }
    if (auto detected = platform::detectSteamRoot()) {
        return *detected;
    }
    return std::unexpected(
        Error::make(ErrorCode::NotFound, "Steam installation not found"));
}

Result<std::vector<std::filesystem::path>> parseLibraryFolders(
    const std::filesystem::path& steamRoot)
{
    const auto resolveRoot = [&steamRoot](std::filesystem::path p) {
        // Steam writes absolute paths; tests/tools may use root-relative ones.
        if (p.is_relative()) {
            p = steamRoot / p;
        }
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : canonical;
    };
    const auto vdfPath = steamRoot / "steamapps" / "libraryfolders.vdf";
    auto text = readTextFile(vdfPath);
    if (!text) {
        return std::unexpected(text.error());
    }
    auto doc = vdf::parse(*text);
    if (!doc) {
        return std::unexpected(doc.error());
    }

    std::vector<std::filesystem::path> roots;
    const vdf::Node* folders = doc->find("libraryfolders");
    if (folders == nullptr) {
        // Pre-2021 format: the file itself is the folder list; the install root
        // is the only library.
        roots.push_back(steamRoot);
        return roots;
    }
    for (const vdf::Node& entry : folders->children) {
        if (entry.isObject) {
            const std::string_view path = entry.get("path");
            if (!path.empty()) {
                roots.push_back(resolveRoot(std::filesystem::path(path)));
            }
        } else if (entry.key.size() <= 3 &&
                   std::all_of(entry.key.begin(), entry.key.end(), ::isdigit)) {
            // Flat "N" "path" format.
            roots.push_back(resolveRoot(std::filesystem::path(entry.value)));
        }
    }
    if (roots.empty()) {
        roots.push_back(steamRoot);
    }
    return roots;
}

std::optional<std::string> parseMostRecentUser(const std::filesystem::path& steamRoot)
{
    const auto vdfPath = steamRoot / "config" / "loginusers.vdf";
    auto text = readTextFile(vdfPath);
    if (!text) {
        return std::nullopt;
    }
    auto doc = vdf::parse(*text);
    if (!doc) {
        return std::nullopt;
    }
    const vdf::Node* users = doc->find("users");
    if (users == nullptr) {
        return std::nullopt;
    }
    const vdf::Node* autoLogin = nullptr;
    const vdf::Node* newest = nullptr;
    unsigned long long newestTs = 0;
    for (const vdf::Node& user : users->children) {
        if (!user.isObject) {
            continue;
        }
        if (user.get("mostrecent") == "1") {
            newest = &user;
            break;
        }
        if (user.get("autologin") == "1") {
            autoLogin = &user;
        }
        unsigned long long ts = 0;
        try {
            ts = std::stoull(std::string(user.get("timestamp")));
        } catch (...) {
            SLC_LOG_DEBUG("loginusers: non-numeric timestamp '{}'",
                          std::string(user.get("timestamp")));
        }
        if (ts > newestTs) {
            newestTs = ts;
            newest = &user;
        }
    }
    const vdf::Node* user = newest != nullptr ? newest : autoLogin;
    if (user == nullptr && users->children.size() == 1) {
        user = &users->children.front();
    }
    if (user == nullptr) {
        return std::nullopt;
    }
    // loginusers key is the 64-bit SteamID; userdata dirs use the
    // 32-bit account id (steamID64 - 76561197960265728).
    try {
        const unsigned long long id64 = std::stoull(user->key);
        constexpr unsigned long long kSteamId64Base = 76561197960265728ULL;
        if (id64 > kSteamId64Base) {
            return std::to_string(id64 - kSteamId64Base);
        }
    } catch (...) {
        // Fall through and return the raw key.
        SLC_LOG_DEBUG("loginusers: non-numeric key '{}'", user->key);
    }
    return user->key;
}

std::unordered_map<std::string, UserAppStats> readUserAppStats(
    const std::filesystem::path& steamRoot, std::string_view userId)
{
    std::unordered_map<std::string, UserAppStats> out;
    if (userId.empty()) {
        return out;
    }
    const auto path = steamRoot / "userdata" / userId / "config" /
                      "localconfig.vdf";
    auto text = readTextFile(path);
    if (!text) {
        return out;
    }
    auto doc = vdf::parse(*text);
    if (!doc) {
        SLC_LOG_WARN("malformed localconfig.vdf {}", path.string());
        return out;
    }
    // "UserLocalConfigStore" -> "Software" -> "Valve" -> "Steam" -> "apps".
    const vdf::Node* node = doc->find("UserLocalConfigStore");
    for (const char* key : {"Software", "Valve", "Steam", "apps"}) {
        node = node != nullptr ? node->find(key) : nullptr;
    }
    if (node == nullptr) {
        return out;
    }
    for (const vdf::Node& app : node->children) {
        if (!app.isObject) {
            continue;
        }
        UserAppStats s;
        try {
            if (const auto pt = app.get("Playtime"); !pt.empty()) {
                s.playtimeMin = std::stoull(std::string(pt));
            }
            if (const auto lp = app.get("LastPlayed"); !lp.empty()) {
                s.lastPlayed = std::stoull(std::string(lp));
            }
        } catch (...) {
            continue;
        }
        out[app.key] = s;
    }
    return out;
}

std::unordered_map<std::string, UserAchievements> readUserAchievements(
    const std::filesystem::path& steamRoot, std::string_view userId)
{
    std::unordered_map<std::string, UserAchievements> out;
    const auto statsDir = steamRoot / "appcache" / "stats";
    std::error_code ec;
    if (!std::filesystem::is_directory(statsDir, ec)) {
        return out;
    }
    // File names: UserGameStats_<sid>_<appid>.bin (per-user progress) and
    // UserGameStatsSchema_<appid>.bin (achievement definitions).
    const std::string userPrefix =
        "UserGameStats_" + std::string(userId) + "_";
    for (const auto& entry :
         std::filesystem::directory_iterator(statsDir, ec)) {
        if (ec) {
            break;
        }
        const auto name = entry.path().filename().string();
        if (!entry.is_regular_file(ec) || !name.ends_with(".bin")) {
            continue;
        }
        if (name.starts_with(userPrefix)) {
            const std::string appId =
                name.substr(userPrefix.size(),
                            name.size() - userPrefix.size() - 4);
            const auto buf = readWholeFile(entry.path());
            if (buf) {
                // cache -> <stat group> -> AchievementTimes -> <bit> = ts
                out[appId].earned =
                    countSectionEntries(*buf, "AchievementTimes");
            }
        } else if (name.starts_with("UserGameStatsSchema_")) {
            const std::string appId =
                name.substr(20, name.size() - 20 - 4);
            const auto buf = readWholeFile(entry.path());
            if (buf) {
                // <appid> -> stats -> <stat group> -> bits -> <bit> = def
                out[appId].total = countSectionEntries(*buf, "bits");
            }
        }
    }
    return out;
}

std::unordered_map<std::string, InstalledApp> enumerateInstalledApps(
    const std::vector<std::filesystem::path>& libraryRoots)
{
    std::unordered_map<std::string, InstalledApp> apps;
    for (const auto& lib : libraryRoots) {
        const auto steamapps = lib / "steamapps";
        std::error_code ec;
        if (!std::filesystem::exists(steamapps, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(steamapps, ec)) {
            const auto filename = entry.path().filename().string();
            if (!filename.starts_with("appmanifest_") || !filename.ends_with(".acf")) {
                continue;
            }
            auto text = readTextFile(entry.path());
            if (!text) {
                continue;
            }
            auto doc = vdf::parse(*text);
            if (!doc) {
                SLC_LOG_WARN("malformed manifest {}", entry.path().string());
                continue;
            }
            const vdf::Node* state = doc->find("AppState");
            if (state == nullptr) {
                continue;
            }
            const std::string_view appId = state->get("appid");
            if (appId.empty()) {
                continue;
            }
            InstalledApp app;
            app.appId = appId;
            app.name = state->get("name");
            app.libraryRoot = lib;
            apps[std::string(appId)] = std::move(app);
        }
    }
    return apps;
}

Result<SteamInstall> locateInstall(const std::optional<std::filesystem::path>& steamRootOverride)
{
    auto root = resolveSteamRoot(steamRootOverride);
    if (!root) {
        return std::unexpected(root.error());
    }
    std::error_code ec;
    if (!std::filesystem::exists(*root, ec)) {
        return std::unexpected(Error::make(ErrorCode::NotFound,
                                           "steam root does not exist: " + root->string()));
    }

    SteamInstall install;
    install.root = *root;

    auto libs = parseLibraryFolders(*root);
    install.libraryRoots = libs ? std::move(*libs)
                                : std::vector<std::filesystem::path>{*root};
    if (!libs) {
        SLC_LOG_WARN("libraryfolders.vdf unreadable, using install root only: {}",
                     libs.error().message);
    }

    install.mostRecentUserId = parseMostRecentUser(*root);
    install.installedApps = enumerateInstalledApps(install.libraryRoots);
    install.appInfo =
        readAppInfo(*root / "appcache" / "appinfo.vdf");
    if (install.mostRecentUserId) {
        install.userStats =
            readUserAppStats(*root, *install.mostRecentUserId);
        for (auto& [appId, ach] :
             readUserAchievements(*root, *install.mostRecentUserId)) {
            auto& s = install.userStats[appId];
            s.achEarned = ach.earned;
            s.achTotal = ach.total;
        }
    }
    SLC_LOG_INFO("steam: {} libraries, {} installed apps, {} app names, "
                 "{} app stats",
                 install.libraryRoots.size(), install.installedApps.size(),
                 install.appInfo.size(), install.userStats.size());
    return install;
}

} // namespace slc::content
