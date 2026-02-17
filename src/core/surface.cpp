#include "core/surface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace paintnux {

Surface::Surface(int width, int height)
    : m_image(width, height, QImage::Format_ARGB32) {
    m_image.fill(0); // transparent black
}

Surface::Surface(QImage image) {
    if (image.format() != QImage::Format_ARGB32) {
        m_image = image.convertToFormat(QImage::Format_ARGB32);
    } else {
        m_image = std::move(image);
    }
}

Surface::Surface(const Surface& other)
    : m_image(other.m_image.copy()) {
}

Surface& Surface::operator=(const Surface& other) {
    if (this != &other) {
        m_image = other.m_image.copy();
    }
    return *this;
}

ColorBgra Surface::getPoint(int x, int y) const {
    if (!isVisible(x, y)) {
        return ColorBgra::transparent();
    }
    return *pixelPtr(x, y);
}

void Surface::setPoint(int x, int y, ColorBgra color) {
    if (isVisible(x, y)) {
        *pixelPtr(x, y) = color;
    }
}

void Surface::clear(ColorBgra color) {
    if (color.bgra == 0) {
        m_image.fill(0);
        return;
    }
    for (int y = 0; y < height(); ++y) {
        ColorBgra* row = rowPtr(y);
        for (int x = 0; x < width(); ++x) {
            row[x] = color;
        }
    }
}

void Surface::clear(const QRect& rect, ColorBgra color) {
    QRect clipped = rect.intersected(bounds());
    if (clipped.isEmpty()) return;

    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        ColorBgra* row = rowPtr(y);
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
            row[x] = color;
        }
    }
}

void Surface::copySurface(const Surface& source) {
    copySurface(source, QPoint(0, 0), source.bounds());
}

void Surface::copySurface(const Surface& source, QPoint dstOffset, const QRect& sourceRoi) {
    QRect srcRect = sourceRoi.intersected(source.bounds());
    if (srcRect.isEmpty()) return;

    // Clip to destination bounds
    int dx = dstOffset.x();
    int dy = dstOffset.y();

    int copyW = std::min(srcRect.width(), width() - dx);
    int copyH = std::min(srcRect.height(), height() - dy);
    if (copyW <= 0 || copyH <= 0) return;

    for (int row = 0; row < copyH; ++row) {
        const ColorBgra* srcRow = source.rowPtr(srcRect.top() + row) + srcRect.left();
        ColorBgra* dstRow = rowPtr(dy + row) + dx;
        std::memcpy(dstRow, srcRow, copyW * sizeof(ColorBgra));
    }
}

Surface Surface::clone() const {
    return Surface(*this);
}

ColorBgra Surface::getBilinearSample(float x, float y) const {
    if (x < 0 || y < 0 || x >= width() || y >= height()) {
        return ColorBgra::fromUInt32(0);
    }

    int ix = static_cast<int>(x);
    int iy = static_cast<int>(y);

    float fracX = x - ix;
    float fracY = y - iy;

    int sxfrac = static_cast<int>(fracX * 256.0f);
    int syfrac = static_cast<int>(fracY * 256.0f);
    int sxfracinv = 256 - sxfrac;
    int syfracinv = 256 - syfrac;

    uint32_t wul = static_cast<uint32_t>(sxfracinv * syfracinv); // top-left
    uint32_t wur = static_cast<uint32_t>(sxfrac * syfracinv);    // top-right
    uint32_t wll = static_cast<uint32_t>(sxfracinv * syfrac);    // bottom-left
    uint32_t wlr = static_cast<uint32_t>(sxfrac * syfrac);       // bottom-right

    int right = (ix < width() - 1) ? ix + 1 : ix;
    int bottom = (iy < height() - 1) ? iy + 1 : iy;

    ColorBgra cul = *pixelPtr(ix, iy);
    ColorBgra cur = *pixelPtr(right, iy);
    ColorBgra cll = *pixelPtr(ix, bottom);
    ColorBgra clr = *pixelPtr(right, bottom);

    return ColorBgra::blendColors4W16IP(cul, wul, cur, wur, cll, wll, clr, wlr);
}

