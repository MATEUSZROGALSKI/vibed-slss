// ScreensaverApp rendering — single/collage scene drawing, letterbox
// pads, cell badges, transition compositing, and the debug overlay.
// Split from screensaver_app.cpp.
#include "slc/app/screensaver_app.h"

#include "slc/anim/overscan.h"
#include "slc/common/env.h"
#include "slc/common/logging.h"
#include "slc/platform/paths.h"

#include <cinder/app/App.h>
#include <cinder/app/Window.h>
#include <cinder/Font.h>
#include <cinder/GeomIo.h>
#include <cinder/gl/draw.h>
#include <cinder/gl/gl.h>
#include <cinder/gl/scoped.h>
#include <cinder/ImageIo.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

#if defined(SLC_WITH_IMGUI)
#include <imgui.h>
#endif

namespace slc::app {

namespace {

// Aspect-mismatch threshold for letterboxing (pad + contain instead of
// cover-crop): 1.5 means at most a third of the art would be cut.
constexpr float kLetterboxAspectMismatch = 1.5F;

// Scale-to-cover destination rect preserving aspect ratio.
ci::Rectf coverRect(const ci::gl::Texture2dRef& tex, const ci::Rectf& dst)
{
    const float texW = static_cast<float>(tex->getWidth());
    const float texH = static_cast<float>(tex->getHeight());
    const float scale = std::max(dst.getWidth() / texW, dst.getHeight() / texH);
    const float w = texW * scale;
    const float h = texH * scale;
    const float x = dst.getX1() + (dst.getWidth() - w) * 0.5F;
    const float y = dst.getY1() + (dst.getHeight() - h) * 0.5F;
    return {x, y, x + w, y + h};
}

// Scale-to-contain destination rect preserving aspect ratio (letterbox fit).
ci::Rectf containRect(const ci::gl::Texture2dRef& tex, const ci::Rectf& dst)
{
    const float texW = static_cast<float>(tex->getWidth());
    const float texH = static_cast<float>(tex->getHeight());
    const float scale = std::min(dst.getWidth() / texW, dst.getHeight() / texH);
    const float w = texW * scale;
    const float h = texH * scale;
    const float x = dst.getX1() + (dst.getWidth() - w) * 0.5F;
    const float y = dst.getY1() + (dst.getHeight() - h) * 0.5F;
    return {x, y, x + w, y + h};
}

// Rect transformed by the Ken Burns sample: zoom scales the base rect about
// the window centre; pan (in [-1,1]) shifts within the overflow margin so
// coverage is preserved at the extremes. `base` is the scene-fit rect —
// cover for full-bleed, contain when padding over a blurred backdrop.
ci::Rectf frameRect(const ci::Rectf& base, const ci::Rectf& bounds,
                    const anim::KenBurnsAnimator* an)
{
    if (an == nullptr) {
        return base;
    }
    const float zoom = an->zoom();
    const float dw = base.getWidth() * zoom;
    const float dh = base.getHeight() * zoom;
    const ci::vec2 ctr = bounds.getCenter();
    const ci::vec2 margin{std::max(0.0F, (dw - bounds.getWidth()) * 0.5F),
                          std::max(0.0F, (dh - bounds.getHeight()) * 0.5F)};
    const ci::vec2 p = an->pan();
    const ci::vec2 c{ctr.x + p.x * margin.x, ctr.y + p.y * margin.y};
    return {c.x - dw * 0.5F, c.y - dh * 0.5F, c.x + dw * 0.5F,
            c.y + dh * 0.5F};
}

// Draw `rect` so its centre lands on `center`, scaled by `scale` and rotated
// by `rotDeg` about that centre, at `alpha`. Rotation/scale need the model
// matrix — the rect is drawn in its own frame after the pivot transform.
void drawTransformed(const ci::gl::Texture2dRef& tex, const ci::Rectf& rect,
                     const ci::vec2& center, float scale, float rotDeg,
                     float alpha,
                     const ci::Color& tint = ci::Color(1, 1, 1))
{
    ci::gl::pushModelMatrix();
    ci::gl::translate(center);
    if (rotDeg != 0.0F) {
        ci::gl::rotate(ci::toRadians(rotDeg));
    }
    ci::gl::scale(scale, scale);
    ci::gl::translate(-rect.getCenter());
    ci::gl::color(tint.r, tint.g, tint.b, alpha);
    ci::gl::draw(tex, rect);
    ci::gl::popModelMatrix();
}

} // namespace

void ScreensaverApp::ensureBadgeFonts()
{
    if (m_badgeFont) {
        return;
    }
    // Default glyph set + separators/ellipsis/trademark marks common in
    // game names (and the badge's own middot/ellipsis).
    const std::string chars =
        ci::gl::TextureFont::defaultChars() +
        "\xc2\xb7\xe2\x80\xa6\xe2\x80\x94\xc2\xae\xe2\x84\xa2";
    for (const char* name : {"Segoe UI", "Helvetica Neue", "DejaVu Sans",
                             "Liberation Sans", "sans-serif"}) {
        try {
            m_badgeFont = ci::gl::TextureFont::create(
                ci::Font(name, 26.0F), {}, chars);
            m_badgeStatsFont = ci::gl::TextureFont::create(
                ci::Font(name, 16.0F), {}, chars);
            m_badgeAchFont = ci::gl::TextureFont::create(
                ci::Font(name, 19.0F), {}, chars);
            break;
        } catch (...) {
            m_badgeFont.reset();
        }
    }
    if (!m_badgeFont) {
        SLC_LOG_WARN("no usable font for game badge; badge disabled");
    }
}


void ScreensaverApp::loadUserShader()
{
    m_userShader.reset();
    m_fbo.reset();
    const auto dir = platform::configDir() / "shaders";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Seed an example on first run so users have a working template that
    // documents the uniform contract.
    const auto example = dir / "example_vignette.frag";
    if (!std::filesystem::exists(example, ec)) {
        static const char* kExample = R"GLSL(
// Steam Library Screensaver user shader.
// Drop any *.frag file in this directory and set rendering.shader to its
// filename in config.json (or pick it in Settings). The scene renders to
// a texture, then your fragment shader post-processes it once per frame.
//
// Available uniforms:
//   ciTex0      sampler2D  the rendered frame
//   uResolution vec2       framebuffer size in pixels
//   uTime       float      seconds since app start
//   vTexCoord   vec2       interpolated UV, 0..1 across the frame
#version 150
uniform sampler2D ciTex0;
uniform vec2 uResolution;
uniform float uTime;
in vec2 vTexCoord;
out vec4 oColor;

void main()
{
    vec4 c = texture(ciTex0, vTexCoord);
    vec2 d = vTexCoord - 0.5;
    float vig = 1.0 - 0.55 * dot(d, d) * 2.0;
    oColor = vec4(c.rgb * vig, c.a);
}
)GLSL";
        if (std::ofstream out(example, std::ios::trunc); out) {
            out << kExample;
        }
    }

