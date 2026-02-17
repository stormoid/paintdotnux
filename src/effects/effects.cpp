#include "effects/effects.h"
#include "adjustments/adjustments.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <random>
#include <thread>
#include <vector>

namespace paintnux {

// --- Helpers ---

static void parallelRows(int height, const std::function<void(int, int)>& func) {
    int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    if (height < numThreads * 16) {
        func(0, height);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    int rowsPerThread = height / numThreads;
    for (int t = 0; t < numThreads; ++t) {
        int yStart = t * rowsPerThread;
        int yEnd = (t == numThreads - 1) ? height : yStart + rowsPerThread;
        threads.emplace_back(func, yStart, yEnd);
    }
    for (auto& th : threads) th.join();
}

// ============================================================
// Gaussian Blur — tent kernel via two box blur passes, O(1) sliding window
// ============================================================

// Single horizontal box blur pass with sliding window accumulator
static void boxBlurH(const Surface& src, Surface& dst, int radius) {
    int w = src.width(), h = src.height();
    int span = radius * 2 + 1;

    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);

            // Initialize accumulator with clamped-edge samples for x=0
            long sumB = 0, sumG = 0, sumR = 0, sumA = 0;
            for (int k = -radius; k <= radius; ++k) {
                int sx = std::clamp(k, 0, w - 1);
                sumB += srcRow[sx].b; sumG += srcRow[sx].g;
                sumR += srcRow[sx].r; sumA += srcRow[sx].a;
            }
            dstRow[0] = ColorBgra::fromBgra(
                static_cast<uint8_t>(sumB / span), static_cast<uint8_t>(sumG / span),
                static_cast<uint8_t>(sumR / span), static_cast<uint8_t>(sumA / span));

            for (int x = 1; x < w; ++x) {
                int addIdx = std::min(x + radius, w - 1);
                int remIdx = std::clamp(x - radius - 1, 0, w - 1);
                sumB += srcRow[addIdx].b - srcRow[remIdx].b;
                sumG += srcRow[addIdx].g - srcRow[remIdx].g;
                sumR += srcRow[addIdx].r - srcRow[remIdx].r;
                sumA += srcRow[addIdx].a - srcRow[remIdx].a;
                dstRow[x] = ColorBgra::fromBgra(
                    static_cast<uint8_t>(sumB / span), static_cast<uint8_t>(sumG / span),
                    static_cast<uint8_t>(sumR / span), static_cast<uint8_t>(sumA / span));
            }
        }
    });
}

// Single vertical box blur pass with sliding window accumulator
static void boxBlurV(const Surface& src, Surface& dst, int radius) {
    int w = src.width(), h = src.height();
    int span = radius * 2 + 1;

    // Process columns in parallel by splitting into column ranges
    parallelRows(w, [&](int xStart, int xEnd) {
        for (int x = xStart; x < xEnd; ++x) {
            // Initialize accumulator for y=0
            long sumB = 0, sumG = 0, sumR = 0, sumA = 0;
            for (int k = -radius; k <= radius; ++k) {
                int sy = std::clamp(k, 0, h - 1);
                const ColorBgra* row = src.rowPtr(sy);
                sumB += row[x].b; sumG += row[x].g;
                sumR += row[x].r; sumA += row[x].a;
            }
            dst.rowPtr(0)[x] = ColorBgra::fromBgra(
                static_cast<uint8_t>(sumB / span), static_cast<uint8_t>(sumG / span),
                static_cast<uint8_t>(sumR / span), static_cast<uint8_t>(sumA / span));

            for (int y = 1; y < h; ++y) {
                int addIdx = std::min(y + radius, h - 1);
                int remIdx = std::clamp(y - radius - 1, 0, h - 1);
                const ColorBgra* addRow = src.rowPtr(addIdx);
                const ColorBgra* remRow = src.rowPtr(remIdx);
                sumB += addRow[x].b - remRow[x].b;
                sumG += addRow[x].g - remRow[x].g;
                sumR += addRow[x].r - remRow[x].r;
                sumA += addRow[x].a - remRow[x].a;
                dst.rowPtr(y)[x] = ColorBgra::fromBgra(
                    static_cast<uint8_t>(sumB / span), static_cast<uint8_t>(sumG / span),
                    static_cast<uint8_t>(sumR / span), static_cast<uint8_t>(sumA / span));
            }
        }
    });
}

