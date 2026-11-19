#pragma once

#include <cinder/Rect.h>
#include <cinder/Timeline.h>
#include <cinder/Vector.h>
#include <cinder/gl/Texture.h>

#include <memory>

namespace slc::anim {

/// Doc section 5.4. Attach() must be cheap; the animator registers timeline
/// items and returns — the Timeline owns the active animations. The animator
/// retains the ci::Anim state its tweens drive; consumers fold the query
/// accessors (alpha/scale/offset) when compositing a cell.
class ICellAnimator {
  public:
    virtual ~ICellAnimator() = default;
    virtual void attach(ci::Timeline& timeline,
                        const ci::Rectf& cell,
                        std::shared_ptr<ci::gl::Texture2d> tex) = 0;

    /// Current opacity contribution [0,1]; multiply-fold across animators.
    [[nodiscard]] virtual float alpha() const = 0;
    /// Current draw-scale contribution (~1.0); multiply-fold.
    [[nodiscard]] virtual float scale() const = 0;
    /// Current pixel offset contribution; sum-fold.
    [[nodiscard]] virtual ci::vec2 offset() const = 0;
    /// Current rotation contribution in degrees; sum-fold. Default 0 so
    /// existing animators (fade, drift, Ken Burns) stay unchanged.
    [[nodiscard]] virtual float rotation() const { return 0.0F; }
};

} // namespace slc::anim