    if (m_config.rendering.shader.empty()) {
        return;
    }
    const auto file = dir / m_config.rendering.shader;
    // Block path escape: the config value is a filename, not a path.
    if (file.filename() != std::filesystem::path(m_config.rendering.shader)) {
        SLC_LOG_WARN("shader: ignoring non-filename '{}'",
                     m_config.rendering.shader);
        return;
    }
    std::ifstream in(file);
    if (!in) {
        SLC_LOG_WARN("shader: '{}' not found in {}", file.string(),
                     dir.string());
        return;
    }
    const std::string frag((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());

    // Fixed vertex stage: standard Cinder attributes + vTexCoord for frags.
    static const char* kVert = R"GLSL(
#version 150
uniform mat4 ciModelViewProjection;
in vec4 ciPosition;
in vec4 ciTexCoord0;
out vec2 vTexCoord;
void main()
{
    vTexCoord = ciTexCoord0.xy;
    gl_Position = ciModelViewProjection * ciPosition;
}
)GLSL";
    try {
        m_userShader = ci::gl::GlslProg::create(kVert, frag.c_str());
        SLC_LOG_INFO("shader: '{}' loaded", file.filename().string());
    } catch (const ci::gl::GlslProgCompileExc& e) {
        SLC_LOG_WARN("shader '{}' failed to compile: {}",
                     file.filename().string(), e.what());
    } catch (const std::exception& e) {
        SLC_LOG_WARN("shader '{}' failed: {}", file.filename().string(),
                     e.what());
    }
}

void ScreensaverApp::drawSceneSingle(const std::string& key,
                                     const PlaylistEntry& entry,
                                     const ci::Rectf& bounds,
                                     const anim::KenBurnsAnimator* anim,
                                     const anim::RotateAnimator* spin,
                                     const anim::TransitionVisual& tr,
                                     float badgeAlpha)
{
    auto tex = m_pool->get(key);
    if (!tex) {
        return;
    }
    const float rot = spin ? spin->rotation() : 0.0F;
    const float inflate = spin ? 1.12F : 1.0F;

    // Big aspect mismatch (portrait capsules on wide screens, banners on
    // tall ones) reads badly as a center crop — letterbox the artwork over
    // a dimmed, defocused pad of itself instead (Steam Big Picture look).
    const auto pad = m_pool->getPad(key);
    const float texA = tex->getAspectRatio();
    const float scrA = bounds.getAspectRatio();
    const float mismatch = std::max(texA, scrA) / std::min(texA, scrA);
    if (pad && mismatch > kLetterboxAspectMismatch) {
        const ci::Rectf pr = coverRect(pad, bounds).scaledCentered(1.08F);
        drawTransformed(pad, pr, pr.getCenter() + tr.offset, tr.scale, 0.0F,
                        tr.alpha, ci::Color(0.5F, 0.52F, 0.66F));

        const ci::Rectf r =
            frameRect(containRect(tex, bounds), bounds, anim);
        // Soft drop shadow under the framed image, same pivot transform.
        ci::gl::pushModelMatrix();
        ci::gl::translate(r.getCenter() + tr.offset);
        if (rot != 0.0F) {
            ci::gl::rotate(ci::toRadians(rot));
        }
        ci::gl::scale(tr.scale, tr.scale);
        ci::gl::translate(-r.getCenter() + ci::vec2(0.0F, 9.0F));
        ci::gl::color(0.0F, 0.0F, 0.0F, 0.4F * tr.alpha);
        ci::gl::drawSolidRoundedRect(r.inflated(ci::vec2(4.0F, 4.0F)),
                                     14.0F);
        ci::gl::popModelMatrix();

        drawTransformed(tex, r, r.getCenter() + tr.offset, tr.scale, rot,
                        tr.alpha);
        if (badgeAlpha > 0.0F) {
            drawBadge(entry, r, badgeAlpha);
        }
        return;
    }

    const ci::Rectf r = frameRect(coverRect(tex, bounds), bounds, anim);
    drawTransformed(tex, r, r.getCenter() + tr.offset, tr.scale * inflate,
                    rot, tr.alpha);
    if (badgeAlpha > 0.0F) {
        drawBadge(entry, bounds.inflated(ci::vec2(-24.0F, -24.0F)), badgeAlpha);
    }
}