void effectGaussianBlur(const Surface& src, Surface& dst, int radius) {
    radius = std::clamp(radius, 1, 200);

    // Tent kernel = two box blur passes (box * box = triangle)
    // Use radius+1 for first pass, radius for second to match tent width
    int r1 = (radius + 1) / 2;
    int r2 = radius / 2 + 1;

    Surface temp(src.width(), src.height());

    // Pass 1: horizontal box blur src→temp, vertical box blur temp→dst
    boxBlurH(src, temp, r1);
    boxBlurV(temp, dst, r1);

    // Pass 2: horizontal box blur dst→temp, vertical box blur temp→dst
    boxBlurH(dst, temp, r2);
    boxBlurV(temp, dst, r2);
}

// ============================================================
// Motion Blur — directional blur along angle
// ============================================================

void effectMotionBlur(const Surface& src, Surface& dst, double angle, int distance) {
    distance = std::clamp(distance, 1, 200);
    int w = src.width(), h = src.height();
    double rad = angle * M_PI / 180.0;
    double dx = std::cos(rad);
    double dy = std::sin(rad);

    int numSamples = 1 + distance * 3 / 2;

    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                long sumR = 0, sumG = 0, sumB = 0, sumA = 0;
                int count = 0;
                for (int s = 0; s < numSamples; ++s) {
                    double t = (static_cast<double>(s) / (numSamples - 1) - 0.5) * distance;
                    float sx = static_cast<float>(x + dx * t);
                    float sy = static_cast<float>(y + dy * t);
                    if (sx >= 0 && sx < w - 1 && sy >= 0 && sy < h - 1) {
                        ColorBgra c = src.getBilinearSampleClamped(sx, sy);
                        sumB += c.b; sumG += c.g; sumR += c.r; sumA += c.a;
                        ++count;
                    }
                }
                if (count > 0) {
                    dstRow[x] = ColorBgra::fromBgra(
                        static_cast<uint8_t>(sumB / count),
                        static_cast<uint8_t>(sumG / count),
                        static_cast<uint8_t>(sumR / count),
                        static_cast<uint8_t>(sumA / count));
                } else {
                    dstRow[x] = src.getPoint(x, y);
                }
            }
        }
    });
}

// ============================================================
// Unfocus — box blur via integral image
// ============================================================

void effectUnfocus(const Surface& src, Surface& dst, int radius) {
    radius = std::clamp(radius, 1, 200);
    int w = src.width(), h = src.height();

    // Build integral image (64-bit accumulators to avoid overflow)
    struct IntPixel { int64_t b, g, r, a; };
    std::vector<IntPixel> integral((w + 1) * (h + 1), {0, 0, 0, 0});
    auto idx = [&](int x, int y) -> IntPixel& { return integral[y * (w + 1) + x]; };

    for (int y = 0; y < h; ++y) {
        const ColorBgra* row = src.rowPtr(y);
        int64_t rowB = 0, rowG = 0, rowR = 0, rowA = 0;
        for (int x = 0; x < w; ++x) {
            rowB += row[x].b; rowG += row[x].g; rowR += row[x].r; rowA += row[x].a;
            auto& above = idx(x + 1, y);
            auto& cur = idx(x + 1, y + 1);
            cur.b = rowB + above.b;
            cur.g = rowG + above.g;
            cur.r = rowR + above.r;
            cur.a = rowA + above.a;
        }
    }

    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                int x1 = std::max(0, x - radius);
                int y1 = std::max(0, y - radius);
                int x2 = std::min(w - 1, x + radius);
                int y2 = std::min(h - 1, y + radius);
                int area = (x2 - x1 + 1) * (y2 - y1 + 1);
                auto& br = idx(x2 + 1, y2 + 1);
                auto& tl = idx(x1, y1);
                auto& tr = idx(x2 + 1, y1);
                auto& bl = idx(x1, y2 + 1);
                dstRow[x] = ColorBgra::fromBgra(
                    static_cast<uint8_t>((br.b - tr.b - bl.b + tl.b) / area),
                    static_cast<uint8_t>((br.g - tr.g - bl.g + tl.g) / area),
                    static_cast<uint8_t>((br.r - tr.r - bl.r + tl.r) / area),
                    static_cast<uint8_t>((br.a - tr.a - bl.a + tl.a) / area));
            }
        }
    });
}

// ============================================================
// Add Noise — Gaussian noise with saturation
// ============================================================

