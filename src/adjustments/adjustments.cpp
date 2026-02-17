#include "adjustments/adjustments.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

namespace paintnux {

// --- Helpers ---

// Run func(yStart, yEnd) across threads, splitting rows evenly.
static void parallelRows(int height, const std::function<void(int, int)>& func) {
    int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    if (height < numThreads * 16) {
        // Too small to benefit from threading
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

// Apply a per-channel LUT from src to dst in a single pass (threaded)
static void applyLUT(const Surface& src, Surface& dst,
                     const std::array<uint8_t, 256>& tableR,
                     const std::array<uint8_t, 256>& tableG,
                     const std::array<uint8_t, 256>& tableB) {
    int w = src.width();
    parallelRows(src.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                dstRow[x].r = tableR[srcRow[x].r];
                dstRow[x].g = tableG[srcRow[x].g];
                dstRow[x].b = tableB[srcRow[x].b];
                dstRow[x].a = srcRow[x].a;
            }
        }
    });
}

// Build a levels LUT for one channel
static std::array<uint8_t, 256> buildLevelsLUT(uint8_t inLo, uint8_t inHi,
                                                uint8_t outLo, uint8_t outHi,
                                                float gamma) {
    std::array<uint8_t, 256> table{};
    int range = inHi - inLo;
    if (range <= 0) range = 1;
    float invGamma = 1.0f / std::max(gamma, 0.01f);

    for (int i = 0; i < 256; ++i) {
        float v = static_cast<float>(i - inLo) / range;
        v = std::clamp(v, 0.0f, 1.0f);
        v = std::pow(v, invGamma);
        table[i] = ColorBgra::clampToByte(outLo + v * (outHi - outLo));
    }
    return table;
}

// Paint.NET CalcLevels algorithm for posterize
static std::array<uint8_t, 256> calcPosterizeLevels(int levelCount) {
    levelCount = std::clamp(levelCount, 2, 64);

    std::vector<uint8_t> t1(levelCount);
    for (int i = 0; i < levelCount; ++i)
        t1[i] = static_cast<uint8_t>((255 * i) / (levelCount - 1));

    std::array<uint8_t, 256> table{};
    int j = 0, k = 0;
    for (int i = 0; i < 256; ++i) {
        table[i] = t1[j];
        k += levelCount;
        if (k > 255) { k -= 255; ++j; }
    }
    return table;
}

// HSV helpers
struct Hsv { float h, s, v; };

static Hsv rgbToHsv(uint8_t r, uint8_t g, uint8_t b) {
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxC = std::max({rf, gf, bf});
    float minC = std::min({rf, gf, bf});
    float delta = maxC - minC;

    Hsv hsv{};
    hsv.v = maxC;
    if (delta < 1e-6f) {
        hsv.h = 0; hsv.s = 0;
    } else {
        hsv.s = delta / maxC;
        if (maxC == rf)
            hsv.h = 60.0f * std::fmod((gf - bf) / delta + 6.0f, 6.0f);
        else if (maxC == gf)
            hsv.h = 60.0f * ((bf - rf) / delta + 2.0f);
        else
            hsv.h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
    return hsv;
}

static void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s < 1e-6f) {
        r = g = b = ColorBgra::clampToByte(v * 255.0f);
        return;
    }
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;

    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float rf, gf, bf;
    if (h < 60)       { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }

    r = ColorBgra::clampToByte((rf + m) * 255.0f);
    g = ColorBgra::clampToByte((gf + m) * 255.0f);
    b = ColorBgra::clampToByte((bf + m) * 255.0f);
}

// --- In-place adjustments ---

void adjustInvertColors(Surface& surface) {
    int w = surface.width();
    parallelRows(surface.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* row = surface.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                row[x].r = 255 - row[x].r;
                row[x].g = 255 - row[x].g;
                row[x].b = 255 - row[x].b;
            }
        }
    });
}

void adjustGrayscale(Surface& surface) {
    int w = surface.width();
    parallelRows(surface.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* row = surface.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                uint8_t i = row[x].getIntensityByte();
                row[x].r = i; row[x].g = i; row[x].b = i;
            }
        }
    });
}