ColorBgra Surface::getBilinearSampleClamped(float x, float y) const {
    x = std::clamp(x, 0.0f, static_cast<float>(width() - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height() - 1));
    return getBilinearSample(x, y);
}

// --- Resampling ---

static inline double cubeClamped(double x) {
    return x >= 0 ? x * x * x : 0;
}

/// Bicubic basis function R(x) per Paul Bourke.
static inline double bicubicR(double x) {
    return (cubeClamped(x + 2) - 4 * cubeClamped(x + 1) + 6 * cubeClamped(x) - 4 * cubeClamped(x - 1)) / 6.0;
}

static void nearestNeighborFit(Surface& dst, const Surface& src) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();
    for (int dy = 0; dy < dh; ++dy) {
        int sy = (dy * sh) / dh;
        const ColorBgra* srcRow = src.rowPtr(sy);
        ColorBgra* dstRow = dst.rowPtr(dy);
        for (int dx = 0; dx < dw; ++dx) {
            int sx = (dx * sw) / dw;
            dstRow[dx] = srcRow[sx];
        }
    }
}

static void bilinearFit(Surface& dst, const Surface& src) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();
    if (dw < 2 || dh < 2 || sw < 2 || sh < 2) {
        nearestNeighborFit(dst, src);
        return;
    }
    for (int dy = 0; dy < dh; ++dy) {
        float srcRow = static_cast<float>(dy * (sh - 1)) / static_cast<float>(dh - 1);
        ColorBgra* dstRow = dst.rowPtr(dy);
        for (int dx = 0; dx < dw; ++dx) {
            float srcCol = static_cast<float>(dx * (sw - 1)) / static_cast<float>(dw - 1);
            dstRow[dx] = src.getBilinearSample(srcCol, srcRow);
        }
    }
}

static void bicubicFitChecked(Surface& dst, const Surface& src, int dstLeft, int dstTop, int dstRight, int dstBottom) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();

    // Precompute column R() weights
    int roiW = dstRight - dstLeft;
    std::vector<double> rColCache(4 * roiW);
    for (int dx = dstLeft; dx < dstRight; ++dx) {
        double srcCol = static_cast<double>(dx * (sw - 1)) / static_cast<double>(dw - 1);
        double srcColFrac = srcCol - std::floor(srcCol);
        for (int m = -1; m <= 2; ++m) {
            rColCache[(m + 1) + (dx - dstLeft) * 4] = bicubicR(m - srcColFrac);
        }
    }

    double rRowCache[4];
    for (int dy = dstTop; dy < dstBottom; ++dy) {
        double srcRow = static_cast<double>(dy * (sh - 1)) / static_cast<double>(dh - 1);
        double srcRowFrac = srcRow - std::floor(srcRow);
        int srcRowInt = static_cast<int>(srcRow);

        for (int n = -1; n <= 2; ++n) {
            rRowCache[n + 1] = bicubicR(srcRowFrac - n);
        }

        ColorBgra* dstPtr = dst.rowPtr(dy) + dstLeft;
        for (int dx = dstLeft; dx < dstRight; ++dx) {
            double srcCol = static_cast<double>(dx * (sw - 1)) / static_cast<double>(dw - 1);
            int srcColInt = static_cast<int>(srcCol);

            double bSum = 0, gSum = 0, rSum = 0, aSum = 0, wTotal = 0;
            for (int n = -1; n <= 2; ++n) {
                int sy = srcRowInt + n;
                for (int m = -1; m <= 2; ++m) {
                    int sx = srcColInt + m;
                    if (src.isVisible(sx, sy)) {
                        double w0 = rColCache[(m + 1) + (dx - dstLeft) * 4];
                        double w1 = rRowCache[n + 1];
                        double w = w0 * w1;
                        const ColorBgra* sp = src.pixelPtr(sx, sy);
                        double a = sp->a;
                        bSum += sp->b * w * a;
                        gSum += sp->g * w * a;
                        rSum += sp->r * w * a;
                        aSum += a * w;
                        wTotal += w;
                    }
                }
            }

            double alpha = aSum / wTotal;
            double blue, green, red;
            if (alpha == 0) { blue = green = red = 0; }
            else {
                blue = bSum / aSum + 0.5;
                green = gSum / aSum + 0.5;
                red = rSum / aSum + 0.5;
                alpha += 0.5;
            }
            dstPtr->bgra = static_cast<uint32_t>(std::clamp(blue, 0.0, 255.0))
                         + (static_cast<uint32_t>(std::clamp(green, 0.0, 255.0)) << 8)
                         + (static_cast<uint32_t>(std::clamp(red, 0.0, 255.0)) << 16)
                         + (static_cast<uint32_t>(std::clamp(alpha, 0.0, 255.0)) << 24);
            ++dstPtr;
        }
    }
}

