#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/**
 * Mosaic grid: a near-uniform grid where seeded cells merge with a
 * neighbour (2x1 or 1x2 span), producing the classic "photo wall" mix of
 * large and small tiles.
 */
class MosaicStrategy final : public ILayoutStrategy {
  public:
    explicit MosaicStrategy(float gutter = 12.0F) : m_gutter(gutter) {}

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "mosaic";
    }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
};

} // namespace slc::layout
