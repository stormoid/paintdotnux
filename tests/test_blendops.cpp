#include "core/blendops.h"

#include <gtest/gtest.h>

using namespace paintnux;

TEST(NormalBlendOp, OpaqueOverOpaque) {
    NormalBlendOp op;
    auto bg = ColorBgra::fromBgra(0, 0, 0, 255);     // black
    auto fg = ColorBgra::fromBgra(255, 255, 255, 255); // white
    auto result = op.apply(bg, fg);
    // Opaque foreground replaces background
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 255);
    EXPECT_EQ(result.b, 255);
    EXPECT_EQ(result.a, 255);
}

TEST(NormalBlendOp, TransparentOverOpaque) {
    NormalBlendOp op;
    auto bg = ColorBgra::fromBgra(0, 0, 255, 255); // red
    auto fg = ColorBgra::transparent();
    auto result = op.apply(bg, fg);
    EXPECT_EQ(result, bg);
}

TEST(NormalBlendOp, SemiTransparentOverOpaque) {
    NormalBlendOp op;
    auto bg = ColorBgra::fromBgra(0, 0, 0, 255);       // black, opaque
    auto fg = ColorBgra::fromBgra(255, 255, 255, 128);  // white, 50%
    auto result = op.apply(bg, fg);
    // Should be roughly 50% gray
    EXPECT_GE(result.r, 120);
    EXPECT_LE(result.r, 135);
    EXPECT_EQ(result.a, 255);
}

TEST(NormalBlendOp, ScanlineApply) {
    NormalBlendOp op;
    const int len = 4;
    ColorBgra dst[len];
    ColorBgra src[len];
    for (int i = 0; i < len; ++i) {
        dst[i] = ColorBgra::black();
        src[i] = ColorBgra::fromBgra(255, 255, 255, 128);
    }
    op.apply(dst, src, len);
    for (int i = 0; i < len; ++i) {
        EXPECT_GE(dst[i].r, 120);
        EXPECT_LE(dst[i].r, 135);
    }
}

TEST(NormalBlendOp, WithOpacity) {
    NormalBlendOp op;
    auto withOpacity = op.createWithOpacity(128);

    auto bg = ColorBgra::fromBgra(0, 0, 0, 255);       // black
    auto fg = ColorBgra::fromBgra(255, 255, 255, 255);  // white
    auto result = withOpacity->apply(bg, fg);

    // White at 50% opacity over black = ~50% gray
    EXPECT_GE(result.r, 120);
    EXPECT_LE(result.r, 135);
}

TEST(NormalBlendOp, FullOpacityIdentity) {
    NormalBlendOp op;
    auto withFull = op.createWithOpacity(255);

    auto bg = ColorBgra::fromBgra(0, 0, 0, 255);
    auto fg = ColorBgra::fromBgra(255, 255, 255, 255);
    auto result = withFull->apply(bg, fg);
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 255);
    EXPECT_EQ(result.b, 255);
}

TEST(NormalBlendOp, ZeroOpacity) {
    NormalBlendOp op;
    auto withZero = op.createWithOpacity(0);

    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::fromBgra(255, 255, 255, 255);
    auto result = withZero->apply(bg, fg);
    // Should be unchanged from bg
    EXPECT_EQ(result, bg);
}

TEST(NormalBlendOp, TwoTransparentPixels) {
    NormalBlendOp op;
    auto a = ColorBgra::fromBgra(100, 100, 100, 100);
    auto b = ColorBgra::fromBgra(200, 200, 200, 100);
    auto result = op.apply(a, b);
    // Both semi-transparent: combined alpha should be > 100
    EXPECT_GT(result.a, 100);
    EXPECT_LT(result.a, 200);
}

// --- BlendMode enum tests ---

TEST(BlendMode, ModeCount) {
    EXPECT_EQ(blendModeCount(), 14);
}

TEST(BlendMode, NameStrings) {
    EXPECT_STREQ(blendModeName(BlendMode::Normal), "Normal");
    EXPECT_STREQ(blendModeName(BlendMode::Multiply), "Multiply");
    EXPECT_STREQ(blendModeName(BlendMode::Screen), "Screen");
    EXPECT_STREQ(blendModeName(BlendMode::Overlay), "Overlay");
    EXPECT_STREQ(blendModeName(BlendMode::Xor), "Xor");
}