static void bicubicFitUnchecked(Surface& dst, const Surface& src, int dstLeft, int dstTop, int dstRight, int dstBottom) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();

    int roiW = dstRight - dstLeft;
    std::vector<double> rColCache(4 * roiW);
    for (int dx = dstLeft; dx < dstRight; ++dx) {
        double srcCol = static_cast<double>(dx * (sw - 1)) / static_cast<double>(dw - 1);
        double srcColFrac = srcCol - std::floor(srcCol);
        for (int m = -1; m <= 2; ++m) {
            rColCache[(m + 1) + (dx - dstLeft) * 4] = bicubicR(m - srcColFrac);
        }
    }

    double rRowCache[4];
    for (int dy = dstTop; dy < dstBottom; ++dy) {
        double srcRow = static_cast<double>(dy * (sh - 1)) / static_cast<double>(dh - 1);
        double srcRowFrac = srcRow - std::floor(srcRow);
        int srcRowInt = static_cast<int>(srcRow);

        for (int n = -1; n <= 2; ++n) {
            rRowCache[n + 1] = bicubicR(srcRowFrac - n);
        }

        const double* colPtr = rColCache.data();
        ColorBgra* dstPtr = dst.rowPtr(dy) + dstLeft;
        for (int dx = dstLeft; dx < dstRight; ++dx) {
            double srcCol = static_cast<double>(dx * (sw - 1)) / static_cast<double>(dw - 1);
            int srcColInt = static_cast<int>(srcCol);

            double bSum = 0, gSum = 0, rSum = 0, aSum = 0, wTotal = 0;
            for (int n = 0; n <= 3; ++n) {
                const ColorBgra* sp = src.pixelPtr(srcColInt - 1, srcRowInt - 1 + n);
                double w0 = colPtr[0] * rRowCache[n];
                double w1 = colPtr[1] * rRowCache[n];
                double w2 = colPtr[2] * rRowCache[n];
                double w3 = colPtr[3] * rRowCache[n];

                double a0 = sp[0].a, a1 = sp[1].a, a2 = sp[2].a, a3 = sp[3].a;
                aSum += a0 * w0 + a1 * w1 + a2 * w2 + a3 * w3;
                wTotal += w0 + w1 + w2 + w3;
                bSum += a0 * sp[0].b * w0 + a1 * sp[1].b * w1 + a2 * sp[2].b * w2 + a3 * sp[3].b * w3;
                gSum += a0 * sp[0].g * w0 + a1 * sp[1].g * w1 + a2 * sp[2].g * w2 + a3 * sp[3].g * w3;
                rSum += a0 * sp[0].r * w0 + a1 * sp[1].r * w1 + a2 * sp[2].r * w2 + a3 * sp[3].r * w3;
            }

            double alpha = aSum / wTotal;
            double blue, green, red;
            if (alpha == 0) { blue = green = red = 0; }
            else {
                blue = bSum / aSum + 0.5;
                green = gSum / aSum + 0.5;
                red = rSum / aSum + 0.5;
                alpha += 0.5;
            }
            dstPtr->bgra = static_cast<uint32_t>(std::clamp(blue, 0.0, 255.0))
                         + (static_cast<uint32_t>(std::clamp(green, 0.0, 255.0)) << 8)
                         + (static_cast<uint32_t>(std::clamp(red, 0.0, 255.0)) << 16)
                         + (static_cast<uint32_t>(std::clamp(alpha, 0.0, 255.0)) << 24);
            ++dstPtr;
            colPtr += 4;
        }
    }
}

