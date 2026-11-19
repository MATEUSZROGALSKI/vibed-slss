#include "slc/content/appinfo_names.h"
#include "slc/content/steam_locator.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using slc::content::readAppInfo;

void put(std::vector<std::uint8_t>& buf, const void* data, std::size_t n)
{
    const auto* b = static_cast<const std::uint8_t*>(data);
    buf.insert(buf.end(), b, b + n);
}

template <typename T> void put(std::vector<std::uint8_t>& buf, T v)
{
    put(buf, &v, sizeof(T));
}

void putStr(std::vector<std::uint8_t>& buf, std::string_view s)
{
    put(buf, s.data(), s.size());
    const std::uint8_t zero = 0;
    buf.push_back(zero);
}

// Builds a binary-VDF body:
//   appinfo{ appid:i32, common{ name:str, type:str? } }.
// `type` empty emits no type entry. indexed=true emits u32 keys (v41);
// false emits cstring keys (v40).
std::vector<std::uint8_t> makeRecord(std::uint32_t appId, const std::string& name,
                                     const std::string& type, bool indexed)
{
    const auto key = [&](const char* k, std::uint32_t idx,
                         std::vector<std::uint8_t>& b) {
        if (indexed) {
            put(b, idx);
        } else {
            putStr(b, k);
        }
    };
    std::vector<std::uint8_t> vdf;
    vdf.push_back(0x00);
    key("appinfo", 0, vdf);
    vdf.push_back(0x02);
    key("appid", 1, vdf);
    put(vdf, static_cast<std::int32_t>(appId));
    vdf.push_back(0x00);
    key("common", 2, vdf);
    vdf.push_back(0x01);
    key("name", 3, vdf);
    putStr(vdf, name);
    if (!type.empty()) {
        vdf.push_back(0x01);
        key("type", 4, vdf);
        putStr(vdf, type);
    }
    vdf.push_back(0x08);
    vdf.push_back(0x08);

    std::vector<std::uint8_t> rec;
    put(rec, appId);
    const std::uint32_t size = 60 + static_cast<std::uint32_t>(vdf.size());
    put(rec, size); // bytes after this field
    put(rec, std::uint32_t{2});          // infoState
    put(rec, std::uint32_t{0x6A000000}); // lastUpdated
    const std::uint8_t zeros[52] = {};   // token + sha1 + change + shabin
    put(rec, zeros, sizeof(zeros));
    rec.insert(rec.end(), vdf.begin(), vdf.end());
    return rec;
}

struct File {
    std::filesystem::path path;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& o) noexcept : path(std::move(o.path)) { o.path.clear(); }
    File& operator=(File&& o) noexcept
    {
        std::filesystem::remove(path);
        path = std::move(o.path);
        o.path.clear();
        return *this;
    }
    ~File()
    {
        if (!path.empty()) {
            std::filesystem::remove(path);
        }
    }
};

File writeTmp(const std::vector<std::uint8_t>& buf, const char* suffix)
{
    File f;
    f.path = std::filesystem::temp_directory_path() /
             ("slc_appinfo_test" + std::string(suffix));
    std::ofstream out(f.path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    return f;
}

struct TestApp {
    std::uint32_t id;
    std::string name;
    std::string type = "game";
};

std::vector<std::uint8_t> makeV41(const std::vector<TestApp>& apps)
{
    std::vector<std::uint8_t> f;
    put(f, std::uint32_t{0x07564429});
    put(f, std::uint32_t{1});
    put(f, std::uint64_t{0}); // table offset patched below
    for (const auto& app : apps) {
        const auto rec = makeRecord(app.id, app.name, app.type, true);
        f.insert(f.end(), rec.begin(), rec.end());
    }
    put(f, std::uint32_t{0}); // appid terminator
    const std::uint64_t toff = f.size();
    std::memcpy(f.data() + 8, &toff, 8);
    const char* tbl[] = {"appinfo", "appid", "common", "name", "type"};
    put(f, std::uint32_t{5});
    for (const char* s : tbl) {
        putStr(f, s);
    }
    return f;
}

} // namespace

TEST(AppInfoNames, ReadsV41IndexedKeys)
{
    const auto buf = makeV41({{440, "Team Fortress 2", "game"},
                              {999999, "Obscure Game", "dlc"},
                              {31337, "Typeless App", ""}});
    const auto file = writeTmp(buf, "_v41");
    ASSERT_EQ(std::filesystem::file_size(file.path), buf.size());
    const auto infos = readAppInfo(file.path);
    ASSERT_EQ(infos.size(), 3u);
    EXPECT_EQ(infos.at("440").name, "Team Fortress 2");
    EXPECT_EQ(infos.at("440").type, "game");
    EXPECT_EQ(infos.at("999999").type, "dlc");
    // A missing type field is tolerated: name still resolves.
    EXPECT_EQ(infos.at("31337").name, "Typeless App");
    EXPECT_TRUE(infos.at("31337").type.empty());
}

TEST(AppInfoNames, ReadsV40CStringKeys)
{
    std::vector<std::uint8_t> f;
    put(f, std::uint32_t{0x07564428});
    put(f, std::uint32_t{1});
    const auto rec = makeRecord(570, "Dota 2", "game", false);
    f.insert(f.end(), rec.begin(), rec.end());
    put(f, std::uint32_t{0});
    const auto file = writeTmp(f, "_v40");

    const auto infos = readAppInfo(file.path);
    ASSERT_EQ(infos.size(), 1u);
    EXPECT_EQ(infos.at("570").name, "Dota 2");
    EXPECT_EQ(infos.at("570").type, "game");
}

TEST(AppInfoNames, MissingFileYieldsEmpty)
{
    EXPECT_TRUE(readAppInfo("no/such/appinfo.vdf").empty());
}

TEST(AppInfoNames, BadMagicYieldsEmpty)
{
    const auto file = writeTmp({0xDE, 0xAD, 0xBE, 0xEF, 1, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                               "_bad");
    EXPECT_TRUE(readAppInfo(file.path).empty());
}

TEST(AppInfoNames, TruncatedRecordStopsCleanly)
{
    auto buf = makeV41({{10, "Fine", "game"}});
    buf.resize(buf.size() - 20); // chop the string table tail
    const auto file = writeTmp(buf, "_trunc");
    const auto infos = readAppInfo(file.path);
    // Record itself is intact; the name may resolve or not, but no crash.
    EXPECT_TRUE(infos.empty() || infos.at("10").name == "Fine");
}

TEST(AppInfoNames, LocateInstallPopulatesAppInfo)
{
    const auto root = std::filesystem::path(SLC_FIXTURES_DIR) / "steam-trees" /
                      "basic";
    auto install = slc::content::locateInstall(root);
    ASSERT_TRUE(install.has_value());
    EXPECT_EQ(install->appInfo.at("999").name, "Fixture Hidden Gem");
    EXPECT_EQ(install->appInfo.at("999").type, "game");
    EXPECT_EQ(install->appInfo.at("12345").name, "No Art App");
}