void effectAddNoise(const Surface& src, Surface& dst, int intensity, int saturation, int coverage) {
    intensity = std::clamp(intensity, 1, 100);
    saturation = std::clamp(saturation, 0, 400);
    coverage = std::clamp(coverage, 0, 100);
    int w = src.width(), h = src.height();

    // Build Gaussian LUT matching Paint.NET's CDF-based approach
    // The LUT maps uniform random indices to Gaussian-distributed fixed-point values
    constexpr int TABLE_SIZE = 16384;
    std::array<int, TABLE_SIZE> lookup{};

    // Binary search for scale that makes integral = TABLE_SIZE
    auto normalCurve = [](double x, double sc) { return sc * std::exp(-x * x / 2.0); };
    double lo = 5.0, hi = 10.0, sc = 50.0;
    for (int iter = 0; iter < 100; ++iter) {
        sc = (lo + hi) * 0.5;
        double sum = 0;
        for (int i = 0; i < TABLE_SIZE; ++i) {
            sum += normalCurve(16.0 * (static_cast<double>(i) - TABLE_SIZE / 2) / TABLE_SIZE, sc);
            if (sum > 1000000) break;
        }
        if (std::abs(sum - TABLE_SIZE) < 0.5) break;
        if (sum > TABLE_SIZE) hi = sc; else lo = sc;
    }

    // Fill lookup via CDF mapping
    {
        double sum = 0;
        int roundedSum = 0;
        for (int i = 0; i < TABLE_SIZE; ++i) {
            sum += normalCurve(16.0 * (static_cast<double>(i) - TABLE_SIZE / 2) / TABLE_SIZE, sc);
            int lastRoundedSum = roundedSum;
            roundedSum = static_cast<int>(sum);
            for (int j = lastRoundedSum; j < roundedSum && j < TABLE_SIZE; ++j)
                lookup[j] = (i - TABLE_SIZE / 2) * 65536 / TABLE_SIZE;
        }
    }

    int dev = intensity * intensity / 4;
    int sat = saturation * 4096 / 100;
    double coverageF = coverage / 100.0;

    parallelRows(h, [&](int yStart, int yEnd) {
        std::mt19937 rng(yStart * 31337 + 12345);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                if (coverage < 100 && dist(rng) > coverageF) {
                    dstRow[x] = srcRow[x];
                    continue;
                }

                int r = lookup[rng() % TABLE_SIZE];
                int g = lookup[rng() % TABLE_SIZE];
                int b = lookup[rng() % TABLE_SIZE];

                // Apply saturation: blend noise channels toward their grayscale
                int i = (4899 * r + 9618 * g + 1867 * b) >> 14;
                r = i + (((r - i) * sat) >> 12);
                g = i + (((g - i) * sat) >> 12);
                b = i + (((b - i) * sat) >> 12);

                // Scale by dev and add to source (fixed-point >> 16 with rounding)
                dstRow[x] = ColorBgra::fromBgraClamped(
                    srcRow[x].b + ((b * dev + 32768) >> 16),
                    srcRow[x].g + ((g * dev + 32768) >> 16),
                    srcRow[x].r + ((r * dev + 32768) >> 16),
                    srcRow[x].a);
            }
        }
    });
}

// ============================================================
// Median — sliding histogram median filter
// ============================================================