static void bicubicFit(Surface& dst, const Surface& src) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();
    if (dw < 2 || dh < 2 || sw < 2 || sh < 2) {
        nearestNeighborFit(dst, src);
        return;
    }

    // Compute the safe interior rectangle where all 4x4 samples are in-bounds
    float leftF = static_cast<float>(dw - 1) / static_cast<float>(sw - 1);
    float topF = static_cast<float>(dh - 1) / static_cast<float>(sh - 1);
    float rightF = static_cast<float>((sw - 3) * (dw - 1)) / static_cast<float>(sw - 1);
    float bottomF = static_cast<float>((sh - 3) * (dh - 1)) / static_cast<float>(sh - 1);

    int left = static_cast<int>(std::ceil(leftF));
    int top = static_cast<int>(std::ceil(topF));
    int right = static_cast<int>(std::floor(rightF));
    int bottom = static_cast<int>(std::floor(bottomF));

    // Interior: fast unchecked path
    if (left < right && top < bottom) {
        bicubicFitUnchecked(dst, src, left, top, right, bottom);
    }

    // Border regions: checked path
    // Top strip
    if (top > 0)
        bicubicFitChecked(dst, src, 0, 0, dw, std::min(top, dh));
    // Left strip
    if (left > 0)
        bicubicFitChecked(dst, src, 0, top, std::min(left, dw), dh);
    // Right strip
    if (right < dw)
        bicubicFitChecked(dst, src, right, top, dw, dh);
    // Bottom strip
    if (bottom < dh && left < right)
        bicubicFitChecked(dst, src, left, bottom, right, dh);
}