void ScreensaverApp::drawSceneCollage(
    const std::vector<Cell>& cells, const anim::CollageChoreographer* choreo,
    const anim::TransitionVisual& tr, float badgeAlpha)
{
    // Pass 1: drop shadows + dark placeholders so drifted/rotated textures
    // never expose the clear color; pass 2: textures with per-cell
    // choreography folded with the scene-transition transform, each topped
    // by a thin border that defines the cell edge.
    ci::gl::color(0.0F, 0.0F, 0.0F, 0.38F * tr.alpha);
    for (const auto& cell : cells) {
        ci::gl::drawSolidRoundedRect(cell.rect + tr.offset +
                                         ci::vec2(0.0F, 7.0F),
                                     9.0F);
    }
    ci::gl::color(0.04F, 0.05F, 0.09F);
    for (const auto& cell : cells) {
        const ci::Rectf r = cell.rect + tr.offset;
        ci::gl::drawSolidRect(r);
    }
    const float pxScale = getWindow()->getContentScale();
    const float winH =
        static_cast<float>(getWindowHeight()) * pxScale;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto& cell = cells[i];
        const anim::CellVisual v =
            choreo ? choreo->cellVisual(i) : anim::CellVisual{};
        auto tex = m_pool->get(cell.entry.key);
        const float alpha = v.alpha * tr.alpha;
        if (!tex || alpha <= 0.001F) {
            continue;
        }
        // Inflate the cover rect so drift + rotation never pull an edge
        // inside the cell. The margin is per-axis: an elongated cell's
        // short side must also absorb the rotation lift its long side
        // generates, which a single longest-side margin misses — the
        // exposed underlay band was the flickering "border" at seams.
        const ci::Rectf base = coverRect(tex, cell.rect);
        const float inflate =
            choreo ? anim::cellOverscanScale(
                         base, cell.rect,
                         static_cast<float>(
                             m_config.animation.collage.driftPx),
                         anim::CollageChoreographer::kMaxRotationDeg)
                   : 1.0F;
        const ci::vec2 c =
            cell.rect.getCenter() + v.offset + tr.offset;
        // Extreme aspect mismatch would cover-crop most of the art — a
        // 3:1 hero in a square cell loses two thirds of its width. Give it
        // the single-scene treatment instead: dimmed, defocused copy of
        // itself filling the cell, the art contained on top.
        const auto pad = m_pool->getPad(cell.entry.key);
        const float texA = tex->getAspectRatio();
        const float cellA = cell.rect.getAspectRatio();
        const float mismatch =
            std::max(texA, cellA) / std::min(texA, cellA);
        const bool letterbox =
            pad && mismatch > kLetterboxAspectMismatch;
        // Scissor the artwork to its cell: motion reads as pan/rotate/zoom
        // inside a fixed window and neighbours never overlap. The clip
        // follows the scene transition (offset + scale about cell centre).
        {
            ci::Rectf clipRect = cell.rect + tr.offset;
            clipRect = clipRect.scaledCentered(tr.scale);
            const auto x0 =
                static_cast<int>(std::floor(clipRect.x1 * pxScale));
            const auto x1 =
                static_cast<int>(std::ceil(clipRect.x2 * pxScale));
            const auto y0 =
                static_cast<int>(std::floor(winH - clipRect.y2 * pxScale));
            const auto y1 =
                static_cast<int>(std::ceil(winH - clipRect.y1 * pxScale));
            const ci::gl::ScopedScissor clip(x0, y0,
                                             std::max(0, x1 - x0),
                                             std::max(0, y1 - y0));
            if (letterbox) {
                // The pad is the coverage quad here, so it gets the
                // overscan margin; the contained art rides the same
                // motion on top and needs none.
                const ci::Rectf padBase = coverRect(pad, cell.rect);
                const float padInflate =
                    choreo ? anim::cellOverscanScale(
                                 padBase, cell.rect,
                                 static_cast<float>(
                                     m_config.animation.collage.driftPx),
                                 anim::CollageChoreographer::
                                     kMaxRotationDeg)
                           : 1.0F;
                drawTransformed(pad, padBase, c,
                                v.scale * padInflate * tr.scale,
                                v.rotationDeg, alpha,
                                ci::Color(0.5F, 0.52F, 0.66F));
                const ci::Rectf art = containRect(tex, cell.rect);
                // Soft drop shadow under the framed art.
                ci::gl::pushModelMatrix();
                ci::gl::translate(c);
                if (v.rotationDeg != 0.0F) {
                    ci::gl::rotate(ci::toRadians(v.rotationDeg));
                }
                ci::gl::scale(v.scale * tr.scale, v.scale * tr.scale);
                ci::gl::translate(-art.getCenter() +
                                  ci::vec2(0.0F, 6.0F));
                ci::gl::color(0.0F, 0.0F, 0.0F, 0.35F * alpha);
                ci::gl::drawSolidRoundedRect(
                    art.inflated(ci::vec2(3.0F, 3.0F)), 10.0F);
                ci::gl::popModelMatrix();
                drawTransformed(tex, art, c, v.scale * tr.scale,
                                v.rotationDeg, alpha);
            } else {
                drawTransformed(tex, base, c,
                                v.scale * inflate * tr.scale,
                                v.rotationDeg, alpha);
            }
        }
        if (badgeAlpha > 0.0F) {
            drawCellBadge(cell, badgeAlpha);
        }
    }
}


namespace {

// "3762h" / "9.5h" / "45m" from a minute count.
std::string badgePlaytime(std::uint64_t minutes)
{
    if (minutes >= 600) {
        return std::to_string(minutes / 60) + "h";
    }
    if (minutes >= 60) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fh",
                      static_cast<double>(minutes) / 60.0);
        return buf;
    }
    return std::to_string(minutes) + "m";
}

// "Nov 2024" from a unix timestamp; empty for implausible values.
std::string badgeDate(std::uint64_t lastPlayed)
{
    constexpr std::uint64_t kYear2000 = 946684800ULL;
    if (lastPlayed < kYear2000) {
        return {};
    }
    const std::time_t t = static_cast<std::time_t>(lastPlayed);
    // Single-threaded draw path — std::localtime keeps this portable.
    const std::tm* tmp = std::localtime(&t);
    std::tm tm = tmp ? *tmp : std::tm{};
    char buf[16];
    std::strftime(buf, sizeof(buf), "%b %Y", &tm);
    return buf;
}

} // namespace

namespace {

// Badge card design space: every badge is the same card; cells scale the
// whole card down only when the cell can't fit it at 100%. Rows are fixed
// (title, playtime, hltb, completion); optional rows are skipped and the
// card's bottom edge moves up — row positions are identical.
constexpr float kBadgeCardW = 400.0F;
constexpr float kBadgePad = 16.0F;
constexpr float kBadgeTitleY = 42.0F;    // title baseline
constexpr float kBadgeFirstRowY = 70.0F; // playtime baseline
constexpr float kBadgeRowStep = 28.0F;   // per extra row
constexpr float kBadgeBottomPad = 14.0F;

float badgeCardH(bool hasHltb, bool hasCompletion)
{
    const int extraRows = (hasHltb ? 1 : 0) + (hasCompletion ? 1 : 0);
    return kBadgeFirstRowY + kBadgeRowStep * extraRows + kBadgeBottomPad;
}

std::string fitBadgeLine(std::string text,
                         const ci::gl::TextureFontRef& font, float maxW)
{
    const auto opts = ci::gl::TextureFont::DrawOptions();
    if (font->measureString(text, opts).x <= maxW) {
        return text;
    }
    while (text.size() > 1) {
        text.pop_back();
        if (font->measureString(text + "…", opts).x <= maxW) {
            return text + "…";
        }
    }
    return text;
}

} // namespace

void ScreensaverApp::drawBadge(const PlaylistEntry& entry,
                               const ci::Rectf& anchor, float alpha)
{
    if (!m_badgeFont || entry.gameName.empty()) {
        return;
    }
    const float cardH = badgeCardH(entry.hltbMin > 0, entry.achTotal > 0);
    ci::Rectf card(0.0F, 0.0F, kBadgeCardW, cardH);
    // Float the card just above the anchor's bottom edge, centred; the safe
    // band keeps it clear of the OS taskbar/dock on full-bleed scenes.
    card.offsetCenterTo(anchor.getCenter() +
                        ci::vec2(0.0F, anchor.getHeight() * 0.5F -
                                           cardH * 0.5F - 28.0F));
    const ci::Rectf win(getWindowBounds());
    const float safeY2 = win.y2 - std::min(56.0F, win.getHeight() * 0.05F);
    const ci::vec2 shift{
        std::clamp(card.x1, win.x1 + 8.0F, win.x2 - kBadgeCardW - 8.0F) -
            card.x1,
        std::clamp(card.y1, win.y1 + 8.0F, safeY2 - cardH) - card.y1};
    ci::gl::ScopedModelMatrix mtx;
    ci::gl::translate(card.x1 + shift.x, card.y1 + shift.y);
    drawBadgeCard(entry, alpha);
}

