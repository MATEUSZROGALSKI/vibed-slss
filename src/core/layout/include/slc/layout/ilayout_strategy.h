#pragma once

#include <cinder/Rect.h>

#include <cstddef>
#include <random>
#include <string_view>
#include <vector>

namespace slc::layout {

/// Doc section 5.4. All strategies must be deterministic for a given rng seed.
class ILayoutStrategy {
  public:
    virtual ~ILayoutStrategy() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::vector<ci::Rectf> compute(const ci::Rectf& bounds,
                                                         std::size_t cellCount,
                                                         std::mt19937& rng) const = 0;
};

} // namespace slc::layout