void effectMedian(const Surface& src, Surface& dst, int radius, int percentile) {
    radius = std::clamp(radius, 1, 50);
    percentile = std::clamp(percentile, 0, 100);
    int w = src.width(), h = src.height();

    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);

            // Per-channel histograms
            std::array<int, 256> histR{}, histG{}, histB{}, histA{};
            int count = 0;

            // Initialize histogram for x=0
            for (int ky = std::max(0, y - radius); ky <= std::min(h - 1, y + radius); ++ky) {
                const ColorBgra* row = src.rowPtr(ky);
                for (int kx = 0; kx <= std::min(w - 1, radius); ++kx) {
                    histR[row[kx].r]++; histG[row[kx].g]++;
                    histB[row[kx].b]++; histA[row[kx].a]++;
                    ++count;
                }
            }

            auto findPercentile = [&](const std::array<int, 256>& hist, int total) -> uint8_t {
                int target = std::max(1, total * percentile / 100);
                int cumul = 0;
                for (int i = 0; i < 256; ++i) {
                    cumul += hist[i];
                    if (cumul >= target) return static_cast<uint8_t>(i);
                }
                return 255;
            };

            dstRow[0] = ColorBgra::fromBgra(
                findPercentile(histB, count), findPercentile(histG, count),
                findPercentile(histR, count), findPercentile(histA, count));

            for (int x = 1; x < w; ++x) {
                // Remove left column (x - radius - 1)
                int removeX = x - radius - 1;
                if (removeX >= 0) {
                    for (int ky = std::max(0, y - radius); ky <= std::min(h - 1, y + radius); ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        histR[row[removeX].r]--; histG[row[removeX].g]--;
                        histB[row[removeX].b]--; histA[row[removeX].a]--;
                        --count;
                    }
                }
                // Add right column (x + radius)
                int addX = x + radius;
                if (addX < w) {
                    for (int ky = std::max(0, y - radius); ky <= std::min(h - 1, y + radius); ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        histR[row[addX].r]++; histG[row[addX].g]++;
                        histB[row[addX].b]++; histA[row[addX].a]++;
                        ++count;
                    }
                }

                dstRow[x] = ColorBgra::fromBgra(
                    findPercentile(histB, count), findPercentile(histG, count),
                    findPercentile(histR, count), findPercentile(histA, count));
            }
        }
    });
}

// ============================================================
// Pixelate — block averaging
// ============================================================

void effectPixelate(const Surface& src, Surface& dst, int cellSize) {
    cellSize = std::clamp(cellSize, 1, 100);
    int w = src.width(), h = src.height();

    for (int cy = 0; cy < h; cy += cellSize) {
        for (int cx = 0; cx < w; cx += cellSize) {
            int x2 = std::min(cx + cellSize, w);
            int y2 = std::min(cy + cellSize, h);

            // Sample corners and average
            ColorBgra c00 = src.getPoint(cx, cy);
            ColorBgra c10 = src.getPoint(std::min(x2 - 1, w - 1), cy);
            ColorBgra c01 = src.getPoint(cx, std::min(y2 - 1, h - 1));
            ColorBgra c11 = src.getPoint(std::min(x2 - 1, w - 1), std::min(y2 - 1, h - 1));

            int avgB = (c00.b + c10.b + c01.b + c11.b) / 4;
            int avgG = (c00.g + c10.g + c01.g + c11.g) / 4;
            int avgR = (c00.r + c10.r + c01.r + c11.r) / 4;
            int avgA = (c00.a + c10.a + c01.a + c11.a) / 4;
            ColorBgra fill = ColorBgra::fromBgra(
                static_cast<uint8_t>(avgB), static_cast<uint8_t>(avgG),
                static_cast<uint8_t>(avgR), static_cast<uint8_t>(avgA));

            for (int y = cy; y < y2; ++y) {
                ColorBgra* dstRow = dst.rowPtr(y);
                for (int x = cx; x < x2; ++x)
                    dstRow[x] = fill;
            }
        }
    }
}

// ============================================================
// Edge Detect / Emboss / Relief — 3x3 cosine-weighted kernel
// ============================================================

static void apply3x3CosKernel(const Surface& src, Surface& dst, double angle,
                                int mode) {
    // mode: 0=edge detect, 1=emboss, 2=relief
    double r = angle * 2.0 * M_PI / 360.0;
    double dr = M_PI / 4.0;
    int w = src.width(), h = src.height();

    // Build 3x3 kernel matching Paint.NET's clockwise angular layout
    double kernel[3][3];
    kernel[0][0] = std::cos(r + dr);
    kernel[0][1] = std::cos(r + 2.0 * dr);
    kernel[0][2] = std::cos(r + 3.0 * dr);
    kernel[1][0] = std::cos(r);
    kernel[1][1] = (mode == 2) ? 1.0 : 0.0; // Relief has center=1
    kernel[1][2] = std::cos(r + 4.0 * dr);
    kernel[2][0] = std::cos(r - dr);
    kernel[2][1] = std::cos(r - 2.0 * dr);
    kernel[2][2] = std::cos(r - 3.0 * dr);

    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                double sumR = 0, sumG = 0, sumB = 0;
                for (int fy = 0; fy < 3; ++fy) {
                    int sy = std::clamp(y + fy - 1, 0, h - 1);
                    const ColorBgra* srcRow = src.rowPtr(sy);
                    for (int fx = 0; fx < 3; ++fx) {
                        int sx = std::clamp(x + fx - 1, 0, w - 1);
                        double wt = kernel[fy][fx];
                        sumR += srcRow[sx].r * wt;
                        sumG += srcRow[sx].g * wt;
                        sumB += srcRow[sx].b * wt;
                    }
                }

                if (mode == 1) {
                    // Emboss: grayscale intensity convolution, offset by 128, alpha=255
                    int gray = static_cast<int>((sumR * 0.299 + sumG * 0.587 + sumB * 0.114) + 128);
                    uint8_t g = ColorBgra::clampToByte(gray);
                    dstRow[x] = ColorBgra::fromBgra(g, g, g, 255);
                } else {
                    // Edge detect or Relief: per-channel output clamped
                    dstRow[x] = ColorBgra::fromBgraClamped(
                        static_cast<int>(sumB), static_cast<int>(sumG),
                        static_cast<int>(sumR),
                        static_cast<int>(src.getPoint(x, y).a));
                }
            }
        }
    });
}