void ScreensaverApp::drawCellBadge(const Cell& cell, float alpha)
{
    // Cells that can't fit the card at >= half size get no badge — below
    // that the text is unreadable anyway.
    const float scale =
        std::min(1.0F, (cell.rect.getWidth() - 16.0F) / kBadgeCardW);
    const float cardH = badgeCardH(cell.entry.hltbMin > 0, cell.entry.achTotal > 0);
    if (!m_badgeFont || cell.entry.gameName.empty() || scale < 0.5F ||
        cell.rect.getHeight() < cardH * scale + 12.0F) {
        return;
    }
    // Bottom-left of the cell, lifted clear of the OS bar when the cell
    // hugs the screen's bottom edge.
    const float margin = 10.0F;
    ci::vec2 pos{cell.rect.x1 + margin,
                 cell.rect.y2 - cardH * scale - margin};
    const float winY2 = static_cast<float>(getWindowBounds().y2);
    const float safeY2 = winY2 - std::min(56.0F, winY2 * 0.05F);
    if (pos.y + cardH * scale > safeY2) {
        pos.y = safeY2 - cardH * scale;
    }
    ci::gl::ScopedModelMatrix mtx;
    ci::gl::translate(pos);
    ci::gl::scale(scale, scale);
    drawBadgeCard(cell.entry, alpha);
}

void ScreensaverApp::drawBadgeCard(const PlaylistEntry& entry, float alpha)
{
    ci::gl::color(0.0F, 0.0F, 0.0F, 0.60F * alpha);
    ci::gl::drawSolidRoundedRect(
        ci::Rectf(0.0F, 0.0F, kBadgeCardW, badgeCardH(entry.hltbMin > 0, entry.achTotal > 0)), 14.0F);
    const auto opts = ci::gl::TextureFont::DrawOptions().pixelSnap(true);
    const float textW = kBadgeCardW - 2.0F * kBadgePad;

    // Row 1: title, faux-bold (double draw at +1px).
    const std::string title = fitBadgeLine(entry.gameName, m_badgeFont,
                                           textW - 1.0F);
    ci::gl::color(0.96F, 0.97F, 1.0F, alpha);
    m_badgeFont->drawString(title,
                            ci::vec2(kBadgePad, kBadgeTitleY), opts);
    m_badgeFont->drawString(title,
                            ci::vec2(kBadgePad + 1.0F, kBadgeTitleY), opts);

    // Row 2: playtime + last played, smaller and grayed out.
    std::string stats;
    if (entry.playtimeMin > 0) {
        stats = badgePlaytime(entry.playtimeMin) + " played";
        if (const std::string d = badgeDate(entry.lastPlayed); !d.empty()) {
            stats += " · last " + d;
        }
    } else {
        stats = "not played yet";
    }
    float rowY = kBadgeFirstRowY;
    ci::gl::color(0.70F, 0.74F, 0.80F, alpha);
    m_badgeStatsFont->drawString(fitBadgeLine(stats, m_badgeStatsFont, textW),
                               ci::vec2(kBadgePad, rowY), opts);

    // Row 3: HowLongToBeat main-story time, same muted treatment.
    if (entry.hltbMin > 0) {
        rowY += kBadgeRowStep;
        m_badgeStatsFont->drawString(
            fitBadgeLine("How long to beat?: ~" +
                             badgePlaytime(entry.hltbMin),
                         m_badgeStatsFont, textW),
            ci::vec2(kBadgePad, rowY), opts);
    }

    // Last row: achievement completion, small but ungrayed.
    if (entry.achTotal > 0) {
        rowY += kBadgeRowStep;
        ci::gl::color(0.90F, 0.93F, 1.0F, alpha);
        m_badgeAchFont->drawString(
            std::to_string(entry.achEarned) + " out of " +
                std::to_string(entry.achTotal) + " achievements",
            ci::vec2(kBadgePad, rowY), opts);
    }
}

void ScreensaverApp::ensureRoundedShader()
{
    if (m_roundedShader) {
        return;
    }
    static const char* kVert = R"GLSL(
#version 150
uniform mat4 ciModelViewProjection;
in vec4 ciPosition;
in vec4 ciTexCoord0;
out vec2 vTexCoord;
void main()
{
    vTexCoord = ciTexCoord0.xy;
    gl_Position = ciModelViewProjection * ciPosition;
}
)GLSL";
    // SDF rounded-rect mask over a full-texture draw: vTexCoord runs
    // 0..1 across the texture, so scale it to rect px and evaluate the
    // rounded-box distance for a 1px-antialiased edge.
    static const char* kFrag = R"GLSL(
#version 150
uniform sampler2D ciTex0;
uniform vec2 uSize;
uniform float uRadius;
uniform vec4 uColor;
in vec2 vTexCoord;
out vec4 oColor;
void main()
{
    vec4 c = texture(ciTex0, vTexCoord) * uColor;
    vec2 p = vTexCoord * uSize;
    vec2 d = abs(p - uSize * 0.5) - uSize * 0.5 + uRadius;
    float dist = length(max(d, vec2(0.0))) +
                 min(max(d.x, d.y), 0.0) - uRadius;
    oColor = vec4(c.rgb, c.a * (1.0 - smoothstep(-1.0, 1.0, dist)));
}
)GLSL";
    try {
        m_roundedShader = ci::gl::GlslProg::create(kVert, kFrag);
        m_roundedShader->uniform("ciTex0", 0);
    } catch (const std::exception& e) {
        SLC_LOG_WARN("rounded-cell shader failed to compile: {}", e.what());
        m_roundedShader.reset();
    }
}

void ScreensaverApp::drawRounded(const ci::gl::Texture2dRef& tex,
                                 const ci::Rectf& dst, float alpha)
{
    if (!tex || alpha <= 0.001F || dst.getWidth() <= 0.0F ||
        dst.getHeight() <= 0.0F) {
        return;
    }
    ensureRoundedShader();
    // A Batch is required for the custom shader: gl::draw() always binds
    // the stock shader, so ScopedGlslProg + gl::draw() would silently
    // render a plain rect tinted by the ambient color (that stale color
    // is what painted the hero art black under the shadow's dark tint).
    if (m_roundedShader && !m_roundedBatch) {
        try {
            m_roundedBatch = ci::gl::Batch::create(
                ci::geom::Rect(ci::Rectf(0.0F, 0.0F, 1.0F, 1.0F))
                    .texCoords(ci::vec2(0.0F, 1.0F), ci::vec2(1.0F, 1.0F),
                               ci::vec2(1.0F, 0.0F), ci::vec2(0.0F, 0.0F)),
                m_roundedShader);
        } catch (const std::exception& e) {
            SLC_LOG_WARN("rounded-cell batch failed: {}", e.what());
            m_roundedShader.reset();
        }
    }
    if (m_roundedBatch) {
        m_roundedShader->uniform(
            "uSize", ci::vec2(dst.getWidth(), dst.getHeight()));
        m_roundedShader->uniform("uRadius", 9.0F);
        m_roundedShader->uniform(
            "uColor", ci::ColorA(1.0F, 1.0F, 1.0F, alpha));
        const ci::gl::ScopedTextureBind texScope(tex, 0);
        const ci::gl::ScopedModelMatrix mtx;
        ci::gl::translate(dst.x1, dst.y1);
        ci::gl::scale(dst.getWidth(), dst.getHeight());
        m_roundedBatch->draw();
        return;
    }
    // Fallback: plain draw — must reset the ambient color or the texture
    // inherits whatever tint the last draw left (e.g. the shadow's dark).
    ci::gl::color(1.0F, 1.0F, 1.0F, alpha);
    ci::gl::draw(tex, dst);
}