void adjustSepia(Surface& surface) {
    std::array<uint8_t, 256> tableR{}, tableG{}, tableB{};
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0;
        tableR[i] = ColorBgra::clampToByte(std::pow(v, 0.8) * 255.0);
        tableG[i] = static_cast<uint8_t>(i);
        tableB[i] = ColorBgra::clampToByte(std::pow(v, 1.2) * 255.0);
    }
    int w = surface.width();
    parallelRows(surface.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* row = surface.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                uint8_t i = row[x].getIntensityByte();
                row[x].r = tableR[i]; row[x].g = tableG[i]; row[x].b = tableB[i];
            }
        }
    });
}

void adjustAutoLevel(Surface& surface) {
    std::array<long, 256> histB{}, histG{}, histR{};
    for (int y = 0; y < surface.height(); ++y) {
        const ColorBgra* row = surface.rowPtr(y);
        for (int x = 0; x < surface.width(); ++x) {
            histB[row[x].b]++; histG[row[x].g]++; histR[row[x].r]++;
        }
    }

    auto getPercentile = [](const std::array<long, 256>& hist, float fraction) -> int {
        long sum = 0;
        for (int j = 0; j < 256; ++j) sum += hist[j];
        long integral = 0;
        for (int j = 0; j < 256; ++j) {
            integral += hist[j];
            if (integral > static_cast<long>(sum * fraction)) return j;
        }
        return 255;
    };

    auto getMean = [](const std::array<long, 256>& hist) -> float {
        long avg = 0, sum = 0;
        for (int j = 0; j < 256; ++j) { avg += j * hist[j]; sum += hist[j]; }
        return sum != 0 ? static_cast<float>(avg) / sum : 0.0f;
    };

    int loB = getPercentile(histB, 0.005f), loG = getPercentile(histG, 0.005f), loR = getPercentile(histR, 0.005f);
    int hiB = getPercentile(histB, 0.995f), hiG = getPercentile(histG, 0.995f), hiR = getPercentile(histR, 0.995f);
    float mdB = getMean(histB), mdG = getMean(histG), mdR = getMean(histR);

    auto computeGamma = [](int lo, float md, int hi) -> float {
        if (lo < md && md < hi) {
            double base = static_cast<double>(md - lo) / (hi - lo);
            return static_cast<float>(std::clamp(std::log(0.5) / std::log(base), 0.1, 10.0));
        }
        return 1.0f;
    };

    auto buildAutoLUT = [](int inLo, int inHi, float gamma) -> std::array<uint8_t, 256> {
        std::array<uint8_t, 256> table{};
        if (inHi <= inLo) {
            for (int i = 0; i < 256; ++i) table[i] = static_cast<uint8_t>(i);
            return table;
        }
        for (int i = 0; i < 256; ++i) {
            float v = static_cast<float>(i - inLo);
            if (v < 0) table[i] = 0;
            else if (i >= inHi) table[i] = 255;
            else table[i] = ColorBgra::clampToByte(255.0f * std::pow(v / (inHi - inLo), gamma));
        }
        return table;
    };

    auto tableB = buildAutoLUT(loB, hiB, computeGamma(loB, mdB, hiB));
    auto tableG = buildAutoLUT(loG, hiG, computeGamma(loG, mdG, hiG));
    auto tableR = buildAutoLUT(loR, hiR, computeGamma(loR, mdR, hiR));

    int w = surface.width();
    parallelRows(surface.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            ColorBgra* row = surface.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                row[x].r = tableR[row[x].r]; row[x].g = tableG[row[x].g]; row[x].b = tableB[row[x].b];
            }
        }
    });
}

// --- Source→destination adjustments ---

