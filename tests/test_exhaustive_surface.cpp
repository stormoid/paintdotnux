#include <gtest/gtest.h>
#include "core/surface.h"
#include "core/colorbgra.h"

using namespace paintnux;

// Helper: create a surface with a gradient pattern
static Surface makeGradient(int w, int h) {
    Surface s(w, h);
    for (int y = 0; y < h; ++y) {
        auto* row = s.rowPtr(y);
        for (int x = 0; x < w; ++x) {
            uint8_t r = static_cast<uint8_t>((x * 255) / std::max(w - 1, 1));
            uint8_t g = static_cast<uint8_t>((y * 255) / std::max(h - 1, 1));
            row[x] = ColorBgra::fromBgra(128, g, r, 255);
        }
    }
    return s;
}

// ===== fitSurface with NearestNeighbor =====

TEST(SurfaceResampling, NearestNeighborUpscale) {
    auto src = makeGradient(10, 10);
    Surface dst(20, 20);
    dst.fitSurface(ResamplingAlgorithm::NearestNeighbor, src);
    // Corners should roughly match
    auto srcCorner = src.getPoint(0, 0);
    auto dstCorner = dst.getPoint(0, 0);
    EXPECT_EQ(srcCorner.bgra, dstCorner.bgra);
}

TEST(SurfaceResampling, NearestNeighborDownscale) {
    auto src = makeGradient(20, 20);
    Surface dst(10, 10);
    dst.fitSurface(ResamplingAlgorithm::NearestNeighbor, src);
    EXPECT_EQ(dst.width(), 10);
    EXPECT_EQ(dst.height(), 10);
}

TEST(SurfaceResampling, NearestNeighborSameSize) {
    auto src = makeGradient(15, 15);
    Surface dst(15, 15);
    dst.fitSurface(ResamplingAlgorithm::NearestNeighbor, src);
    // Should be identical
    for (int y = 0; y < 15; ++y) {
        auto* rs = src.rowPtr(y);
        auto* rd = dst.rowPtr(y);
        for (int x = 0; x < 15; ++x) {
            EXPECT_EQ(rs[x].bgra, rd[x].bgra) << "at (" << x << ", " << y << ")";
        }
    }
}

// ===== fitSurface with Bilinear =====

TEST(SurfaceResampling, BilinearUpscale) {
    auto src = makeGradient(10, 10);
    Surface dst(30, 30);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bilinear, src));
    // Just verify no crash and dimensions
    EXPECT_EQ(dst.width(), 30);
}

TEST(SurfaceResampling, BilinearDownscale) {
    auto src = makeGradient(30, 30);
    Surface dst(10, 10);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bilinear, src));
}

// ===== fitSurface with Bicubic =====

TEST(SurfaceResampling, BicubicUpscale) {
    auto src = makeGradient(10, 10);
    Surface dst(25, 25);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bicubic, src));
}

TEST(SurfaceResampling, BicubicDownscale) {
    auto src = makeGradient(25, 25);
    Surface dst(8, 8);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bicubic, src));
}

// ===== fitSurface with SuperSampling =====

TEST(SurfaceResampling, SuperSamplingDownscale) {
    auto src = makeGradient(40, 40);
    Surface dst(10, 10);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::SuperSampling, src));
}

TEST(SurfaceResampling, SuperSamplingUpscaleFallsToBicubic) {
    auto src = makeGradient(10, 10);
    Surface dst(30, 30);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::SuperSampling, src));
}

// ===== Edge cases =====

TEST(SurfaceResampling, OneByOneSource) {
    Surface src(1, 1);
    src.setPoint(0, 0, ColorBgra::fromBgra(100, 150, 200, 255));
    Surface dst(10, 10);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bilinear, src));
    // All pixels should be the same color
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            auto p = dst.getPoint(x, y);
            EXPECT_NEAR(p.r, 200, 2);
            EXPECT_NEAR(p.g, 150, 2);
            EXPECT_NEAR(p.b, 100, 2);
        }
    }
}

TEST(SurfaceResampling, OneByOneDst) {
    auto src = makeGradient(20, 20);
    Surface dst(1, 1);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::NearestNeighbor, src));
}

TEST(SurfaceResampling, LargeSurface) {
    auto src = makeGradient(200, 200);
    Surface dst(50, 50);
    ASSERT_NO_THROW(dst.fitSurface(ResamplingAlgorithm::Bicubic, src));
}

// ===== Boundary pixel sampling =====

TEST(SurfaceBilinear, CenterSample) {
    Surface s(3, 3);
    s.clear(ColorBgra::fromBgra(0, 0, 0, 255));
    s.setPoint(1, 1, ColorBgra::fromBgra(200, 200, 200, 255));
    auto p = s.getBilinearSample(1.0f, 1.0f);
    EXPECT_EQ(p.r, 200);
    EXPECT_EQ(p.g, 200);
    EXPECT_EQ(p.b, 200);
}

TEST(SurfaceBilinear, InterpolatedSample) {
    Surface s(2, 2);
    s.setPoint(0, 0, ColorBgra::fromBgra(0, 0, 0, 255));
    s.setPoint(1, 0, ColorBgra::fromBgra(255, 255, 255, 255));
    s.setPoint(0, 1, ColorBgra::fromBgra(0, 0, 0, 255));
    s.setPoint(1, 1, ColorBgra::fromBgra(255, 255, 255, 255));

    auto p = s.getBilinearSample(0.5f, 0.5f);
    // Should be roughly 128 (midpoint)
    EXPECT_NEAR(p.r, 128, 2);
}

TEST(SurfaceBilinear, OutOfBoundsReturnsTransparent) {
    Surface s(5, 5);
    s.clear(ColorBgra::fromBgra(255, 255, 255, 255));
    auto p = s.getBilinearSample(-5.0f, -5.0f);
    EXPECT_EQ(p.a, 0);
}

TEST(SurfaceBilinear, ClampedEdge) {
    Surface s(5, 5);
    s.clear(ColorBgra::fromBgra(100, 150, 200, 255));
    auto p = s.getBilinearSampleClamped(-1.0f, -1.0f);
    // Should clamp to edge — return edge color
    EXPECT_EQ(p.r, 200);
    EXPECT_EQ(p.g, 150);
    EXPECT_EQ(p.b, 100);
}

TEST(SurfaceBilinear, ClampedFarEdge) {
    Surface s(5, 5);
    s.clear(ColorBgra::fromBgra(100, 150, 200, 255));
    auto p = s.getBilinearSampleClamped(100.0f, 100.0f);
    EXPECT_EQ(p.r, 200);
    EXPECT_EQ(p.g, 150);
    EXPECT_EQ(p.b, 100);
}
