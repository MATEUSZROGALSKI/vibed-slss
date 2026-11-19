#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/**
 * Column-major masonry: fixed column width, variable heights. Heights are
 * drawn from a seeded distribution so identical seeds reproduce identical
 * layouts on every platform (doc M3 exit criterion).
 */
class MasonryStrategy final : public ILayoutStrategy {
  public:
    explicit MasonryStrategy(float gutter = 12.0F) : m_gutter(gutter) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "masonry"; }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
};

} // namespace slc::layout
