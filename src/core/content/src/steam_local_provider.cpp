#include "slc/content/steam_local_provider.h"

#include "slc/common/logging.h"
#include "slc/content/image_size.h"
#include "slc/content/steam_locator.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace slc::content {
namespace {

bool hasArtworkExtension(const std::filesystem::path& path)
{
    const std::string ext = path.extension().string();
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

/// "<appid>_<rest>" -> (appid, rest). Returns false when the stem has no
/// appid prefix or the file is a derived/blurred variant.
bool splitLibraryCacheName(const std::string& stem, std::string_view& appId,
                           std::string_view& rest)
{
    const auto underscore = stem.find('_');
    if (underscore == std::string::npos || underscore == 0) {
        return false;
    }
    appId = std::string_view(stem).substr(0, underscore);
    if (!std::all_of(appId.begin(), appId.end(), ::isdigit)) {
        return false;
    }
    rest = std::string_view(stem).substr(underscore + 1);
    return rest.find("blur") == std::string_view::npos;
}

std::optional<ArtworkKind> kindFromLibraryCacheSuffix(std::string_view rest)
{
    if (rest.starts_with("library_600x900") || rest.starts_with("library_capsule"))
        return ArtworkKind::Grid;
    if (rest.starts_with("library_hero"))
        return ArtworkKind::Hero;
    if (rest.starts_with("header") || rest.starts_with("capsule"))
        return ArtworkKind::Header;
    if (rest.starts_with("logo"))
        return ArtworkKind::Logo;
    if (rest.starts_with("icon"))
        return ArtworkKind::Icon;
    return std::nullopt;
}

/// Modern layout: librarycache/<appid>/<kind>.<ext> — the appid comes from the
/// directory name, the file stem is the bare kind. Hash-named files (long hex
/// stems) are CDN variants we cannot classify.
std::optional<ArtworkKind> kindFromBareStem(std::string_view stem)
{
    if (stem.find("blur") != std::string_view::npos || stem.size() >= 32) {
        return std::nullopt;
    }
    return kindFromLibraryCacheSuffix(stem);
}

/// userdata/<uid>/config/grid file naming:
///   <appid>.<ext> / <appid>p.<ext>  grid capsule (landscape / portrait)
///   <appid>_hero.<ext>              hero
///   <appid>_logo.<ext>              logo
///   <appid>_icon.<ext>              icon
///   <appid>_header.<ext>            header
bool parseCustomGridName(const std::string& stem, std::string_view& appId,
                         ArtworkKind& kind)
{
    const auto underscore = stem.find('_');
    if (underscore == std::string::npos) {
        // "<appid>" or "<appid>p"
        std::string_view id = stem;
        if (!id.empty() && id.back() == 'p') {
            id.remove_suffix(1);
        }
        if (id.empty() || !std::all_of(id.begin(), id.end(), ::isdigit)) {
            return false;
        }
        appId = id;
        kind = ArtworkKind::Grid;
        return true;
    }
    appId = std::string_view(stem).substr(0, underscore);
    if (appId.empty() || !std::all_of(appId.begin(), appId.end(), ::isdigit)) {
        return false;
    }
    const std::string_view suffix = std::string_view(stem).substr(underscore + 1);
    if (suffix == "hero")   kind = ArtworkKind::Hero;
    else if (suffix == "logo")   kind = ArtworkKind::Logo;
    else if (suffix == "icon")   kind = ArtworkKind::Icon;
    else if (suffix == "header") kind = ArtworkKind::Header;
    else return false;
    return true;
}

using Key = std::pair<std::string, ArtworkKind>;

/// Steam store type -> keep? Only real games make the playlist; DLCs,
/// soundtracks, tools, dedicated servers, videos etc. are excluded. Apps
/// missing from appinfo.vdf fail open (kept) so a truncated cache can't
/// wipe the library.
bool isGameType(const AppInfoEntry* info)
{
    if (info == nullptr || info->type.empty()) {
        return true;
    }
    std::string t = info->type;
    std::ranges::transform(t, t.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return t == "game";
}

/// Adds one artwork if the (appId, kind) slot is free or override is allowed.
void put(std::map<Key, Artwork>& out, std::string_view appId, ArtworkKind kind,
         const std::filesystem::path& path, bool overrideExisting)
{
    const auto fill = [&path, kind](Artwork& art) {
        if (auto dims = probeImageSize(path)) {
            art.width = dims->first;
            art.height = dims->second;
        }
    };
    Key key{std::string(appId), kind};
    if (overrideExisting) {
        Artwork art;
        art.appId = key.first;
        art.kind = kind;
        art.path = path;
        fill(art);
        out[key] = std::move(art);
    } else if (!out.contains(key)) {
        Artwork art;
        art.appId = key.first;
        art.kind = kind;
        art.path = path;
        fill(art);
        out.emplace(std::move(key), std::move(art));
    }
}

void collectLibraryCache(const std::filesystem::path& cacheDir,
                         std::map<Key, Artwork>& out)
{
    std::error_code ec;
    if (!std::filesystem::exists(cacheDir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory(ec)) {
            // Modern: <appid>/<kind>.<ext>
            if (!std::all_of(name.begin(), name.end(), ::isdigit)) {
                continue;
            }
            for (const auto& file :
                 std::filesystem::directory_iterator(entry.path(), ec)) {
                if (!file.is_regular_file(ec) || !hasArtworkExtension(file.path())) {
                    continue;
                }
                if (auto kind = kindFromBareStem(file.path().stem().string())) {
                    put(out, name, *kind, file.path(), false);
                }
            }
        } else if (entry.is_regular_file(ec) && hasArtworkExtension(entry.path())) {
            // Legacy flat: <appid>_<kind>.<ext>
            const std::string stem = entry.path().stem().string();
            std::string_view appId, rest;
            if (!splitLibraryCacheName(stem, appId, rest)) {
                continue;
            }
            if (auto kind = kindFromLibraryCacheSuffix(rest)) {
                put(out, appId, *kind, entry.path(), false);
            }
        }
    }
}

void collectFrom(const std::filesystem::path& dir,
                 const std::function<bool(const std::string&, std::string_view&,
                                          ArtworkKind&)>& parseName,
                 std::map<Key, Artwork>& out,
                 bool customWins)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || !hasArtworkExtension(entry.path())) {
            continue;
        }
        std::string_view appId;
        ArtworkKind kind{};
        const std::string stem = entry.path().stem().string();
        if (!parseName(stem, appId, kind)) {
            continue;
        }
        Key key{std::string(appId), kind};
        if (customWins) {
            Artwork art;
            art.appId = key.first;
            art.kind = kind;
            art.path = entry.path();
            if (auto dims = probeImageSize(art.path)) {
                art.width = dims->first;
                art.height = dims->second;
            }
            out[key] = std::move(art); // override: user art wins
        } else if (!out.contains(key)) {
            Artwork art;
            art.appId = key.first;
            art.kind = kind;
            art.path = entry.path();
            if (auto dims = probeImageSize(art.path)) {
                art.width = dims->first;
                art.height = dims->second;
            }
            out.emplace(std::move(key), std::move(art));
        }
    }
}

} // namespace

