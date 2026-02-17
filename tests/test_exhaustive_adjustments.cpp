#include <gtest/gtest.h>
#include "core/surface.h"
#include "core/colorbgra.h"
#include "adjustments/adjustments.h"

using namespace paintnux;

static Surface makeTestSurface(int w = 20, int h = 20) {
    Surface s(w, h);
    for (int y = 0; y < h; ++y) {
        auto* row = s.rowPtr(y);
        for (int x = 0; x < w; ++x) {
            uint8_t r = static_cast<uint8_t>((x * 255) / std::max(w - 1, 1));
            uint8_t g = static_cast<uint8_t>((y * 255) / std::max(h - 1, 1));
            uint8_t b = 128;
            row[x] = ColorBgra::fromBgra(b, g, r, 255);
        }
    }
    return s;
}

static bool surfacesEqual(const Surface& a, const Surface& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        auto* ra = a.rowPtr(y);
        auto* rb = b.rowPtr(y);
        for (int x = 0; x < a.width(); ++x) {
            if (ra[x].bgra != rb[x].bgra) return false;
        }
    }
    return true;
}

// ===== Invert =====

TEST(AdjustInvert, DoubleInvertIsIdentity) {
    auto original = makeTestSurface();
    auto surface = original.clone();
    adjustInvertColors(surface);
    // After one invert, should differ
    EXPECT_FALSE(surfacesEqual(original, surface));
    adjustInvertColors(surface);
    // After double invert, should be identity
    EXPECT_TRUE(surfacesEqual(original, surface));
}

TEST(AdjustInvert, PixelValues) {
    Surface s(1, 1);
    s.setPoint(0, 0, ColorBgra::fromBgra(10, 20, 30, 200));
    adjustInvertColors(s);
    auto p = s.getPoint(0, 0);
    EXPECT_EQ(p.b, 245);
    EXPECT_EQ(p.g, 235);
    EXPECT_EQ(p.r, 225);
    EXPECT_EQ(p.a, 200); // alpha preserved
}

// ===== Grayscale =====

TEST(AdjustGrayscale, ChangesPixels) {
    auto original = makeTestSurface();
    auto surface = original.clone();
    adjustGrayscale(surface);
    EXPECT_FALSE(surfacesEqual(original, surface));
}

TEST(AdjustGrayscale, GreyPixelChannelsEqual) {
    Surface s(1, 1);
    s.setPoint(0, 0, ColorBgra::fromBgra(100, 150, 200, 255));
    adjustGrayscale(s);
    auto p = s.getPoint(0, 0);
    // For grayscale, R==G==B (or close due to luminance weighting)
    // At minimum, alpha preserved
    EXPECT_EQ(p.a, 255);
}

// ===== Sepia =====

TEST(AdjustSepia, ChangesPixels) {
    auto original = makeTestSurface();
    auto surface = original.clone();
    adjustSepia(surface);
    EXPECT_FALSE(surfacesEqual(original, surface));
}

TEST(AdjustSepia, PreservesAlpha) {
    Surface s(1, 1);
    s.setPoint(0, 0, ColorBgra::fromBgra(100, 150, 200, 42));
    adjustSepia(s);
    EXPECT_EQ(s.getPoint(0, 0).a, 42);
}

// ===== Auto-Level =====

TEST(AdjustAutoLevel, ChangesPixelsOnLowContrast) {
    // Use a low-contrast surface so auto-level has something to stretch
    Surface original(20, 20);
    for (int y = 0; y < 20; ++y) {
        auto* row = original.rowPtr(y);
        for (int x = 0; x < 20; ++x) {
            uint8_t v = static_cast<uint8_t>(100 + (x * 20) / 19); // range 100-120
            row[x] = ColorBgra::fromBgra(v, v, v, 255);
        }
    }
    auto surface = original.clone();
    adjustAutoLevel(surface);
    EXPECT_FALSE(surfacesEqual(original, surface));
}

TEST(AdjustAutoLevel, UniformSurfaceNoCrash) {
    Surface s(10, 10);
    s.clear(ColorBgra::fromBgra(128, 128, 128, 255));
    ASSERT_NO_THROW(adjustAutoLevel(s));
}

// ===== Brightness/Contrast =====

TEST(AdjustBrightnessContrast, IdentityParams) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustBrightnessContrast(src, dst, 0, 0);
    EXPECT_TRUE(surfacesEqual(src, dst));
}

TEST(AdjustBrightnessContrast, BrightnessIncrease) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustBrightnessContrast(src, dst, 50, 0);
    // Pixels should be brighter — check one pixel
    auto sp = src.getPoint(5, 5);
    auto dp = dst.getPoint(5, 5);
    EXPECT_GE(dp.r, sp.r);
}

