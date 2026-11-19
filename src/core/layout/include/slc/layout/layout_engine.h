#pragma once

#include "slc/layout/ilayout_strategy.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace slc::layout {

/// Registry + dispatch for ILayoutStrategy instances.
class LayoutEngine {
  public:
    LayoutEngine();

    void registerStrategy(std::unique_ptr<ILayoutStrategy> strategy);
    [[nodiscard]] const ILayoutStrategy* strategy(std::string_view name) const;

    /// Falls back to "grid" for unknown names; empty for cellCount == 0.
    [[nodiscard]] std::vector<ci::Rectf> compute(std::string_view strategyName,
                                                 const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const;

  private:
    std::unordered_map<std::string, std::unique_ptr<ILayoutStrategy>> m_strategies;
};

} // namespace slc::layout
