#include <gtest/gtest.h>
#include "core/surface.h"
#include "core/colorbgra.h"
#include "effects/effects.h"

using namespace paintnux;

// Helper: create a 20x20 surface with a gradient pattern
static Surface makeTestSurface(int w = 20, int h = 20) {
    Surface s(w, h);
    for (int y = 0; y < h; ++y) {
        auto* row = s.rowPtr(y);
        for (int x = 0; x < w; ++x) {
            uint8_t r = static_cast<uint8_t>((x * 255) / std::max(w - 1, 1));
            uint8_t g = static_cast<uint8_t>((y * 255) / std::max(h - 1, 1));
            uint8_t b = static_cast<uint8_t>(128);
            row[x] = ColorBgra::fromBgra(b, g, r, 255);
        }
    }
    return s;
}

// Helper: check that src and dst differ by at least one pixel
static bool surfacesDiffer(const Surface& a, const Surface& b) {
    if (a.width() != b.width() || a.height() != b.height()) return true;
    for (int y = 0; y < a.height(); ++y) {
        auto* ra = a.rowPtr(y);
        auto* rb = b.rowPtr(y);
        for (int x = 0; x < a.width(); ++x) {
            if (ra[x].bgra != rb[x].bgra) return true;
        }
    }
    return false;
}

// ===== Gaussian Blur =====

TEST(EffectGaussianBlur, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectGaussianBlur(src, dst, 3));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectGaussianBlur, OutputDimensionsMatch) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    effectGaussianBlur(src, dst, 5);
    EXPECT_EQ(dst.width(), src.width());
    EXPECT_EQ(dst.height(), src.height());
}

TEST(EffectGaussianBlur, RadiusZero) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectGaussianBlur(src, dst, 0));
}

TEST(EffectGaussianBlur, OneByOneSurface) {
    Surface src(1, 1);
    src.setPoint(0, 0, ColorBgra::fromBgra(100, 150, 200, 255));
    Surface dst(1, 1);
    ASSERT_NO_THROW(effectGaussianBlur(src, dst, 3));
}

// ===== Motion Blur =====

TEST(EffectMotionBlur, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectMotionBlur(src, dst, 45.0, 10));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectMotionBlur, ZeroDistance) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectMotionBlur(src, dst, 0.0, 0));
}

TEST(EffectMotionBlur, OneByOneSurface) {
    Surface src(1, 1);
    src.setPoint(0, 0, ColorBgra::fromBgra(50, 100, 150, 255));
    Surface dst(1, 1);
    ASSERT_NO_THROW(effectMotionBlur(src, dst, 90.0, 5));
}

// ===== Unfocus =====

TEST(EffectUnfocus, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectUnfocus(src, dst, 4));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectUnfocus, RadiusZero) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectUnfocus(src, dst, 0));
}

// ===== Add Noise =====

TEST(EffectAddNoise, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectAddNoise(src, dst, 50, 100, 100));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectAddNoise, ZeroIntensity) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectAddNoise(src, dst, 0, 0, 100));
}

// ===== Median =====

TEST(EffectMedian, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectMedian(src, dst, 2, 50));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectMedian, RadiusZero) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectMedian(src, dst, 0, 50));
}

TEST(EffectMedian, OneByOneSurface) {
    Surface src(1, 1);
    src.setPoint(0, 0, ColorBgra::fromBgra(100, 100, 100, 255));
    Surface dst(1, 1);
    ASSERT_NO_THROW(effectMedian(src, dst, 3, 50));
}

// ===== Pixelate =====

TEST(EffectPixelate, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectPixelate(src, dst, 4));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectPixelate, CellSizeOne) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectPixelate(src, dst, 1));
}

// ===== Edge Detect =====

TEST(EffectEdgeDetect, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectEdgeDetect(src, dst, 0.0));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectEdgeDetect, DifferentAngles) {
    auto src = makeTestSurface();
    Surface dst1(src.width(), src.height());
    Surface dst2(src.width(), src.height());
    effectEdgeDetect(src, dst1, 0.0);
    effectEdgeDetect(src, dst2, 90.0);
    EXPECT_TRUE(surfacesDiffer(dst1, dst2));
}

// ===== Emboss =====

TEST(EffectEmboss, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectEmboss(src, dst, 0.0));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

// ===== Relief =====

TEST(EffectRelief, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectRelief(src, dst, 0.0));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

// ===== Outline =====

TEST(EffectOutline, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectOutline(src, dst, 50, 3));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectOutline, RadiusZero) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectOutline(src, dst, 50, 0));
}

// ===== Glow =====

TEST(EffectGlow, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectGlow(src, dst, 5, 10, 10));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectGlow, ZeroRadius) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectGlow(src, dst, 0, 0, 0));
}

// ===== Sharpen =====

TEST(EffectSharpen, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectSharpen(src, dst, 50));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectSharpen, ZeroAmount) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectSharpen(src, dst, 0));
}

// ===== Oil Painting =====

TEST(EffectOilPainting, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectOilPainting(src, dst, 4, 8));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectOilPainting, MinBrushSize) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectOilPainting(src, dst, 1, 3));
}

// ===== Pencil Sketch =====

TEST(EffectPencilSketch, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectPencilSketch(src, dst, 3, 10));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

// ===== Ink Sketch =====

TEST(EffectInkSketch, NoCrashAndOutputDiffers) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(effectInkSketch(src, dst, 50, 50));
    EXPECT_TRUE(surfacesDiffer(src, dst));
}

TEST(EffectInkSketch, OneByOneSurface) {
    Surface src(1, 1);
    src.setPoint(0, 0, ColorBgra::fromBgra(200, 100, 50, 255));
    Surface dst(1, 1);
    ASSERT_NO_THROW(effectInkSketch(src, dst, 50, 50));
}

// ===== Alpha Preservation =====

TEST(EffectsAlpha, GaussianBlurPreservesTransparent) {
    Surface src(10, 10);
    src.clear(ColorBgra::fromBgra(0, 0, 0, 0)); // fully transparent
    Surface dst(10, 10);
    effectGaussianBlur(src, dst, 2);
    // All pixels should remain fully transparent
    for (int y = 0; y < dst.height(); ++y) {
        auto* row = dst.rowPtr(y);
        for (int x = 0; x < dst.width(); ++x) {
            EXPECT_EQ(row[x].a, 0) << "at (" << x << ", " << y << ")";
        }
    }
}
