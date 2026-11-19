#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/**
 * Spotlight: one dominant hero cell (~60% of the long axis) plus a strip of
 * supporting cells. Landscape bounds put the hero left or right (seeded);
 * portrait stacks it top/bottom. Cell 0 is the hero.
 */
class SpotlightStrategy final : public ILayoutStrategy {
  public:
    explicit SpotlightStrategy(float gutter = 12.0F,
                               float heroFraction = 0.62F)
        : m_gutter(gutter), m_heroFraction(heroFraction)
    {
    }

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "spotlight";
    }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
    float m_heroFraction;
};

} // namespace slc::layout
