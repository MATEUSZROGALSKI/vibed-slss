// doc §9.5 benchmarks: config + VDF parsing on realistic inputs.
#include "slc/config/app_config.h"
#include "slc/content/artwork.h"
#include "slc/content/vdf.h"

#include <benchmark/benchmark.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::filesystem::path fixtureRoot()
{
    return std::filesystem::path(SLC_FIXTURES_DIR) / "steam-trees" / "basic";
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::filesystem::path tempConfigPath()
{
    static const std::filesystem::path path = [] {
        const auto dir =
            std::filesystem::temp_directory_path() / "slc_bench";
        std::filesystem::create_directories(dir);
        (void)slc::config::saveConfig(dir / "config.json",
                                      slc::config::AppConfig{});
        return dir / "config.json";
    }();
    return path;
}

} // namespace

static void BM_VdfLibraryFolders(benchmark::State& state)
{
    const std::string text = readFile(
        fixtureRoot() / "steamapps" / "libraryfolders.vdf");
    for (auto _ : state) {
        auto node = slc::content::vdf::parse(text);
        benchmark::DoNotOptimize(node);
    }
}

static void BM_VdfAppmanifest(benchmark::State& state)
{
    const std::string text = readFile(
        fixtureRoot() / "steamapps" / "appmanifest_440.acf");
    for (auto _ : state) {
        auto node = slc::content::vdf::parse(text);
        benchmark::DoNotOptimize(node);
    }
}

static void BM_ConfigRoundTrip(benchmark::State& state)
{
    const auto path = tempConfigPath();
    for (auto _ : state) {
        auto cfg = slc::config::loadConfig(path);
        benchmark::DoNotOptimize(cfg);
    }
}

static void BM_PickBestFit(benchmark::State& state)
{
    std::vector<slc::content::Artwork> candidates;
    for (int i = 0; i < 16; ++i) {
        slc::content::Artwork a;
        a.appId = "440";
        a.width = static_cast<std::uint32_t>(300 + i * 60);
        a.height = static_cast<std::uint32_t>(450 + i * 90);
        candidates.push_back(a);
    }
    for (auto _ : state) {
        auto idx =
            slc::content::pickBestFit(candidates, 16.0F / 9.0F, 600.0F);
        benchmark::DoNotOptimize(idx);
    }
}

BENCHMARK(BM_VdfLibraryFolders);
BENCHMARK(BM_VdfAppmanifest);
BENCHMARK(BM_ConfigRoundTrip);
BENCHMARK(BM_PickBestFit);
