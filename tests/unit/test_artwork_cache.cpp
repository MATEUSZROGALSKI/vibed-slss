#include "slc/cache/artwork_cache.h"

#include <gtest/gtest.h>

#include <fstream>
#include <thread>

namespace {

class CacheTest : public ::testing::Test {
  protected:
    std::filesystem::path m_dir;

    void SetUp() override
    {
        m_dir = std::filesystem::temp_directory_path() /
                ("slc_cache_" + std::to_string(
                                    ::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::remove_all(m_dir);
        std::filesystem::create_directories(m_dir);
    }

    void TearDown() override { std::filesystem::remove_all(m_dir); }

    std::filesystem::path writePayload(std::string_view name, std::size_t bytes)
    {
        auto p = m_dir / (std::string(name) + ".src");
        std::ofstream out(p, std::ios::binary);
        out << std::string(bytes, 'x');
        return p;
    }
};

} // namespace

using slc::cache::ArtworkCache;
using slc::cache::CacheLimits;

TEST_F(CacheTest, StoreThenLookup)
{
    ArtworkCache cache(m_dir);
    const auto payload = writePayload("p1", 100);
    ASSERT_TRUE(cache.store("440:hero", payload).has_value());
    auto hit = cache.lookup("440:hero");
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(std::filesystem::exists(*hit));
}

TEST_F(CacheTest, MissReturnsNullopt)
{
    ArtworkCache cache(m_dir);
    EXPECT_FALSE(cache.lookup("nope").has_value());
}

TEST_F(CacheTest, EvictsLeastRecentlyUsedOverBudget)
{
    CacheLimits limits;
    limits.maxBytes = 250;
    limits.maxEntries = 100;
    ArtworkCache cache(m_dir, limits);
    ASSERT_TRUE(cache.store("a", writePayload("a", 100)).has_value());
    ASSERT_TRUE(cache.store("b", writePayload("b", 100)).has_value());
    ASSERT_TRUE(cache.lookup("a").has_value()); // refresh a: b becomes LRU
    ASSERT_TRUE(cache.store("c", writePayload("c", 100)).has_value());
    EXPECT_TRUE(cache.lookup("a").has_value());
    EXPECT_TRUE(cache.lookup("c").has_value());
    EXPECT_FALSE(cache.lookup("b").has_value()); // evicted
}

TEST_F(CacheTest, EvictsOverMaxEntries)
{
    CacheLimits limits;
    limits.maxEntries = 2;
    limits.maxBytes = 1ull << 40;
    ArtworkCache cache(m_dir, limits);
    ASSERT_TRUE(cache.store("a", writePayload("a", 10)).has_value());
    ASSERT_TRUE(cache.store("b", writePayload("b", 10)).has_value());
    ASSERT_TRUE(cache.store("c", writePayload("c", 10)).has_value());
    EXPECT_EQ(cache.entries().size(), 2u);
}

TEST_F(CacheTest, ManifestSurvivesReload)
{
    {
        ArtworkCache cache(m_dir);
        ASSERT_TRUE(cache.store("440:grid", writePayload("g", 50)).has_value());
        ASSERT_TRUE(cache.flush().has_value());
    }
    ArtworkCache reloaded(m_dir);
    EXPECT_TRUE(reloaded.lookup("440:grid").has_value());
}

TEST_F(CacheTest, CorruptManifestRecovers)
{
    {
        std::ofstream bad(m_dir / "manifest.json");
        bad << "{{{{ not json";
    }
    ArtworkCache cache(m_dir); // must not throw
    EXPECT_TRUE(cache.entries().empty());
    ASSERT_TRUE(cache.store("k", writePayload("k", 10)).has_value());
}

TEST_F(CacheTest, RemoveMissingFilesPurgesDeletedPayloads)
{
    ArtworkCache cache(m_dir);
    auto cached = cache.store("x", writePayload("x", 10));
    ASSERT_TRUE(cached.has_value());
    std::filesystem::remove(*cached);
    EXPECT_FALSE(cache.lookup("x").has_value()); // lazily pruned
}