void effectEdgeDetect(const Surface& src, Surface& dst, double angle) {
    apply3x3CosKernel(src, dst, angle, 0);
}

void effectEmboss(const Surface& src, Surface& dst, double angle) {
    apply3x3CosKernel(src, dst, angle, 1);
}

void effectRelief(const Surface& src, Surface& dst, double angle) {
    apply3x3CosKernel(src, dst, angle, 2);
}

// ============================================================
// Outline — local histogram range
// ============================================================

void effectOutline(const Surface& src, Surface& dst, int intensity, int radius) {
    intensity = std::clamp(intensity, 0, 100);
    radius = std::clamp(radius, 1, 200);
    int w = src.width(), h = src.height();

    parallelRows(h, [&](int yStart, int yEnd) {
        std::array<int, 256> histR{}, histG{}, histB{};

        auto findAtCount = [](const std::array<int, 256>& hist, int target) -> int {
            int cumul = 0;
            for (int i = 0; i < 256; ++i) {
                cumul += hist[i];
                if (cumul > target) return i;
            }
            return 255;
        };

        int y1 = std::max(0, yStart - radius);
        int y2 = std::min(h - 1, yStart + radius);

        for (int y = yStart; y < yEnd; ++y) {
            // Reset histograms at start of each row
            std::fill(histR.begin(), histR.end(), 0);
            std::fill(histG.begin(), histG.end(), 0);
            std::fill(histB.begin(), histB.end(), 0);

            int ky1 = std::max(0, y - radius);
            int ky2 = std::min(h - 1, y + radius);

            // Build initial histogram for x=0
            int area = 0;
            int kx2init = std::min(w - 1, radius);
            for (int ky = ky1; ky <= ky2; ++ky) {
                const ColorBgra* row = src.rowPtr(ky);
                for (int kx = 0; kx <= kx2init; ++kx) {
                    histR[row[kx].r]++; histG[row[kx].g]++; histB[row[kx].b]++;
                    ++area;
                }
            }

            ColorBgra* dstRow = dst.rowPtr(y);
            const ColorBgra* srcRow = src.rowPtr(y);

            int loTarget = area * (100 - intensity) / 200;
            int hiTarget = area * (100 + intensity) / 200;
            int outR = 255 - (findAtCount(histR, hiTarget) - findAtCount(histR, loTarget));
            int outG = 255 - (findAtCount(histG, hiTarget) - findAtCount(histG, loTarget));
            int outB = 255 - (findAtCount(histB, hiTarget) - findAtCount(histB, loTarget));
            dstRow[0] = ColorBgra::fromBgraClamped(outB, outG, outR, static_cast<int>(srcRow[0].a));

            for (int x = 1; x < w; ++x) {
                // Remove left column
                int removeX = x - radius - 1;
                if (removeX >= 0) {
                    for (int ky = ky1; ky <= ky2; ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        histR[row[removeX].r]--; histG[row[removeX].g]--; histB[row[removeX].b]--;
                        --area;
                    }
                }
                // Add right column
                int addX = x + radius;
                if (addX < w) {
                    for (int ky = ky1; ky <= ky2; ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        histR[row[addX].r]++; histG[row[addX].g]++; histB[row[addX].b]++;
                        ++area;
                    }
                }

                loTarget = area * (100 - intensity) / 200;
                hiTarget = area * (100 + intensity) / 200;
                outR = 255 - (findAtCount(histR, hiTarget) - findAtCount(histR, loTarget));
                outG = 255 - (findAtCount(histG, hiTarget) - findAtCount(histG, loTarget));
                outB = 255 - (findAtCount(histB, hiTarget) - findAtCount(histB, loTarget));
                dstRow[x] = ColorBgra::fromBgraClamped(outB, outG, outR, static_cast<int>(srcRow[x].a));
            }
        }
    });
}