void adjustBrightnessContrast(const Surface& src, Surface& dst, int brightness, int contrast) {
    int multiply, divide;
    if (contrast < 0)      { multiply = contrast + 100; divide = 100; }
    else if (contrast > 0) { multiply = 100; divide = 100 - contrast; }
    else                   { multiply = 1; divide = 1; }

    int w = src.width();
    if (divide == 0) {
        // Hard threshold (contrast = 100)
        std::array<uint8_t, 256> threshTable{};
        for (int i = 0; i < 256; ++i)
            threshTable[i] = (i + brightness < 128) ? 0 : 255;

        parallelRows(src.height(), [&](int yStart, int yEnd) {
            for (int y = yStart; y < yEnd; ++y) {
                const ColorBgra* srcRow = src.rowPtr(y);
                ColorBgra* dstRow = dst.rowPtr(y);
                for (int x = 0; x < w; ++x) {
                    uint8_t c = threshTable[srcRow[x].getIntensityByte()];
                    dstRow[x] = ColorBgra::fromBgra(c, c, c, srcRow[x].a);
                }
            }
        });
    } else {
        // Build 256-entry shift table (one per intensity)
        std::array<int, 256> shiftTable{};
        for (int intensity = 0; intensity < 256; ++intensity) {
            if (divide == 100)
                shiftTable[intensity] = (intensity - 127) * multiply / divide + 127 - intensity + brightness;
            else
                shiftTable[intensity] = (intensity - 127 + brightness) * multiply / divide + 127 - intensity;
        }

        parallelRows(src.height(), [&](int yStart, int yEnd) {
            for (int y = yStart; y < yEnd; ++y) {
                const ColorBgra* srcRow = src.rowPtr(y);
                ColorBgra* dstRow = dst.rowPtr(y);
                for (int x = 0; x < w; ++x) {
                    int shift = shiftTable[srcRow[x].getIntensityByte()];
                    dstRow[x].r = ColorBgra::clampToByte(srcRow[x].r + shift);
                    dstRow[x].g = ColorBgra::clampToByte(srcRow[x].g + shift);
                    dstRow[x].b = ColorBgra::clampToByte(srcRow[x].b + shift);
                    dstRow[x].a = srcRow[x].a;
                }
            }
        });
    }
}

void adjustHueSaturationLightness(const Surface& src, Surface& dst,
                                   int hue, int saturation, int lightness) {
    int satFactor = saturation * 1024 / 100;
    int blendA = 0, blendC = 0;
    if (lightness > 0) { blendA = lightness * 255 / 100; blendC = 255; }
    else if (lightness < 0) { blendA = -lightness * 255 / 100; blendC = 0; }
    int invBlendA = 255 - blendA;

    int w = src.width();
    parallelRows(src.height(), [&](int yStart, int yEnd) {
        for (int y = yStart; y < yEnd; ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            ColorBgra* dstRow = dst.rowPtr(y);
            for (int x = 0; x < w; ++x) {
                int r = srcRow[x].r, g = srcRow[x].g, b = srcRow[x].b;

                if (saturation != 100) {
                    int intensity = (7471 * b + 38470 * g + 19595 * r) >> 16;
                    r = std::clamp((intensity * 1024 + (r - intensity) * satFactor) >> 10, 0, 255);
                    g = std::clamp((intensity * 1024 + (g - intensity) * satFactor) >> 10, 0, 255);
                    b = std::clamp((intensity * 1024 + (b - intensity) * satFactor) >> 10, 0, 255);
                }

                if (hue != 0) {
                    uint8_t ru = r, gu = g, bu = b;
                    Hsv hsv = rgbToHsv(ru, gu, bu);
                    hsv.h += static_cast<float>(hue);
                    hsvToRgb(hsv.h, hsv.s, hsv.v, ru, gu, bu);
                    r = ru; g = gu; b = bu;
                }

                if (lightness != 0) {
                    r = (r * invBlendA + blendC * blendA) / 256;
                    g = (g * invBlendA + blendC * blendA) / 256;
                    b = (b * invBlendA + blendC * blendA) / 256;
                }

                dstRow[x] = ColorBgra::fromBgra(static_cast<uint8_t>(b), static_cast<uint8_t>(g),
                                                  static_cast<uint8_t>(r), srcRow[x].a);
            }
        }
    });
}

void adjustPosterize(const Surface& src, Surface& dst,
                     int redLevels, int greenLevels, int blueLevels) {
    applyLUT(src, dst, calcPosterizeLevels(redLevels),
             calcPosterizeLevels(greenLevels), calcPosterizeLevels(blueLevels));
}

void adjustLevels(const Surface& src, Surface& dst,
                  ColorBgra inputBlack, ColorBgra inputWhite,
                  ColorBgra outputBlack, ColorBgra outputWhite,
                  float gammaR, float gammaG, float gammaB) {
    applyLUT(src, dst,
             buildLevelsLUT(inputBlack.r, inputWhite.r, outputBlack.r, outputWhite.r, gammaR),
             buildLevelsLUT(inputBlack.g, inputWhite.g, outputBlack.g, outputWhite.g, gammaG),
             buildLevelsLUT(inputBlack.b, inputWhite.b, outputBlack.b, outputWhite.b, gammaB));
}

} // namespace paintnux
