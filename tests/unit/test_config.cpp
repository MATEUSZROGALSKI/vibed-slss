#include "slc/config/app_config.h"

#include <gtest/gtest.h>

#include <fstream>
#include <unordered_set>

namespace {

class TempConfig : public ::testing::Test {
  protected:
    std::filesystem::path m_path;

    void SetUp() override
    {
        m_path = std::filesystem::temp_directory_path() /
                 ("slc_cfg_" + std::to_string(::testing::UnitTest::GetInstance()
                                                ->random_seed()) +
                  ".json");
        std::filesystem::remove(m_path);
    }

    void TearDown() override { std::filesystem::remove(m_path); }

    void write(std::string_view text)
    {
        std::ofstream out(m_path);
        out << text;
    }
};

} // namespace

using slc::config::AppConfig;
using slc::config::loadConfig;
using slc::config::saveConfig;

TEST_F(TempConfig, MissingFileYieldsDefaults)
{
    const AppConfig cfg = loadConfig(m_path);
    EXPECT_EQ(cfg.version, 1);
    EXPECT_EQ(cfg.layout.cellCount, 12);
    EXPECT_FLOAT_EQ(cfg.layout.collageProbability, 0.5F);
    EXPECT_EQ(cfg.animation.transition, "random");
    EXPECT_TRUE(cfg.animation.kenBurns.enabled);
    EXPECT_FALSE(cfg.enrichment.steamGridDbEnabled);
    EXPECT_FALSE(cfg.content.steamRoot.has_value());
    EXPECT_FALSE(cfg.layout.seed.has_value());
}

TEST_F(TempConfig, MalformedJsonYieldsDefaults)
{
    write("{ not json !!!");
    const AppConfig cfg = loadConfig(m_path);
    EXPECT_EQ(cfg.layout.cellCount, 12);
}

TEST_F(TempConfig, ReadsSpecExampleDocument)
{
    write(R"({
        "version": 1,
        "content": {
            "steamRoot": "D:/Steam",
            "includeKinds": ["hero"],
            "excludeAppIds": ["1", "2"],
            "minImageWidth": 512
        },
        "layout": { "strategy": "grid", "cellCount": 8, "gutter": 4, "seed": 42 },
        "animation": {
            "kenBurns": { "enabled": false, "durationMs": 5000, "zoomFrom": 2.0, "zoomTo": 1.2 },
            "collage": { "enabled": true, "fadeInMs": 300, "driftPx": 10 },
            "transitionMs": 700
        },
        "rendering": { "targetFps": 30, "vsync": false, "blurBackground": false },
        "enrichment": { "steamGridDb": { "enabled": true, "apiKeyEnvVar": "MY_KEY" } },
        "advanced": { "logLevel": "debug", "cacheDir": "E:/cache", "debugUi": true }
    })");
    const AppConfig cfg = loadConfig(m_path);
    EXPECT_EQ(cfg.content.steamRoot, std::filesystem::path("D:/Steam"));
    EXPECT_EQ(cfg.content.includeKinds, std::unordered_set<std::string>{"hero"});
    EXPECT_EQ(cfg.content.minImageWidth, 512u);
    // Legacy "strategy" key above is accepted but ignored (scene-random).
    EXPECT_EQ(cfg.layout.cellCount, 8);
    ASSERT_TRUE(cfg.layout.seed.has_value());
    EXPECT_EQ(*cfg.layout.seed, 42u);
    EXPECT_FALSE(cfg.animation.kenBurns.enabled);
    EXPECT_EQ(cfg.animation.kenBurns.durationMs, 5000);
    EXPECT_FLOAT_EQ(cfg.animation.kenBurns.zoomFrom, 2.0F);
    EXPECT_EQ(cfg.animation.transitionMs, 700);
    EXPECT_EQ(cfg.rendering.targetFps, 30);
    EXPECT_TRUE(cfg.enrichment.steamGridDbEnabled);
    EXPECT_EQ(cfg.enrichment.apiKeyEnvVar, "MY_KEY");
    EXPECT_EQ(cfg.advanced.logLevel, "debug");
    EXPECT_TRUE(cfg.advanced.debugUi);
}

TEST_F(TempConfig, UnknownKeysAreIgnored)
{
    write(R"({ "version": 1, "bogus": 3, "layout": { "cellCount": 20, "nope": 1 } })");
    const AppConfig cfg = loadConfig(m_path);
    EXPECT_EQ(cfg.layout.cellCount, 20);
}

TEST_F(TempConfig, RandomSeedStringMeansNullopt)
{
    write(R"({ "layout": { "seed": "random" } })");
    EXPECT_FALSE(loadConfig(m_path).layout.seed.has_value());
}

TEST_F(TempConfig, RoundTripPreservesValues)
{
    AppConfig cfg;
    cfg.layout.mode = "matrix";
    cfg.layout.cellCount = 20;
    cfg.layout.seed = 1234;
    cfg.content.excludeAppIds = {"5"};
    cfg.animation.kenBurns.zoomFrom = 1.5F;
    cfg.rendering.blurBackground = false;
    ASSERT_TRUE(saveConfig(m_path, cfg).has_value());

    const AppConfig back = loadConfig(m_path);
    EXPECT_EQ(back.layout.mode, "matrix");
    EXPECT_EQ(back.layout.cellCount, 20);
    ASSERT_TRUE(back.layout.seed.has_value());
    EXPECT_EQ(*back.layout.seed, 1234u);
    EXPECT_TRUE(back.content.excludeAppIds.contains("5"));
    EXPECT_FLOAT_EQ(back.animation.kenBurns.zoomFrom, 1.5F);
    EXPECT_FALSE(back.rendering.blurBackground);
}

TEST_F(TempConfig, NewerVersionStillReads)
{
    write(R"({ "version": 99, "layout": { "cellCount": 7 } })");
    EXPECT_EQ(loadConfig(m_path).layout.cellCount, 7);
}
