#pragma once

#include "slc/layout/ilayout_strategy.h"

namespace slc::layout {

/**
 * Square-tile spiral: a ceil(sqrt(n))^2 grid of uniform tiles, emitted in
 * spiral order starting from the centre so playlist order flows inward-out
 * (cell 0, the highest-ranked artwork, lands on the visual centre).
 * Start direction and handedness are seeded-deterministic.
 */
class SpiralStrategy final : public ILayoutStrategy {
  public:
    explicit SpiralStrategy(float gutter = 12.0F) : m_gutter(gutter) {}

    [[nodiscard]] std::string_view name() const noexcept override
    {
        return "spiral";
    }

    [[nodiscard]] std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                 std::size_t cellCount,
                                                 std::mt19937& rng) const override;

  private:
    float m_gutter;
};

} // namespace slc::layout