std::vector<Artwork> SteamLocalProvider::discover(const DiscoveryContext& ctx)
{
    auto install = locateInstall(ctx.steamRootOverride);
    if (!install) {
        SLC_LOG_WARN("steam discovery failed: {}", install.error().message);
        return {};
    }

    std::map<Key, Artwork> found;

    // Official cache (flat legacy + per-app subdir layouts).
    collectLibraryCache(install->root / "appcache" / "librarycache", found);

    // User-custom art overrides official art per (appId, kind). Scan every
    // userdata/<uid>/config/grid; the most-recent user goes last so their art
    // wins over other accounts'.
    const auto userdata = install->root / "userdata";
    std::error_code ec;
    std::vector<std::filesystem::path> gridDirs;
    for (const auto& entry : std::filesystem::directory_iterator(userdata, ec)) {
        if (entry.is_directory(ec)) {
            gridDirs.push_back(entry.path() / "config" / "grid");
        }
    }
    if (install->mostRecentUserId) {
        const auto recent = userdata / *install->mostRecentUserId / "config" / "grid";
        std::erase(gridDirs, recent);
        gridDirs.push_back(recent);
    }
    for (const auto& gridDir : gridDirs) {
        collectFrom(gridDir, parseCustomGridName, found, /*customWins=*/true);
    }

    std::vector<Artwork> result;
    for (auto& [key, art] : found) {
        if (ctx.stopToken.stop_requested()) {
            break;
        }
        const auto appIt = install->installedApps.find(key.first);
        if (ctx.installedOnly && appIt == install->installedApps.end()) {
            continue; // not installed
        }
        if (ctx.excludeAppIds.contains(key.first)) {
            continue;
        }
        if (!ctx.includeKinds.empty() && !ctx.includeKinds.contains(key.second)) {
            continue;
        }
        if (ctx.minImageWidth > 0 && art.width > 0 && art.width < ctx.minImageWidth) {
            continue;
        }
        const auto infoIt = install->appInfo.find(key.first);
        const AppInfoEntry* info =
            infoIt != install->appInfo.end() ? &infoIt->second : nullptr;
        if (!isGameType(info)) {
            continue; // DLC, tool, soundtrack, ... — games only
        }
        if (appIt != install->installedApps.end()) {
            art.gameName = appIt->second.name;
        } else if (info != nullptr) {
            art.gameName = info->name;
        }
        if (const auto stIt = install->userStats.find(key.first);
            stIt != install->userStats.end()) {
            art.playtimeMin = stIt->second.playtimeMin;
            art.lastPlayed = stIt->second.lastPlayed;
            art.achEarned = stIt->second.achEarned;
            art.achTotal = stIt->second.achTotal;
        }
        result.push_back(std::move(art));
    }

    std::sort(result.begin(), result.end(), [](const Artwork& a, const Artwork& b) {
        if (a.appId != b.appId) {
            return a.appId < b.appId;
        }
        return static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    std::unordered_set<std::string_view> appIds;
    for (const auto& a : result) {
        appIds.insert(a.appId);
    }
    SLC_LOG_INFO("steam-local: {} artworks for {} apps (installedOnly={})",
                 result.size(), appIds.size(), ctx.installedOnly);
    return result;
}

} // namespace slc::content