// ============================================================
// Oil Painting — histogram mode
// ============================================================

void effectOilPainting(const Surface& src, Surface& dst, int brushSize, int coarseness) {
    brushSize = std::clamp(brushSize, 1, 8);
    coarseness = std::clamp(coarseness, 3, 255);
    int w = src.width(), h = src.height();

    parallelRows(h, [&](int yStart, int yEnd) {
        std::vector<int> intensityCount(coarseness + 1);
        std::vector<long> sumR(coarseness + 1), sumG(coarseness + 1), sumB(coarseness + 1);

        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);
            const ColorBgra* srcRow = src.rowPtr(y);

            int ky1 = std::max(0, y - brushSize);
            int ky2 = std::min(h - 1, y + brushSize);

            // Build initial histogram for x=0
            std::fill(intensityCount.begin(), intensityCount.end(), 0);
            std::fill(sumR.begin(), sumR.end(), 0);
            std::fill(sumG.begin(), sumG.end(), 0);
            std::fill(sumB.begin(), sumB.end(), 0);

            int kx2init = std::min(w - 1, brushSize);
            for (int ky = ky1; ky <= ky2; ++ky) {
                const ColorBgra* row = src.rowPtr(ky);
                for (int kx = 0; kx <= kx2init; ++kx) {
                    int bucket = row[kx].getIntensityByte() * coarseness / 255;
                    bucket = std::clamp(bucket, 0, coarseness);
                    intensityCount[bucket]++;
                    sumR[bucket] += row[kx].r;
                    sumG[bucket] += row[kx].g;
                    sumB[bucket] += row[kx].b;
                }
            }

            auto emitPixel = [&](int x) {
                int maxCount = 0, maxBucket = 0;
                for (int i = 0; i <= coarseness; ++i) {
                    if (intensityCount[i] > maxCount) {
                        maxCount = intensityCount[i];
                        maxBucket = i;
                    }
                }
                if (maxCount > 0) {
                    dstRow[x] = ColorBgra::fromBgra(
                        static_cast<uint8_t>(sumB[maxBucket] / maxCount),
                        static_cast<uint8_t>(sumG[maxBucket] / maxCount),
                        static_cast<uint8_t>(sumR[maxBucket] / maxCount),
                        srcRow[x].a);
                } else {
                    dstRow[x] = srcRow[x];
                }
            };

            emitPixel(0);

            for (int x = 1; x < w; ++x) {
                // Remove left column
                int removeX = x - brushSize - 1;
                if (removeX >= 0) {
                    for (int ky = ky1; ky <= ky2; ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        int bucket = row[removeX].getIntensityByte() * coarseness / 255;
                        bucket = std::clamp(bucket, 0, coarseness);
                        intensityCount[bucket]--;
                        sumR[bucket] -= row[removeX].r;
                        sumG[bucket] -= row[removeX].g;
                        sumB[bucket] -= row[removeX].b;
                    }
                }
                // Add right column
                int addX = x + brushSize;
                if (addX < w) {
                    for (int ky = ky1; ky <= ky2; ++ky) {
                        const ColorBgra* row = src.rowPtr(ky);
                        int bucket = row[addX].getIntensityByte() * coarseness / 255;
                        bucket = std::clamp(bucket, 0, coarseness);
                        intensityCount[bucket]++;
                        sumR[bucket] += row[addX].r;
                        sumG[bucket] += row[addX].g;
                        sumB[bucket] += row[addX].b;
                    }
                }
                emitPixel(x);
            }
        }
    });
}

// ============================================================
// Glow — blur + brightness/contrast + screen blend
// ============================================================

