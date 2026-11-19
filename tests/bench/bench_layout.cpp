// doc §9.5 benchmarks: layout compute must stay well under 1 ms for 12 cells.
#include "slc/layout/layout_engine.h"
#include "slc/layout/rows_strategy.h"

#include <benchmark/benchmark.h>

#include <random>

namespace {

const ci::Rectf kBounds{0.0F, 0.0F, 1920.0F, 1080.0F};

void benchStrategy(benchmark::State& state, const char* name)
{
    slc::layout::LayoutEngine engine;
    engine.registerStrategy(std::make_unique<slc::layout::RowsStrategy>());
    for (auto _ : state) {
        std::mt19937 rng(1234);
        auto cells = engine.compute(name, kBounds, 12, rng);
        benchmark::DoNotOptimize(cells);
    }
}

} // namespace

static void BM_LayoutGrid(benchmark::State& state) { benchStrategy(state, "grid"); }
static void BM_LayoutMasonry(benchmark::State& state) { benchStrategy(state, "masonry"); }
static void BM_LayoutSpiral(benchmark::State& state) { benchStrategy(state, "spiral"); }
static void BM_LayoutSpotlight(benchmark::State& state) { benchStrategy(state, "spotlight"); }
static void BM_LayoutRows(benchmark::State& state) { benchStrategy(state, "rows"); }

BENCHMARK(BM_LayoutGrid);
BENCHMARK(BM_LayoutMasonry);
BENCHMARK(BM_LayoutSpiral);
BENCHMARK(BM_LayoutSpotlight);
BENCHMARK(BM_LayoutRows);
