// §9.4 visual regression harness: renders a deterministic seeded collage
// into an offscreen FBO, then either writes it out (--render, used to
// create/refresh goldens) or compares it against a committed golden PNG
// (--compare, run under ctest/CI).
//
// The scene is deliberately rasterizer-robust: flat cell fills with
// deterministic colors plus a soft gradient — no text (font rasterization
// differs per platform) and no artwork decode (fixture-free). Rasterizer
// variance (llvmpipe vs real GPU) is absorbed by a per-pixel tolerance
// and a bounded fraction of differing pixels.
//
//   slc_visual --render  out.png
//   slc_visual --compare tests/visual/golden/collage.png

#include <cinder/app/App.h>
#include <cinder/app/RendererGl.h>
#include <cinder/gl/Fbo.h>
#include <cinder/gl/draw.h>
#include <cinder/gl/gl.h>
#include <cinder/ImageIo.h>

#include "slc/layout/layout_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 450;
constexpr int kTolerance = 8;           // per-channel abs diff allowed
constexpr float kMaxDiffRatio = 0.015F; // ≤1.5% of pixels may differ

// Deterministic flat color per cell index — a cheap LCG hash so the
// palette stays stable across platforms/compilers.
ci::Color cellColor(std::size_t i)
{
    std::uint32_t h = static_cast<std::uint32_t>(i) * 2654435761u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return ci::Color(((h >> 16) & 0xFF) / 255.0F,
                     ((h >> 8) & 0xFF) / 255.0F, (h & 0xFF) / 255.0F);
}

void renderScene(ci::gl::FboRef& fbo)
{
    fbo->bindFramebuffer();
    // FBO binding already sets the viewport to the FBO size.
    ci::gl::setMatricesWindow(fbo->getWidth(), fbo->getHeight(), true);
    ci::gl::clear(ci::Color(0.06F, 0.06F, 0.08F));

    const ci::Rectf bounds(0, 0, kWidth, kHeight);
    slc::layout::LayoutEngine engine;
    std::mt19937 rng(20240107); // fixed seed — the test IS determinism
    const auto cells = engine.compute("masonry", bounds, 10, rng);
    for (std::size_t i = 0; i < cells.size(); ++i) {
        ci::gl::color(cellColor(i));
        ci::gl::drawSolidRect(cells[i]);
    }

    // Deterministic accent shape (arc-free, AA edge exercised).
    ci::gl::color(ci::ColorA(1, 1, 1, 0.35F));
    ci::gl::drawSolidCircle(
        ci::vec2(kWidth * 0.5F, kHeight * 0.5F), 60.0F);
    fbo->unbindFramebuffer();
}

int compareSurfaces(const ci::Surface8u& actual,
                    const ci::Surface8u& golden)
{
    if (actual.getSize() != golden.getSize()) {
        std::fprintf(stderr, "size mismatch %dx%d vs %dx%d\n",
                     actual.getWidth(), actual.getHeight(),
                     golden.getWidth(), golden.getHeight());
        return 2;
    }
    std::size_t diff = 0;
    const auto total = static_cast<std::size_t>(actual.getWidth()) *
                       actual.getHeight();
    auto aIt = actual.getIter();
    auto gIt = golden.getIter();
    while (aIt.line() && gIt.line()) {
        while (aIt.pixel() && gIt.pixel()) {
            if (std::abs(aIt.r() - gIt.r()) > kTolerance ||
                std::abs(aIt.g() - gIt.g()) > kTolerance ||
                std::abs(aIt.b() - gIt.b()) > kTolerance) {
                ++diff;
            }
        }
    }
    const float ratio =
        static_cast<float>(diff) / static_cast<float>(total);
    std::printf("visual diff: %zu/%zu pixels (%.3f%%, allowed %.1f%%)\n",
                diff, total, ratio * 100.0F,
                static_cast<double>(kMaxDiffRatio) * 100.0);
    return ratio <= kMaxDiffRatio ? 0 : 1;
}

} // namespace

class VisualHarness : public ci::app::App {
  public:
    void setup() override
    {
        const auto& args = getCommandLineArgs();
        for (std::size_t i = 1; i + 1 < args.size(); ++i) {
            if (args[i] == "--render") {
                m_out = args[i + 1];
            } else if (args[i] == "--compare") {
                m_golden = args[i + 1];
            }
        }
        m_fbo = ci::gl::Fbo::create(kWidth, kHeight);
    }

    void draw() override
    {
        if (m_done) {
            return;
        }
        m_done = true;
        renderScene(m_fbo);
        const auto surface = m_fbo->readPixels8u(m_fbo->getBounds());
        if (!m_out.empty()) {
            ci::writeImage(m_out, surface);
            std::printf("wrote %s\n", m_out.string().c_str());
            std::exit(0);
        }
        ci::Surface8uRef golden;
        try {
            golden = ci::Surface8u::create(ci::loadImage(m_golden));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "golden load failed: %s\n", e.what());
            std::exit(2);
        }
        std::exit(compareSurfaces(surface, *golden));
    }

  private:
    ci::gl::FboRef m_fbo;
    std::filesystem::path m_out;
    std::filesystem::path m_golden;
    bool m_done = false;
};

CINDER_APP(VisualHarness, ci::app::RendererGl)