void ScreensaverApp::drawMatrixColumns(
    const MatrixScene& scene, const anim::MatrixChoreographer* choreo,
    const anim::TransitionVisual& tr, float dim)
{
    const ci::Rectf win(getWindowBounds());
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    const float winH = win.getHeight() * pxScale;
    const float heroT = choreo ? choreo->heroT() : 0.0F;
    const auto& cells = scene.cells;

    // Column draw: each cell's stack position wraps through
    // [-pitch, stackH - pitch) — with stackH >= H + pitch the window is
    // always covered. Scissored to the column strip like collage cells.
    for (const auto& cell : cells) {
        if (cell.entry.key.empty() ||
            cell.column >= scene.columns.size()) {
            continue;
        }
        const auto& col = scene.columns[cell.column];
        float y = std::fmod(cell.y + col.offset, col.stackH);
        if (y < 0.0F) {
            y += col.stackH;
        }
        const ci::Rectf dst(col.x1, y - scene.pitch, col.x2,
                            y - scene.pitch + cell.h);
        if (dst.y2 < win.y1 || dst.y1 > win.y2) {
            continue;
        }
        // The hero's slot stays empty through the whole cycle — the
        // card visibly docks back into it on zoom-out. Only after an
        // instant swap does the index belong to a NEW cell that should
        // draw normally under the dissolving art.
        const bool swappedZoomOut =
            m_heroSwapActive && (&scene == &m_matrix) && choreo &&
            choreo->phase() ==
                anim::MatrixChoreographer::Phase::ZoomOut;
        const bool isHero =
            choreo && heroT > 0.01F && !swappedZoomOut &&
            static_cast<std::size_t>(&cell - cells.data()) ==
                choreo->heroCell();
        if (isHero) {
            continue; // drawn separately on top
        }
        const auto tex = m_pool->get(cell.entry.key);
        if (!tex) {
            continue;
        }
        ci::Rectf clipRect(col.x1, win.y1, col.x2, win.y2);
        clipRect += tr.offset;
        clipRect = clipRect.scaledCentered(tr.scale);
        const auto x0 =
            static_cast<int>(std::floor(clipRect.x1 * pxScale));
        const auto x1 =
            static_cast<int>(std::ceil(clipRect.x2 * pxScale));
        const auto y0 =
            static_cast<int>(std::floor(winH - clipRect.y2 * pxScale));
        const auto y1 =
            static_cast<int>(std::ceil(winH - clipRect.y1 * pxScale));
        const ci::gl::ScopedScissor clip(x0, y0,
                                         std::max(0, x1 - x0),
                                         std::max(0, y1 - y0));
        ci::Rectf dstT = dst + tr.offset;
        dstT = dstT.scaledCentered(tr.scale);
        drawRounded(tex, dstT, tr.alpha * dim);
    }
}

bool ScreensaverApp::ensureMatrixFbos()
{
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    const int pw = static_cast<int>(getWindowWidth() * pxScale);
    const int ph = static_cast<int>(getWindowHeight() * pxScale);
    if (pw <= 0 || ph <= 0) {
        return false;
    }
    if (!m_fboMatrixSharp || m_fboMatrixSharp->getWidth() != pw ||
        m_fboMatrixSharp->getHeight() != ph) {
        m_fboMatrixSharp = ci::gl::Fbo::create(pw, ph);
    }
    const int qw = std::max(1, pw / 4);
    const int qh = std::max(1, ph / 4);
    if (!m_fboMatrixBlurA || m_fboMatrixBlurA->getWidth() != qw ||
        m_fboMatrixBlurA->getHeight() != qh) {
        m_fboMatrixBlurA = ci::gl::Fbo::create(qw, qh);
    }
    if (!m_fboMatrixBlurB || m_fboMatrixBlurB->getWidth() != qw ||
        m_fboMatrixBlurB->getHeight() != qh) {
        m_fboMatrixBlurB = ci::gl::Fbo::create(qw, qh);
    }
    return m_fboMatrixSharp && m_fboMatrixBlurA && m_fboMatrixBlurB;
}

void ScreensaverApp::ensureBlurShader()
{
    if (m_blurShader) {
        return;
    }
    static const char* kVert = R"GLSL(
#version 150
uniform mat4 ciModelViewProjection;
in vec4 ciPosition;
in vec4 ciTexCoord0;
out vec2 vTexCoord;
void main()
{
    vTexCoord = ciTexCoord0.xy;
    gl_Position = ciModelViewProjection * ciPosition;
}
)GLSL";
    // 9-tap separable gaussian (sigma ~4 dest-px, linear-sampler
    // offsets). uTexel = 1/destSize so a downsampled pass strides the
    // kernel across the higher-res source automatically.
    static const char* kFrag = R"GLSL(
#version 150
uniform sampler2D ciTex0;
uniform vec2 uTexel;
uniform vec2 uDir;
in vec2 vTexCoord;
out vec4 oColor;
void main()
{
    vec2 d = uTexel * uDir;
    vec3 acc = texture(ciTex0, vTexCoord).rgb * 0.2270270270;
    acc += texture(ciTex0, vTexCoord + d * 1.3846153846).rgb * 0.3162162162;
    acc += texture(ciTex0, vTexCoord - d * 1.3846153846).rgb * 0.3162162162;
    acc += texture(ciTex0, vTexCoord + d * 3.2307692308).rgb * 0.0702702703;
    acc += texture(ciTex0, vTexCoord - d * 3.2307692308).rgb * 0.0702702703;
    oColor = vec4(acc, 1.0);
}
)GLSL";
    try {
        m_blurShader = ci::gl::GlslProg::create(kVert, kFrag);
        m_blurShader->uniform("ciTex0", 0);
        m_blurBatch = ci::gl::Batch::create(
            ci::geom::Rect(ci::Rectf(0.0F, 0.0F, 1.0F, 1.0F))
                .texCoords(ci::vec2(0.0F, 1.0F), ci::vec2(1.0F, 1.0F),
                           ci::vec2(1.0F, 0.0F), ci::vec2(0.0F, 0.0F)),
            m_blurShader);
    } catch (const std::exception& e) {
        SLC_LOG_WARN("matrix blur shader failed to compile: {}",
                     e.what());
        m_blurShader.reset();
        m_blurBatch.reset();
    }
}

