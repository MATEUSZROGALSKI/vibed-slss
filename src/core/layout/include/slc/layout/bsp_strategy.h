#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/**
 * Recursive binary partition ("treemap collage"): repeatedly splits the
 * largest leaf along a seeded axis and fraction. Produces highly varied,
 * organic layouts — no two seeds look alike.
 */
class BspStrategy final : public ILayoutStrategy {
  public:
    explicit BspStrategy(float gutter = 12.0F) : m_gutter(gutter) {}

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "bsp";
    }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
};

} // namespace slc::layout