void effectGlow(const Surface& src, Surface& dst, int radius, int brightness, int contrast) {
    radius = std::clamp(radius, 1, 20);
    int w = src.width(), h = src.height();

    Surface blurred(w, h);
    effectGaussianBlur(src, blurred, radius);

    Surface adjusted(w, h);
    adjustBrightnessContrast(blurred, adjusted, brightness, contrast);

    // Screen blend: dst = src + blurred - src*blurred/255
    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            const ColorBgra* blurRow = adjusted.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                dstRow[x] = ColorBgra::fromBgra(
                    ColorBgra::clampToByte(srcRow[x].b + blurRow[x].b - srcRow[x].b * blurRow[x].b / 255),
                    ColorBgra::clampToByte(srcRow[x].g + blurRow[x].g - srcRow[x].g * blurRow[x].g / 255),
                    ColorBgra::clampToByte(srcRow[x].r + blurRow[x].r - srcRow[x].r * blurRow[x].r / 255),
                    srcRow[x].a);
            }
        }
    });
}

// ============================================================
// Sharpen — local histogram median, lerp(src, median, -0.5)
// ============================================================

void effectSharpen(const Surface& src, Surface& dst, int amount) {
    amount = std::clamp(amount, 1, 20);
    int w = src.width(), h = src.height();
    int rad = amount;
    // Circular cutoff matching Paint.NET: ((2*rad+1)^2 + 2) / 4
    int side = 2 * rad + 1;
    int cutoff = (side * side + 2) / 4;

    // Precompute horizontal extent for each vertical offset
    std::vector<int> extents(rad + 1);
    for (int v = 0; v <= rad; ++v)
        extents[v] = static_cast<int>(std::sqrt(static_cast<double>(cutoff - v * v)));

    parallelRows(h, [&](int yStart, int yEnd) {
        std::array<int, 256> histR{}, histG{}, histB{}, histA{};

        auto findMedian = [](const std::array<int, 256>& hist, int total) -> uint8_t {
            int target = total / 2;
            int cumul = 0;
            for (int i = 0; i < 256; ++i) {
                cumul += hist[i];
                if (cumul > target) return static_cast<uint8_t>(i);
            }
            return 255;
        };

        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = dst.rowPtr(y);
            const ColorBgra* srcRow = src.rowPtr(y);

            std::fill(histR.begin(), histR.end(), 0);
            std::fill(histG.begin(), histG.end(), 0);
            std::fill(histB.begin(), histB.end(), 0);
            std::fill(histA.begin(), histA.end(), 0);
            int count = 0;

            // Build initial histogram for x=0 using circular neighborhood
            for (int v = -rad; v <= rad; ++v) {
                int sy = y + v;
                if (sy < 0 || sy >= h) continue;
                const ColorBgra* row = src.rowPtr(sy);
                int ext = extents[std::abs(v)];
                for (int u = -ext; u <= ext; ++u) {
                    int sx = u; // center is at x=0
                    if (sx < 0 || sx >= w) continue;
                    histR[row[sx].r]++; histG[row[sx].g]++;
                    histB[row[sx].b]++; histA[row[sx].a]++;
                    ++count;
                }
            }

            auto emitPixel = [&](int x) {
                uint8_t medR = findMedian(histR, count);
                uint8_t medG = findMedian(histG, count);
                uint8_t medB = findMedian(histB, count);
                uint8_t medA = findMedian(histA, count);
                // Lerp(src, median, -0.5) = src + (src - median) * 0.5
                dstRow[x] = ColorBgra::fromBgraClamped(
                    srcRow[x].b + (srcRow[x].b - medB) / 2,
                    srcRow[x].g + (srcRow[x].g - medG) / 2,
                    srcRow[x].r + (srcRow[x].r - medR) / 2,
                    srcRow[x].a + (srcRow[x].a - medA) / 2);
            };

            emitPixel(0);

            for (int x = 1; x < w; ++x) {
                for (int v = -rad; v <= rad; ++v) {
                    int sy = y + v;
                    if (sy < 0 || sy >= h) continue;
                    const ColorBgra* row = src.rowPtr(sy);
                    int ext = extents[std::abs(v)];

                    // Remove pixel leaving the circle (was at old left edge)
                    int removeX = (x - 1) - ext;
                    if (removeX >= 0 && removeX < w) {
                        histR[row[removeX].r]--; histG[row[removeX].g]--;
                        histB[row[removeX].b]--; histA[row[removeX].a]--;
                        --count;
                    }

                    // Add pixel entering the circle (new right edge)
                    int addX = x + ext;
                    if (addX >= 0 && addX < w) {
                        histR[row[addX].r]++; histG[row[addX].g]++;
                        histB[row[addX].b]++; histA[row[addX].a]++;
                        ++count;
                    }
                }
                emitPixel(x);
            }
        }
    });
}