void ScreensaverApp::matrixBlurPass(const ci::gl::Texture2dRef& src,
                                    const ci::gl::FboRef& dst,
                                    const ci::vec2& dir)
{
    if (!src || !dst) {
        return;
    }
    ensureBlurShader();
    if (!m_blurBatch) {
        // Shader unavailable: naive downscale still blurs acceptably.
        renderIntoFbo(dst, [&] {
            ci::gl::draw(src, ci::Rectf(0, 0,
                                        static_cast<float>(
                                            dst->getWidth()),
                                        static_cast<float>(
                                            dst->getHeight())));
        });
        return;
    }
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    m_blurShader->uniform(
        "uTexel", ci::vec2(1.0F / dst->getWidth(),
                           1.0F / dst->getHeight()));
    m_blurShader->uniform("uDir", dir);
    renderIntoFbo(dst, [&] {
        const ci::gl::ScopedTextureBind texScope(src, 0);
        const ci::gl::ScopedModelMatrix mtx;
        ci::gl::scale(dst->getWidth() / pxScale,
                      dst->getHeight() / pxScale);
        m_blurBatch->draw();
    });
}

void ScreensaverApp::drawSceneMatrix(const MatrixScene& scene,
                                     const anim::MatrixChoreographer* choreo,
                                     const anim::TransitionVisual& tr,
                                     float /*badgeAlpha*/)
{
    const ci::Rectf win(getWindowBounds());
    const float heroT = choreo ? choreo->heroT() : 0.0F;
    const auto& cells = scene.cells;
    const bool heroActive =
        choreo && heroT > 0.001F && choreo->heroCell() < cells.size();

    if (!heroActive) {
        drawMatrixColumns(scene, choreo, tr, 1.0F);
        return;
    }

    // Hero moment: the surrounding matrix blurs out — columns render
    // once into the sharp FBO, then two H+V separable-gaussian passes at
    // quarter res produce a real blur (cross-faded in by heroT), plus a
    // readability dim. Fallback without FBO support: dim the sharp
    // columns and use the pad backdrop.
    const bool fboOk = ensureMatrixFbos();
    if (fboOk) {
        const anim::TransitionVisual identity;
        renderIntoFbo(m_fboMatrixSharp, [&] {
            drawMatrixColumns(scene, choreo, identity, 1.0F);
        });
        const auto sharpTex = m_fboMatrixSharp->getColorTexture();
        matrixBlurPass(sharpTex, m_fboMatrixBlurA, ci::vec2(1, 0));
        matrixBlurPass(m_fboMatrixBlurA->getColorTexture(),
                       m_fboMatrixBlurB, ci::vec2(0, 1));
        matrixBlurPass(m_fboMatrixBlurB->getColorTexture(),
                       m_fboMatrixBlurA, ci::vec2(1, 0));
        matrixBlurPass(m_fboMatrixBlurA->getColorTexture(),
                       m_fboMatrixBlurB, ci::vec2(0, 1));
        drawComposite(sharpTex, win, tr);
        anim::TransitionVisual blurTr = tr;
        blurTr.alpha *= heroT;
        drawComposite(m_fboMatrixBlurB->getColorTexture(), win, blurTr);
        ci::gl::color(0.0F, 0.0F, 0.0F, 0.42F * heroT * tr.alpha);
        ci::gl::drawSolidRect(win);
    } else {
        drawMatrixColumns(scene, choreo, tr, 1.0F - 0.55F * heroT);
    }

    // After a matrix→matrix swap the hero's old cell is gone from the
    // scene — draw the frozen art/rect held in the swap state instead.
    const bool swapped = m_heroSwapActive && (&scene == &m_matrix);
    const PlaylistEntry* heroEntry;
    ci::gl::Texture2dRef tex;
    ci::Rectf from;
    if (swapped) {
        heroEntry = &m_heroSwapEntry;
        tex = m_heroSwapTex;
        from = m_heroFrom;
    } else {
        const auto& hero = cells[choreo->heroCell()];
        const auto& hcol = scene.columns[hero.column];
        heroEntry = &hero.entry;
        tex = m_pool->get(hero.entry.key);
        if (tex) {
            float hy = std::fmod(hero.y + hcol.offset, hcol.stackH);
            if (hy < 0.0F) {
                hy += hcol.stackH;
            }
            from = ci::Rectf(hcol.x1, hy - scene.pitch, hcol.x2,
                             hy - scene.pitch + hero.h);
        }
    }
    if (!tex) {
        if (!m_heroLogged && heroT > 0.5F) {
            m_heroLogged = true;
            SLC_LOG_WARN("matrix hero draw: tex LOST key='{}' heroT={:.2f}",
                         heroEntry->key, heroT);
        }
        return;
    }

    if (!fboOk) {
        if (const auto pad = m_pool->getPad(heroEntry->key)) {
            const ci::Rectf pr = coverRect(pad, win).scaledCentered(1.08F);
            drawTransformed(pad, pr, pr.getCenter() + tr.offset, tr.scale,
                            0.0F, tr.alpha * heroT,
                            ci::Color(0.5F, 0.52F, 0.66F));
        } else {
            ci::gl::color(0.0F, 0.0F, 0.0F, 0.55F * heroT * tr.alpha);
            ci::gl::drawSolidRect(win);
        }
    }

    const float W = win.getWidth();
    const float H = win.getHeight();
    const float mx = W * 0.05F;
    ci::Rectf artZone(win.x1 + mx, win.y1 + H * 0.08F,
                      win.x1 + mx + W * 0.56F, win.y2 - H * 0.08F);
    // Stats card rides the right margin; give its space to the art on
    // narrow windows where it would drop below readable scale.
    const float cardScale =
        std::min(1.0F, (W - artZone.getX2() - mx * 2.0F - 24.0F) /
                           kBadgeCardW);
    ensureBadgeFonts();
    const bool showCard = m_config.animation.showBadge &&
                          cardScale >= 0.45F && m_badgeFont &&
                          !heroEntry->gameName.empty();
    if (!showCard) {
        artZone.x2 = win.x2 - mx;
    }
    const ci::Rectf heroDst = containRect(tex, artZone);

    // Eased lerp scrolled-slot -> hero position.
    const ci::Rectf dst(
        from.x1 + (heroDst.x1 - from.x1) * heroT,
        from.y1 + (heroDst.y1 - from.y1) * heroT,
        from.x2 + (heroDst.x2 - from.x2) * heroT,
        from.y2 + (heroDst.y2 - from.y2) * heroT);

    if (!m_heroLogged && heroT > 0.5F) {
        m_heroLogged = true;
        SLC_LOG_INFO(
            "matrix hero draw: key='{}' tex={}x{} dst=({:.0f},{:.0f})-"
            "({:.0f},{:.0f}) heroDst=({:.0f},{:.0f})-({:.0f},{:.0f}) "
            "heroT={:.2f} fbo={} swap={}",
            heroEntry->key, tex->getWidth(), tex->getHeight(),
            dst.x1, dst.y1, dst.x2, dst.y2, heroDst.x1, heroDst.y1,
            heroDst.x2, heroDst.y2, heroT, fboOk, swapped);
    }

    // Drop shadow under the framed art once it lifts out of the column.
    if (heroT > 0.3F) {
        ci::gl::color(0.0F, 0.0F, 0.0F, 0.4F * tr.alpha * heroT);
        ci::gl::drawSolidRoundedRect(
            dst.inflated(ci::vec2(4.0F, 4.0F)) + ci::vec2(0.0F, 9.0F),
            14.0F);
    }
    // During zoom-out the art dissolves into the marquee instead of
    // snapping into a column slot — the sharp new cells unblur under it.
    const float artAlpha =
        choreo->phase() == anim::MatrixChoreographer::Phase::ZoomOut
            ? tr.alpha * std::min(1.0F, heroT * 3.0F)
            : tr.alpha;
    drawRounded(tex, dst, artAlpha);

    if (showCard) {
        // Card visibility is purely hero-driven — not badgeAlpha, which
        // follows the scene clock: the flush arm resets the dwell and
        // would pop the card out mid-hold.
        const float cardA =
            std::clamp((heroT - 0.6F) / 0.4F, 0.0F, 1.0F) * tr.alpha;
        if (cardA > 0.001F) {
            const float cardH =
                badgeCardH(heroEntry->hltbMin > 0,
                           heroEntry->achTotal > 0) *
                cardScale;
            const ci::vec2 pos(win.x2 - mx - kBadgeCardW * cardScale,
                               win.getCenter().y - cardH * 0.5F);
            const ci::gl::ScopedModelMatrix mtx;
            ci::gl::translate(pos);
            ci::gl::scale(cardScale, cardScale);
            drawBadgeCard(*heroEntry, cardA);
        }
    }
}

