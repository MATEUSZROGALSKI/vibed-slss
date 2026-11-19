#pragma once

#include <cinder/Vector.h>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace slc::anim {

/// Scene-to-scene transition kinds (doc 5.4 animated transitions). "random"
/// is resolved by the app before storing.
enum class SceneTransition { Crossfade, FadeBlack, Slide, Zoom };

/// Draw transform for one side of a transition: multiply alpha and scale,
/// add offset (px), applied about the viewport centre.
struct TransitionVisual {
    float alpha = 1.0F;
    ci::vec2 offset{0};
    float scale = 1.0F;
};

[[nodiscard]] inline SceneTransition
sceneTransitionFromString(std::string_view name, SceneTransition fallback)
{
    if (name == "crossfade") return SceneTransition::Crossfade;
    if (name == "fadeblack") return SceneTransition::FadeBlack;
    if (name == "slide") return SceneTransition::Slide;
    if (name == "zoom") return SceneTransition::Zoom;
    return fallback;
}

/// Outgoing scene transform at fade progress f in [0,1]. `dir` is the unit
/// slide direction; `view` the viewport size in px.
[[nodiscard]] inline TransitionVisual
transitionOut(SceneTransition kind, float f, const ci::vec2& dir,
              const ci::vec2& view)
{
    f = std::clamp(f, 0.0F, 1.0F);
    TransitionVisual v;
    switch (kind) {
    case SceneTransition::Crossfade:
        v.alpha = 1.0F; // stays fully painted under the incoming scene
        break;
    case SceneTransition::FadeBlack:
        v.alpha = f < 0.5F ? 1.0F - 2.0F * f : 0.0F;
        break;
    case SceneTransition::Slide:
        v.offset = -dir * view * f;
        break;
    case SceneTransition::Zoom:
        v.alpha = 1.0F;
        v.scale = 1.0F + 0.1F * f; // outgoing recedes slightly
        break;
    }
    return v;
}

/// Incoming scene transform at fade progress f in [0,1].
[[nodiscard]] inline TransitionVisual
transitionIn(SceneTransition kind, float f, const ci::vec2& dir,
             const ci::vec2& view)
{
    f = std::clamp(f, 0.0F, 1.0F);
    TransitionVisual v;
    switch (kind) {
    case SceneTransition::Crossfade:
        v.alpha = f;
        break;
    case SceneTransition::FadeBlack:
        v.alpha = f <= 0.5F ? 0.0F : 2.0F * f - 1.0F;
        break;
    case SceneTransition::Slide:
        v.offset = dir * view * (1.0F - f);
        break;
    case SceneTransition::Zoom:
        v.alpha = f;
        v.scale = 1.15F - 0.15F * f;
        break;
    }
    return v;
}

} // namespace slc::anim