TEST(BlendMode, FactoryCreatesAllModes) {
    for (int i = 0; i < blendModeCount(); ++i) {
        auto op = createBlendOp(static_cast<BlendMode>(i));
        ASSERT_NE(op, nullptr) << "createBlendOp failed for mode " << i;
    }
}

// --- Blend function tests: opaque over opaque ---

TEST(MultiplyBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Multiply);
    auto bg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto fg = ColorBgra::fromBgra(128, 128, 128, 255);
    auto result = op->apply(bg, fg);
    // Multiply(200,128) = 200*128/255 ≈ 100
    EXPECT_NEAR(result.r, 100, 2);
    EXPECT_EQ(result.a, 255);
}

TEST(AdditiveBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Additive);
    auto bg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    // Additive: min(255, 200+100) = 255 (clamped)
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.a, 255);
}

TEST(AdditiveBlend, NoClamping) {
    auto op = createBlendOp(BlendMode::Additive);
    auto bg = ColorBgra::fromBgra(50, 50, 50, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result.r, 150);
}

TEST(ScreenBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Screen);
    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    // Screen(100,100) = 100+100 - 100*100/255 ≈ 161
    EXPECT_NEAR(result.r, 161, 2);
}

TEST(OverlayBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Overlay);
    // Background < 128: Overlay = 2*A*B/255
    auto bg = ColorBgra::fromBgra(50, 50, 50, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    // Overlay(50,100) = 2*50*100/255 ≈ 39
    EXPECT_NEAR(result.r, 39, 2);
}

TEST(DifferenceBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Difference);
    auto bg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto fg = ColorBgra::fromBgra(50, 50, 50, 255);
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result.r, 150);
}

TEST(DarkenBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Darken);
    auto bg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result.r, 100);
}

TEST(LightenBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Lighten);
    auto bg = ColorBgra::fromBgra(50, 50, 50, 255);
    auto fg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result.r, 200);
}

TEST(XorBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Xor);
    auto bg = ColorBgra::fromBgra(0xFF, 0xFF, 0xFF, 255);
    auto fg = ColorBgra::fromBgra(0x0F, 0x0F, 0x0F, 255);
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result.r, 0xF0);
}

TEST(NegationBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::Negation);
    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto result = op->apply(bg, fg);
    // Negation(100,100) = 255 - |255-100-100| = 255 - 55 = 200
    EXPECT_EQ(result.r, 200);
}

TEST(ColorDodgeBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::ColorDodge);
    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::fromBgra(128, 128, 128, 255);
    auto result = op->apply(bg, fg);
    // ColorDodge(100,128) = 100*255/(255-128) = 25500/127 ≈ 200
    EXPECT_NEAR(result.r, 200, 2);
}

TEST(ColorBurnBlend, OpaqueOverOpaque) {
    auto op = createBlendOp(BlendMode::ColorBurn);
    auto bg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto fg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto result = op->apply(bg, fg);
    // ColorBurn(200,200) = 255 - (255-200)*255/200 = 255 - 55*255/200 ≈ 255 - 70 = 185
    EXPECT_NEAR(result.r, 185, 2);
}

// --- Transparency handling ---

TEST(GenericBlend, TransparentForegroundPassthrough) {
    auto op = createBlendOp(BlendMode::Multiply);
    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::transparent();
    auto result = op->apply(bg, fg);
    EXPECT_EQ(result, bg);
}

TEST(GenericBlend, WithOpacity) {
    auto op = createBlendOp(BlendMode::Multiply);
    auto withOpacity = op->createWithOpacity(128);

    auto bg = ColorBgra::fromBgra(255, 255, 255, 255);
    auto fg = ColorBgra::fromBgra(255, 255, 255, 255);
    auto result = withOpacity->apply(bg, fg);
    // Multiply(255,255) at 50% opacity: F=255, blended with bg at ~50%
    EXPECT_EQ(result.a, 255);
    EXPECT_GE(result.r, 250); // white * white = white, blended with white bg
}

TEST(GenericBlend, ZeroOpacityPassthrough) {
    auto op = createBlendOp(BlendMode::Screen);
    auto withZero = op->createWithOpacity(0);

    auto bg = ColorBgra::fromBgra(100, 100, 100, 255);
    auto fg = ColorBgra::fromBgra(200, 200, 200, 255);
    auto result = withZero->apply(bg, fg);
    EXPECT_EQ(result, bg);
}
