#include "slc/anim/overscan.h"

#include <cinder/CinderMath.h>

#include <algorithm>
#include <cmath>

namespace slc::anim {

float cellOverscanScale(const ci::Rectf& drawn, const ci::Rectf& clip,
                        float driftPx, float maxRotDeg)
{
    const float baseW = drawn.getWidth();
    const float baseH = drawn.getHeight();
    if (baseW <= 0.0F || baseH <= 0.0F) {
        return 1.0F;
    }
    const float sn =
        std::sin(ci::toRadians(std::fabs(maxRotDeg)));
    const float cs =
        std::cos(ci::toRadians(std::fabs(maxRotDeg)));
    const float drift = std::max(0.0F, driftPx);
    // Clip-corner extent in the quad's frame, doubled to a full size. The
    // pivot's drift offset rotates in too: its per-axis contribution is
    // bounded by drift·(cos+sin).
    const float driftTerm = 2.0F * drift * (cs + sn);
    const float needW = clip.getWidth() * cs + clip.getHeight() * sn +
                        driftTerm;
    const float needH = clip.getWidth() * sn + clip.getHeight() * cs +
                        driftTerm;
    // ~4% extra: the 0.96 entrance scale and sub-pixel rounding must not
    // re-open the coverage gap the margin exists to close.
    return std::max({1.0F, needW / baseW, needH / baseH}) * 1.05F;
}

} // namespace slc::anim
