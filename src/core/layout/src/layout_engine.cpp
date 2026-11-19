#include "slc/layout/layout_engine.h"

#include "slc/layout/grid_strategy.h"
#include "slc/layout/masonry_strategy.h"
#include "slc/layout/spiral_strategy.h"
#include "slc/layout/spotlight_strategy.h"

namespace slc::layout {

LayoutEngine::LayoutEngine()
{
    registerStrategy(std::make_unique<GridStrategy>());
    registerStrategy(std::make_unique<MasonryStrategy>());
    registerStrategy(std::make_unique<SpiralStrategy>());
    registerStrategy(std::make_unique<SpotlightStrategy>());
}

void LayoutEngine::registerStrategy(std::unique_ptr<ILayoutStrategy> strategy)
{
    m_strategies[std::string(strategy->name())] = std::move(strategy);
}

const ILayoutStrategy* LayoutEngine::strategy(std::string_view name) const
{
    const auto it = m_strategies.find(std::string(name));
    return it == m_strategies.end() ? nullptr : it->second.get();
}

std::vector<ci::Rectf> LayoutEngine::compute(std::string_view strategyName,
                                             const ci::Rectf& bounds,
                                             std::size_t cellCount,
                                             std::mt19937& rng) const
{
    const ILayoutStrategy* s = strategy(strategyName);
    if (s == nullptr) {
        s = strategy("grid"); // deterministic fallback
    }
    if (s == nullptr || cellCount == 0) {
        return {};
    }
    return s->compute(bounds, cellCount, rng);
}

} // namespace slc::layout