void ScreensaverApp::ensureTransitionFbos()
{
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    const int pw = static_cast<int>(getWindowWidth() * pxScale);
    const int ph = static_cast<int>(getWindowHeight() * pxScale);
    if (pw <= 0 || ph <= 0) {
        return;
    }
    if (!m_fboPrev || m_fboPrev->getWidth() != pw ||
        m_fboPrev->getHeight() != ph) {
        m_fboPrev = ci::gl::Fbo::create(pw, ph);
    }
    if (!m_fboCur || m_fboCur->getWidth() != pw ||
        m_fboCur->getHeight() != ph) {
        m_fboCur = ci::gl::Fbo::create(pw, ph);
    }
}

void ScreensaverApp::renderIntoFbo(const ci::gl::FboRef& fbo,
                                   const std::function<void()>& drawFn)
{
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    const ci::gl::ScopedFramebuffer fboScope(fbo);
    ci::gl::clear(ci::ColorA(0.0F, 0.0F, 0.0F, 0.0F));
    const ci::gl::ScopedViewport vp(0, 0, fbo->getWidth(),
                                    fbo->getHeight());
    const ci::gl::ScopedMatrices mtx;
    ci::gl::setMatricesWindow(fbo->getWidth(), fbo->getHeight(), true);
    ci::gl::scale(pxScale, pxScale);
    drawFn();
}

void ScreensaverApp::drawComposite(const ci::gl::Texture2dRef& tex,
                                   const ci::Rectf& bounds,
                                   const anim::TransitionVisual& tr)
{
    if (!tex || tr.alpha <= 0.001F) {
        return;
    }
    drawTransformed(tex, bounds, bounds.getCenter() + tr.offset, tr.scale,
                    0.0F, tr.alpha);
}

void ScreensaverApp::drawScene()
{
    const ci::Rectf bounds(getWindowBounds());

    if (m_state == State::Running) {
        const ci::vec2 view(bounds.getWidth(), bounds.getHeight());
        const auto trOut = anim::transitionOut(m_transition, m_fade,
                                               m_slideDir, view);
        const auto trIn = anim::transitionIn(m_transition, m_fade,
                                             m_slideDir, view);
        const bool fading = m_fade < 1.0F;

        // Badge fades in over the first ~0.8s of the still window.
        const double animSec =
            static_cast<double>(m_config.animation.kenBurns.durationMs) /
            1000.0;
        const float badgeAlpha =
            (m_config.animation.showBadge && m_settled && !fading)
                ? std::clamp(
                      static_cast<float>(m_dwellElapsedSec - animSec) /
                          0.8F,
                      0.0F, 1.0F)
                : 0.0F;

        const bool prevOk =
            fading && trOut.alpha > 0.001F &&
            ((m_prevScene == SceneKind::Collage && !m_prevCells.empty()) ||
             (m_prevScene == SceneKind::Matrix &&
              !m_prevMatrix.cells.empty()) ||
             (m_prevScene == SceneKind::Single && !m_prevKey.empty() &&
              m_pool->isResident(m_prevKey)));
        const bool curOk =
            trIn.alpha > 0.001F &&
            ((collage() && !m_cells.empty()) ||
             (m_scene == SceneKind::Matrix && !m_matrix.cells.empty()) ||
             (m_scene == SceneKind::Single && !m_currentKey.empty() &&
              m_pool->isResident(m_currentKey)));

        if (fading && (prevOk || curOk)) {
            // Each scene renders at identity transform into its own FBO:
            // cell scissors never move or re-rasterize, so seams and
            // clips stay pixel-stable, and the transition transform
            // (offset/scale/alpha) applies to whole images only.
            ensureTransitionFbos();
            if (!m_fboPrev || !m_fboCur) {
                return;
            }
            ci::gl::clear(ci::Color::black());
            if (prevOk) {
                renderIntoFbo(m_fboPrev, [&] {
                    const anim::TransitionVisual identity;
                    if (m_prevScene == SceneKind::Collage) {
                        drawSceneCollage(m_prevCells, m_prevChoreo.get(),
                                         identity, 0.0F);
                    } else if (m_prevScene == SceneKind::Matrix) {
                        // Frozen snapshot: the choreo keeps its heroT
                        // but is never stepped, and column offsets are
                        // plain values copied with the scene.
                        drawSceneMatrix(m_prevMatrix,
                                        m_prevMatrixChoreo.get(),
                                        identity, 0.0F);
                    } else {
                        drawSceneSingle(m_prevKey, m_prevEntry, bounds,
                                        m_prevAnim.get(), m_prevSpin.get(),
                                        identity, 0.0F);
                    }
                });
                drawComposite(m_fboPrev->getColorTexture(), bounds, trOut);
            }
            if (curOk) {
                renderIntoFbo(m_fboCur, [&] {
                    const anim::TransitionVisual identity;
                    if (collage()) {
                        drawSceneCollage(m_cells, m_choreo.get(), identity,
                                         badgeAlpha);
                    } else if (m_scene == SceneKind::Matrix) {
                        drawSceneMatrix(m_matrix, m_matrixChoreo.get(),
                                        identity, badgeAlpha);
                    } else {
                        drawSceneSingle(m_currentKey, m_curEntry, bounds,
                                        m_curAnim.get(), m_curSpin.get(),
                                        identity, badgeAlpha);
                    }
                });
                drawComposite(m_fboCur->getColorTexture(), bounds, trIn);
            }
            return;
        }
        if (curOk) {
            // Collages do not cover every pixel (gutters, skipped cells):
            // without a clear, gaps show stale framebuffer contents.
            ci::gl::clear(ci::Color(0.04F, 0.05F, 0.09F));
            const anim::TransitionVisual identity;
            if (collage()) {
                drawSceneCollage(m_cells, m_choreo.get(), identity,
                                 badgeAlpha);
            } else if (m_scene == SceneKind::Matrix) {
                drawSceneMatrix(m_matrix, m_matrixChoreo.get(), identity,
                                badgeAlpha);
            } else {
                drawSceneSingle(m_currentKey, m_curEntry, bounds,
                                m_curAnim.get(), m_curSpin.get(), identity,
                                badgeAlpha);
            }
            return;
        }
        // A live transition owns the frame even when nothing can paint
        // yet — never drop to the Loading gradient mid-transition.
        if (fading) {
            ci::gl::clear(ci::Color::black());
            return;
        }
        if (!collage()) {
            SLC_LOG_WARN("scene unpaintable at fade end — key='{}' "
                         "resident={} failed={} pool={}q — reloading",
                         m_currentKey, m_pool->isResident(m_currentKey),
                         isFailed(m_currentKey), m_pool->queuedCount());
            m_state = State::Loading;
        }
    }

    // Loading / Fallback: subtle animated gradient (doc 5.5 placeholder).
    const float t = static_cast<float>(getElapsedSeconds());
    const float pulse = 0.02F + 0.015F * std::sin(t * 0.8F);
    ci::gl::clear(ci::Color(0.02F, 0.03F, 0.06F + pulse));

    if (m_config.advanced.debugUi && !m_debugUi) {
        drawDebugOverlay(bounds);
    }
}