// ============================================================
// Pencil Sketch — blur + invert + desaturate + color dodge
// ============================================================

void effectPencilSketch(const Surface& src, Surface& dst, int pencilSize, int range) {
    pencilSize = std::clamp(pencilSize, 1, 20);
    range = std::clamp(range, -20, 20);
    int w = src.width(), h = src.height();

    // 1. Gaussian blur src → dst (used as working buffer)
    effectGaussianBlur(src, dst, pencilSize);

    // 2. Brightness/contrast adjustment (contrast = -range, per Paint.NET)
    Surface adjusted(w, h);
    adjustBrightnessContrast(dst, adjusted, range, -range);

    // 3. Invert
    adjustInvertColors(adjusted);

    // 4. Grayscale
    adjustGrayscale(adjusted);

    // 5. Color dodge blend: desaturate(src) dodged with processed sketch
    //    ColorDodge(A, B) = min(255, A * 255 / (255 - B))
    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            const ColorBgra* sketchRow = adjusted.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                // Desaturate source pixel
                uint8_t grey = srcRow[x].getIntensityByte();
                int b = sketchRow[x].r; // grayscale, so r==g==b
                if (b == 255) {
                    dstRow[x] = ColorBgra::fromBgra(255, 255, 255, srcRow[x].a);
                } else {
                    int dodged = std::min(255, grey * 255 / (255 - b));
                    dstRow[x] = ColorBgra::fromBgra(
                        static_cast<uint8_t>(dodged),
                        static_cast<uint8_t>(dodged),
                        static_cast<uint8_t>(dodged),
                        srcRow[x].a);
                }
            }
        }
    });
}

// ============================================================
// Ink Sketch — glow + convolution + threshold + darken
// ============================================================

void effectInkSketch(const Surface& src, Surface& dst, int inkOutline, int coloring) {
    inkOutline = std::clamp(inkOutline, 0, 99);
    coloring = std::clamp(coloring, 0, 100);
    int w = src.width(), h = src.height();

    // Compute brightness/contrast from coloring (matching Paint.NET)
    int adjustment = -(coloring - 50) * 2;

    // 1. Glow (radius=6 per Paint.NET)
    Surface glowed(w, h);
    effectGlow(src, glowed, 6, adjustment, adjustment);

    // 2. 5×5 convolution on src for edge detection
    // Center=30, bottom-center=-5, rest=-1
    Surface convolved(w, h);
    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* dstRow = convolved.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                double sumR = 0, sumG = 0, sumB = 0;
                for (int ky = -2; ky <= 2; ++ky) {
                    int sy = std::clamp(y + ky, 0, h - 1);
                    const ColorBgra* srcRow2 = src.rowPtr(sy);
                    for (int kx = -2; kx <= 2; ++kx) {
                        int sx = std::clamp(x + kx, 0, w - 1);
                        double wt;
                        if (kx == 0 && ky == 0) wt = 30.0;
                        else if (kx == 0 && ky == 2) wt = -5.0;
                        else wt = -1.0;
                        sumR += srcRow2[sx].r * wt;
                        sumG += srcRow2[sx].g * wt;
                        sumB += srcRow2[sx].b * wt;
                    }
                }
                dstRow[x] = ColorBgra::fromBgraClamped(
                    static_cast<int>(sumB), static_cast<int>(sumG),
                    static_cast<int>(sumR), static_cast<int>(src.getPoint(x, y).a));
            }
        }
    });

    // 3. Desaturate + threshold
    int threshold = inkOutline * 255 / 100;
    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* row = convolved.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                uint8_t intensity = row[x].getIntensityByte();
                uint8_t v = (intensity > threshold) ? 255 : 0;
                row[x] = ColorBgra::fromBgra(v, v, v, row[x].a);
            }
        }
    });

    // 4. Darken blend: dst = min(glowed, thresholded)
    parallelRows(h, [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* glowRow = glowed.rowPtr(y);
            const ColorBgra* convRow = convolved.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                dstRow[x] = ColorBgra::fromBgra(
                    std::min(glowRow[x].b, convRow[x].b),
                    std::min(glowRow[x].g, convRow[x].g),
                    std::min(glowRow[x].r, convRow[x].r),
                    glowRow[x].a);
            }
        }
    });
}

} // namespace paintnux
