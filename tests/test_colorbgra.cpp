#include "core/colorbgra.h"

#include <gtest/gtest.h>

using namespace paintnux;

TEST(ColorBgra, SizeIs4Bytes) {
    EXPECT_EQ(sizeof(ColorBgra), 4u);
}

TEST(ColorBgra, FromBgra) {
    auto c = ColorBgra::fromBgra(10, 20, 30, 40);
    EXPECT_EQ(c.b, 10);
    EXPECT_EQ(c.g, 20);
    EXPECT_EQ(c.r, 30);
    EXPECT_EQ(c.a, 40);
}

TEST(ColorBgra, FromBgr) {
    auto c = ColorBgra::fromBgr(10, 20, 30);
    EXPECT_EQ(c.b, 10);
    EXPECT_EQ(c.g, 20);
    EXPECT_EQ(c.r, 30);
    EXPECT_EQ(c.a, 255);
}

TEST(ColorBgra, UnionOverlay) {
    auto c = ColorBgra::fromBgra(0x12, 0x34, 0x56, 0x78);
    // On little-endian: bgra = 0x78563412
    EXPECT_EQ(c.bgra, 0x78563412u);
}

TEST(ColorBgra, FromUInt32) {
    auto c = ColorBgra::fromUInt32(0xFF00FF00);
    EXPECT_EQ(c.b, 0x00);
    EXPECT_EQ(c.g, 0xFF);
    EXPECT_EQ(c.r, 0x00);
    EXPECT_EQ(c.a, 0xFF);
}

TEST(ColorBgra, Transparent) {
    auto c = ColorBgra::transparent();
    EXPECT_EQ(c.bgra, 0u);
}

TEST(ColorBgra, BlackAndWhite) {
    auto b = ColorBgra::black();
    EXPECT_EQ(b.r, 0);
    EXPECT_EQ(b.g, 0);
    EXPECT_EQ(b.b, 0);
    EXPECT_EQ(b.a, 255);

    auto w = ColorBgra::white();
    EXPECT_EQ(w.r, 255);
    EXPECT_EQ(w.g, 255);
    EXPECT_EQ(w.b, 255);
    EXPECT_EQ(w.a, 255);
}

TEST(ColorBgra, ClampToByte) {
    EXPECT_EQ(ColorBgra::clampToByte(-10), 0);
    EXPECT_EQ(ColorBgra::clampToByte(0), 0);
    EXPECT_EQ(ColorBgra::clampToByte(128), 128);
    EXPECT_EQ(ColorBgra::clampToByte(255), 255);
    EXPECT_EQ(ColorBgra::clampToByte(300), 255);
}

TEST(ColorBgra, FromBgraClamped) {
    auto c = ColorBgra::fromBgraClamped(-10, 128, 300, 200);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.g, 128);
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.a, 200);
}

TEST(ColorBgra, Equality) {
    auto a = ColorBgra::fromBgra(1, 2, 3, 4);
    auto b = ColorBgra::fromBgra(1, 2, 3, 4);
    auto c = ColorBgra::fromBgra(1, 2, 3, 5);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(ColorBgra, ChannelIndex) {
    auto c = ColorBgra::fromBgra(10, 20, 30, 40);
    EXPECT_EQ(c[0], 10); // B
    EXPECT_EQ(c[1], 20); // G
    EXPECT_EQ(c[2], 30); // R
    EXPECT_EQ(c[3], 40); // A
}

TEST(ColorBgra, NewAlpha) {
    auto c = ColorBgra::fromBgra(10, 20, 30, 40);
    auto c2 = c.newAlpha(100);
    EXPECT_EQ(c2.b, 10);
    EXPECT_EQ(c2.g, 20);
    EXPECT_EQ(c2.r, 30);
    EXPECT_EQ(c2.a, 100);
}

TEST(ColorBgra, IntensityByte) {
    // Pure white: should be ~255
    auto w = ColorBgra::white();
    EXPECT_GE(w.getIntensityByte(), 254);
    EXPECT_LE(w.getIntensityByte(), 255);

    // Pure black: should be 0
    auto b = ColorBgra::black();
    EXPECT_EQ(b.getIntensityByte(), 0);
}

TEST(ColorBgra, FastScaleByteByByte) {
    EXPECT_EQ(ColorBgra::fastScaleByteByByte(0, 0), 0);
    EXPECT_EQ(ColorBgra::fastScaleByteByByte(255, 255), 255);
    EXPECT_EQ(ColorBgra::fastScaleByteByByte(255, 0), 0);
    EXPECT_EQ(ColorBgra::fastScaleByteByByte(0, 255), 0);
    // ~50% * ~50%
    uint8_t half = ColorBgra::fastScaleByteByByte(128, 128);
    EXPECT_GE(half, 63);
    EXPECT_LE(half, 65);
}

TEST(ColorBgra, Premultiply) {
    auto c = ColorBgra::fromBgra(200, 100, 50, 128);
    auto pm = c.premultiply();
    EXPECT_EQ(pm.a, 128);
    // Each channel scaled by 128/255 ≈ 0.502
    EXPECT_NEAR(pm.b, 100, 2);
    EXPECT_NEAR(pm.g, 50, 2);
    EXPECT_NEAR(pm.r, 25, 2);
}

TEST(ColorBgra, UnpremultiplyRoundTrip) {
    auto c = ColorBgra::fromBgra(200, 100, 50, 180);
    auto pm = c.premultiply();
    auto upm = pm.unpremultiply();
    EXPECT_NEAR(upm.b, c.b, 2);
    EXPECT_NEAR(upm.g, c.g, 2);
    EXPECT_NEAR(upm.r, c.r, 2);
    EXPECT_EQ(upm.a, c.a);
}

TEST(ColorBgra, Lerp) {
    auto a = ColorBgra::fromBgra(0, 0, 0, 255);
    auto b = ColorBgra::fromBgra(200, 100, 50, 255);
    auto mid = ColorBgra::lerp(a, b, 0.5f);
    EXPECT_NEAR(mid.b, 100, 2);
    EXPECT_NEAR(mid.g, 50, 2);
    EXPECT_NEAR(mid.r, 25, 2);
}

TEST(ColorBgra, BlendColors4W16IP) {
    auto c = ColorBgra::fromBgra(100, 100, 100, 255);
    // All same color, any weights summing to 65536
    auto result = ColorBgra::blendColors4W16IP(c, 16384, c, 16384, c, 16384, c, 16384);
    EXPECT_EQ(result.b, 100);
    EXPECT_EQ(result.g, 100);
    EXPECT_EQ(result.r, 100);
}