void ScreensaverApp::drawDebugOverlay(const ci::Rectf& bounds)
{
    if (m_frameTimes.empty()) {
        return;
    }
    std::vector<float> dts(m_frameTimes.begin(), m_frameTimes.end());
    std::sort(dts.begin(), dts.end());
    const float med = dts[dts.size() / 2];
    const float p1 =
        dts[std::min(dts.size() - 1,
                     static_cast<std::size_t>(dts.size() * 0.99F))];
    const float fpsMed = med > 0.0F ? 1.0F / med : 0.0F;
    const float fpsLow = p1 > 0.0F ? 1.0F / p1 : 0.0F;
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "fps %.0f | 1%% low %.0f | resident %zu",
                  static_cast<double>(fpsMed), static_cast<double>(fpsLow),
                  m_pool ? m_pool->residentCount() : 0u);
    ci::gl::drawString(buf,
                       ci::vec2(bounds.getX1() + 10.0F,
                                bounds.getY2() - 26.0F),
                       ci::ColorA(1, 1, 1, 0.8F),
                       ci::Font("Arial", 16.0F));
}

#if defined(SLC_WITH_IMGUI)

void ScreensaverApp::drawDebugUiFrame()
{
    const float dt = m_frameTimes.empty() ? 1.0F / 60.0F
                                          : m_frameTimes.back();
    const float pxScale =
        getWindow() ? getWindow()->getContentScale() : 1.0F;
    m_debugUi->newFrame(
        dt, ci::vec2(getWindowWidth(), getWindowHeight()), pxScale);

    // Frame-time stats over the rolling window (same math as the text
    // overlay above).
    std::vector<float> dts(m_frameTimes.begin(), m_frameTimes.end());
    std::sort(dts.begin(), dts.end());
    const float med = dts.empty() ? 0.0F : dts[dts.size() / 2];
    const float p1 =
        dts.empty()
            ? 0.0F
            : dts[std::min(dts.size() - 1,
                           static_cast<std::size_t>(dts.size() * 0.99F))];

    if (ImGui::Begin("slc debug", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("fps %.0f | 1%% low %.0f | frame %.2f ms",
                    med > 0.0F ? 1.0F / med : 0.0F,
                    p1 > 0.0F ? 1.0F / p1 : 0.0F,
                    static_cast<double>(med * 1000.0F));
        if (!dts.empty()) {
            ImGui::PlotLines("##frametimes", dts.data(),
                             static_cast<int>(dts.size()), 0, nullptr,
                             0.0F, 0.05F, ImVec2(220, 48));
        }
        ImGui::Separator();

        const char* state = m_state == State::Loading  ? "loading"
                            : m_state == State::Running ? "running"
                                                        : "fallback";
        const char* scene = m_scene == SceneKind::Single  ? "single"
                            : m_scene == SceneKind::Collage ? "collage"
                                                            : "matrix";
        ImGui::Text("state=%s scene=%s idx=%zu cells=%zu", state, scene,
                    m_sceneIndex, m_cells.size());
        ImGui::Text("playlist=%zu resident=%zu",
                    playlistSize(),
                    m_pool ? m_pool->residentCount() : 0u);
        {
            std::lock_guard lock(m_decodeMutex);
            ImGui::Text("decodeQ=%zu inflight=%zu zombies=%d",
                        m_decodeQueue.size(), m_inflight.size(),
                        m_decodeZombies.load());
        }
        {
            std::lock_guard lock(m_failedMutex);
            ImGui::Text("failed=%zu", m_failed.size());
        }
        ImGui::Separator();

        // Live toggles: routed through applyConfig like a file reload.
        bool badge = m_config.animation.showBadge;
        if (ImGui::Checkbox("badge", &badge)) {
            auto next = m_config;
            next.animation.showBadge = badge;
            applyConfig(std::move(next));
        }
        bool blur = m_config.rendering.blurBackground;
        if (ImGui::Checkbox("blur background", &blur)) {
            auto next = m_config;
            next.rendering.blurBackground = blur;
            applyConfig(std::move(next));
        }
        bool collageAnim = m_config.animation.collage.enabled;
        if (ImGui::Checkbox("collage anim", &collageAnim)) {
            auto next = m_config;
            next.animation.collage.enabled = collageAnim;
            applyConfig(std::move(next));
        }
    }
    ImGui::End();

    m_debugUi->render();
}

#else

void ScreensaverApp::drawDebugUiFrame() {}

#endif // SLC_WITH_IMGUI

} // namespace slc::app