static void superSamplingFit(Surface& dst, const Surface& src) {
    int dw = dst.width(), dh = dst.height();
    int sw = src.width(), sh = src.height();

    if (sw == dw && sh == dh) {
        dst.copySurface(src);
        return;
    }

    // SuperSampling is for downscaling; fall back to bicubic for upscaling
    if (sw <= dw || sh <= dh) {
        if (sw < 2 || sh < 2 || dw < 2 || dh < 2)
            nearestNeighborFit(dst, src);
        else
            bicubicFit(dst, src);
        return;
    }

    // Area-weighted downsampling with premultiplied alpha
    for (int dy = 0; dy < dh; ++dy) {
        double srcTop = static_cast<double>(dy * sh) / dh;
        double srcTopFloor = std::floor(srcTop);
        double srcTopWeight = 1.0 - (srcTop - srcTopFloor);
        int srcTopInt = static_cast<int>(srcTopFloor);

        double srcBottom = static_cast<double>((dy + 1) * sh) / dh;
        double srcBottomFloor = std::floor(srcBottom - 0.00001);
        double srcBottomWeight = srcBottom - srcBottomFloor;
        int srcBottomInt = static_cast<int>(srcBottomFloor);

        ColorBgra* dstPtr = dst.rowPtr(dy);

        for (int dx = 0; dx < dw; ++dx) {
            double srcLeft = static_cast<double>(dx * sw) / dw;
            double srcLeftFloor = std::floor(srcLeft);
            double srcLeftWeight = 1.0 - (srcLeft - srcLeftFloor);
            int srcLeftInt = static_cast<int>(srcLeftFloor);

            double srcRight = static_cast<double>((dx + 1) * sw) / dw;
            double srcRightFloor = std::floor(srcRight - 0.00001);
            double srcRightWeight = srcRight - srcRightFloor;
            int srcRightInt = static_cast<int>(srcRightFloor);

            double bSum = 0, gSum = 0, rSum = 0, aSum = 0;

            // Left fractional edge (interior rows)
            for (int sy = srcTopInt + 1; sy < srcBottomInt; ++sy) {
                const ColorBgra* sp = src.pixelPtr(srcLeftInt, sy);
                double a = sp->a;
                bSum += sp->b * srcLeftWeight * a;
                gSum += sp->g * srcLeftWeight * a;
                rSum += sp->r * srcLeftWeight * a;
                aSum += a * srcLeftWeight;
            }

            // Right fractional edge (interior rows)
            for (int sy = srcTopInt + 1; sy < srcBottomInt; ++sy) {
                const ColorBgra* sp = src.pixelPtr(srcRightInt, sy);
                double a = sp->a;
                bSum += sp->b * srcRightWeight * a;
                gSum += sp->g * srcRightWeight * a;
                rSum += sp->r * srcRightWeight * a;
                aSum += a * srcRightWeight;
            }

            // Top fractional edge (interior cols)
            for (int sx = srcLeftInt + 1; sx < srcRightInt; ++sx) {
                const ColorBgra* sp = src.pixelPtr(sx, srcTopInt);
                double a = sp->a;
                bSum += sp->b * srcTopWeight * a;
                gSum += sp->g * srcTopWeight * a;
                rSum += sp->r * srcTopWeight * a;
                aSum += a * srcTopWeight;
            }

            // Bottom fractional edge (interior cols)
            for (int sx = srcLeftInt + 1; sx < srcRightInt; ++sx) {
                const ColorBgra* sp = src.pixelPtr(sx, srcBottomInt);
                double a = sp->a;
                bSum += sp->b * srcBottomWeight * a;
                gSum += sp->g * srcBottomWeight * a;
                rSum += sp->r * srcBottomWeight * a;
                aSum += a * srcBottomWeight;
            }

            // Center area (full pixels)
            for (int sy = srcTopInt + 1; sy < srcBottomInt; ++sy) {
                const ColorBgra* sp = src.pixelPtr(srcLeftInt + 1, sy);
                for (int sx = srcLeftInt + 1; sx < srcRightInt; ++sx) {
                    double a = sp->a;
                    bSum += sp->b * a;
                    gSum += sp->g * a;
                    rSum += sp->r * a;
                    aSum += a;
                    ++sp;
                }
            }

            // Four corner pixels
            auto addCorner = [&](int cx, int cy, double weight) {
                ColorBgra c = src.getPoint(cx, cy);
                double a = c.a;
                bSum += c.b * weight * a;
                gSum += c.g * weight * a;
                rSum += c.r * weight * a;
                aSum += a * weight;
            };
            addCorner(srcLeftInt, srcTopInt, srcTopWeight * srcLeftWeight);
            addCorner(srcRightInt, srcTopInt, srcTopWeight * srcRightWeight);
            addCorner(srcLeftInt, srcBottomInt, srcBottomWeight * srcLeftWeight);
            addCorner(srcRightInt, srcBottomInt, srcBottomWeight * srcRightWeight);

            double area = (srcRight - srcLeft) * (srcBottom - srcTop);
            double alpha = aSum / area;
            double blue, green, red;
            if (alpha == 0) { blue = green = red = 0; }
            else {
                blue = bSum / aSum;
                green = gSum / aSum;
                red = rSum / aSum;
            }

            dstPtr->bgra = static_cast<uint32_t>(std::clamp(blue + 0.5, 0.0, 255.0))
                         + (static_cast<uint32_t>(std::clamp(green + 0.5, 0.0, 255.0)) << 8)
                         + (static_cast<uint32_t>(std::clamp(red + 0.5, 0.0, 255.0)) << 16)
                         + (static_cast<uint32_t>(std::clamp(alpha + 0.5, 0.0, 255.0)) << 24);
            ++dstPtr;
        }
    }
}

void Surface::fitSurface(ResamplingAlgorithm algorithm, const Surface& source) {
    switch (algorithm) {
        case ResamplingAlgorithm::NearestNeighbor:
            nearestNeighborFit(*this, source);
            break;
        case ResamplingAlgorithm::Bilinear:
            bilinearFit(*this, source);
            break;
        case ResamplingAlgorithm::Bicubic:
            bicubicFit(*this, source);
            break;
        case ResamplingAlgorithm::SuperSampling:
            superSamplingFit(*this, source);
            break;
    }
}

void Surface::clearWithCheckerboard() {
    for (int y = 0; y < height(); ++y) {
        ColorBgra* row = rowPtr(y);
        for (int x = 0; x < width(); ++x) {
            uint8_t v = static_cast<uint8_t>(((((x ^ y) >> 3) & 1) * 8) + 191);
            row[x] = ColorBgra::fromBgr(v, v, v);
        }
    }
}

} // namespace paintnux
