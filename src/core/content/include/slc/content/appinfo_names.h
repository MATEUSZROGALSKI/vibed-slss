#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace slc::content {

/// Per-app metadata extracted from the appinfo "common" section.
struct AppInfoEntry {
    std::string name;
    /// Steam store type: "game", "dlc", "tool", "demo", "music", ...
    /// Empty when the record lacks a type field.
    std::string type;
};

/**
 * Reads appid -> {name, type} from Steam's appcache/appinfo.vdf.
 *
 * Supported on-disk layouts:
 *  - v41 ("version 29", magic 0x07564429): record keys are u32 indices into
 *    a string table appended at the end of the file.
 *  - v39/v40 (magics 0x07564427/0x07564428): classic binary VDF with
 *    inline cstring keys.
 *
 * This is a best-effort metadata source: any read/parse failure yields an
 * empty map and callers must keep working with unnamed appids.
 */
[[nodiscard]] std::unordered_map<std::string, AppInfoEntry> readAppInfo(
    const std::filesystem::path& appinfoPath);

} // namespace slc::content
