#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/// Regular row/column grid covering `bounds` with a fixed pixel gutter.
class GridStrategy final : public ILayoutStrategy {
  public:
    explicit GridStrategy(float gutter = 12.0F) : m_gutter(gutter) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "grid"; }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
};

} // namespace slc::layout
