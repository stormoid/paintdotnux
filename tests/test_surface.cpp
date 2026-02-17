#include "core/surface.h"

#include <gtest/gtest.h>

using namespace paintnux;

TEST(Surface, CreateTransparent) {
    Surface s(100, 50);
    EXPECT_EQ(s.width(), 100);
    EXPECT_EQ(s.height(), 50);
    // Should be transparent
    EXPECT_EQ(s.getPoint(0, 0).bgra, 0u);
    EXPECT_EQ(s.getPoint(99, 49).bgra, 0u);
}

TEST(Surface, SetAndGetPoint) {
    Surface s(10, 10);
    auto red = ColorBgra::fromBgra(0, 0, 255, 255);
    s.setPoint(5, 5, red);
    EXPECT_EQ(s.getPoint(5, 5), red);
}

TEST(Surface, GetPointOutOfBounds) {
    Surface s(10, 10);
    EXPECT_EQ(s.getPoint(-1, 0).bgra, 0u);
    EXPECT_EQ(s.getPoint(10, 0).bgra, 0u);
    EXPECT_EQ(s.getPoint(0, -1).bgra, 0u);
    EXPECT_EQ(s.getPoint(0, 10).bgra, 0u);
}

TEST(Surface, ClearColor) {
    Surface s(10, 10);
    auto blue = ColorBgra::fromBgra(255, 0, 0, 255);
    s.clear(blue);
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            EXPECT_EQ(s.getPoint(x, y), blue);
        }
    }
}

TEST(Surface, ClearRect) {
    Surface s(20, 20);
    auto green = ColorBgra::fromBgra(0, 255, 0, 255);
    s.clear(QRect(5, 5, 10, 10), green);
    EXPECT_EQ(s.getPoint(5, 5), green);
    EXPECT_EQ(s.getPoint(14, 14), green);
    EXPECT_EQ(s.getPoint(4, 5).bgra, 0u); // outside rect
}

TEST(Surface, CopySurface) {
    Surface src(10, 10);
    src.clear(ColorBgra::white());
    Surface dst(10, 10);
    dst.copySurface(src);
    EXPECT_EQ(dst.getPoint(0, 0), ColorBgra::white());
}

TEST(Surface, CopySurfaceWithOffset) {
    Surface src(5, 5);
    auto c = ColorBgra::fromBgra(100, 100, 100, 255);
    src.clear(c);

    Surface dst(20, 20);
    dst.copySurface(src, QPoint(10, 10), src.bounds());
    EXPECT_EQ(dst.getPoint(10, 10), c);
    EXPECT_EQ(dst.getPoint(14, 14), c);
    EXPECT_EQ(dst.getPoint(9, 9).bgra, 0u);
}

TEST(Surface, Clone) {
    Surface s(10, 10);
    s.clear(ColorBgra::black());
    Surface c = s.clone();

    EXPECT_EQ(c.width(), 10);
    EXPECT_EQ(c.height(), 10);
    EXPECT_EQ(c.getPoint(0, 0), ColorBgra::black());

    // Modifying clone should not affect original
    c.clear(ColorBgra::white());
    EXPECT_EQ(s.getPoint(0, 0), ColorBgra::black());
    EXPECT_EQ(c.getPoint(0, 0), ColorBgra::white());
}

TEST(Surface, RowPtr) {
    Surface s(10, 10);
    auto red = ColorBgra::fromBgra(0, 0, 255, 255);
    ColorBgra* row = s.rowPtr(5);
    row[3] = red;
    EXPECT_EQ(s.getPoint(3, 5), red);
}

TEST(Surface, IsVisible) {
    Surface s(10, 10);
    EXPECT_TRUE(s.isVisible(0, 0));
    EXPECT_TRUE(s.isVisible(9, 9));
    EXPECT_FALSE(s.isVisible(-1, 0));
    EXPECT_FALSE(s.isVisible(10, 0));
    EXPECT_FALSE(s.isVisible(0, 10));
}

TEST(Surface, BilinearSampleCenter) {
    Surface s(10, 10);
    s.clear(ColorBgra::white());
    // Sampling at integer coords should return exact pixel
    auto sample = s.getBilinearSample(5.0f, 5.0f);
    EXPECT_EQ(sample, ColorBgra::white());
}

TEST(Surface, BilinearSampleOutOfBounds) {
    Surface s(10, 10);
    s.clear(ColorBgra::white());
    auto sample = s.getBilinearSample(-1.0f, 5.0f);
    EXPECT_EQ(sample.bgra, 0u);
}

TEST(Surface, Checkerboard) {
    Surface s(16, 16);
    s.clearWithCheckerboard();
    // Just verify no crash and pixels are set
    auto p1 = s.getPoint(0, 0);
    auto p2 = s.getPoint(8, 0);
    EXPECT_EQ(p1.a, 255);
    EXPECT_EQ(p2.a, 255);
    // The two 8x8 blocks should differ
    EXPECT_NE(p1.b, p2.b);
}

TEST(Surface, QImageInterop) {
    Surface s(10, 10);
    s.clear(ColorBgra::white());
    const QImage& img = s.qimage();
    EXPECT_EQ(img.width(), 10);
    EXPECT_EQ(img.height(), 10);
    EXPECT_EQ(img.format(), QImage::Format_ARGB32);
}