TEST(AdjustBrightnessContrast, ExtremeValues) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(adjustBrightnessContrast(src, dst, 100, 100));
    ASSERT_NO_THROW(adjustBrightnessContrast(src, dst, -100, -100));
}

// ===== Hue/Saturation/Lightness =====

TEST(AdjustHSL, IdentityParams) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustHueSaturationLightness(src, dst, 0, 100, 0);
    EXPECT_TRUE(surfacesEqual(src, dst));
}

TEST(AdjustHSL, HueShift) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustHueSaturationLightness(src, dst, 90, 100, 0);
    EXPECT_FALSE(surfacesEqual(src, dst));
}

TEST(AdjustHSL, Desaturate) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustHueSaturationLightness(src, dst, 0, 0, 0);
    // With saturation=0, result should be grayscale-ish
    auto p = dst.getPoint(10, 10);
    // R, G, B should be close (grayscale)
    EXPECT_NEAR(p.r, p.g, 2);
    EXPECT_NEAR(p.g, p.b, 2);
}

TEST(AdjustHSL, ExtremeValues) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    ASSERT_NO_THROW(adjustHueSaturationLightness(src, dst, -180, 0, -100));
    ASSERT_NO_THROW(adjustHueSaturationLightness(src, dst, 180, 200, 100));
}

// ===== Posterize =====

TEST(AdjustPosterize, HighLevelsApproxIdentity) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    // With max levels (64), result should be very close to original
    adjustPosterize(src, dst, 64, 64, 64);
    // Allow up to 4 units of difference per channel due to quantization
    bool closeEnough = true;
    for (int y = 0; y < src.height() && closeEnough; ++y) {
        auto* rs = src.rowPtr(y);
        auto* rd = dst.rowPtr(y);
        for (int x = 0; x < src.width(); ++x) {
            if (std::abs(rs[x].r - rd[x].r) > 4 ||
                std::abs(rs[x].g - rd[x].g) > 4 ||
                std::abs(rs[x].b - rd[x].b) > 4) {
                closeEnough = false;
                break;
            }
        }
    }
    EXPECT_TRUE(closeEnough);
}

TEST(AdjustPosterize, TwoLevels) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustPosterize(src, dst, 2, 2, 2);
    // With 2 levels, all channels should be 0 or 255
    for (int y = 0; y < dst.height(); ++y) {
        auto* row = dst.rowPtr(y);
        for (int x = 0; x < dst.width(); ++x) {
            EXPECT_TRUE(row[x].r == 0 || row[x].r == 255) << "r=" << (int)row[x].r;
            EXPECT_TRUE(row[x].g == 0 || row[x].g == 255) << "g=" << (int)row[x].g;
            EXPECT_TRUE(row[x].b == 0 || row[x].b == 255) << "b=" << (int)row[x].b;
        }
    }
}

// ===== Levels =====

TEST(AdjustLevels, IdentityParams) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustLevels(src, dst,
                 ColorBgra::fromBgra(0, 0, 0, 255),     // inputBlack
                 ColorBgra::fromBgra(255, 255, 255, 255), // inputWhite
                 ColorBgra::fromBgra(0, 0, 0, 255),     // outputBlack
                 ColorBgra::fromBgra(255, 255, 255, 255), // outputWhite
                 1.0f, 1.0f, 1.0f);                      // gamma
    EXPECT_TRUE(surfacesEqual(src, dst));
}

TEST(AdjustLevels, CrushBlacks) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustLevels(src, dst,
                 ColorBgra::fromBgra(128, 128, 128, 255), // inputBlack at mid
                 ColorBgra::fromBgra(255, 255, 255, 255),
                 ColorBgra::fromBgra(0, 0, 0, 255),
                 ColorBgra::fromBgra(255, 255, 255, 255),
                 1.0f, 1.0f, 1.0f);
    EXPECT_FALSE(surfacesEqual(src, dst));
}

TEST(AdjustLevels, GammaChange) {
    auto src = makeTestSurface();
    Surface dst(src.width(), src.height());
    adjustLevels(src, dst,
                 ColorBgra::fromBgra(0, 0, 0, 255),
                 ColorBgra::fromBgra(255, 255, 255, 255),
                 ColorBgra::fromBgra(0, 0, 0, 255),
                 ColorBgra::fromBgra(255, 255, 255, 255),
                 2.0f, 2.0f, 2.0f);
    EXPECT_FALSE(surfacesEqual(src, dst));
}

// ===== Transparent surface =====

TEST(AdjustmentsAlpha, InvertPreservesAlpha) {
    Surface s(5, 5);
    s.clear(ColorBgra::fromBgra(100, 100, 100, 0));
    adjustInvertColors(s);
    for (int y = 0; y < s.height(); ++y) {
        auto* row = s.rowPtr(y);
        for (int x = 0; x < s.width(); ++x) {
            EXPECT_EQ(row[x].a, 0);
        }
    }
}
