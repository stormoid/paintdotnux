#pragma once

#include "core/colorbgra.h"

#include <QImage>
#include <QRect>
#include <QSize>

#include <memory>

namespace paintnux {

/// Resampling algorithms for Surface::fitSurface().
enum class ResamplingAlgorithm {
    NearestNeighbor,
    Bilinear,
    Bicubic,
    SuperSampling   // area-weighted for downscaling, falls back to bicubic for upscaling
};

/// Surface wraps a QImage (Format_ARGB32) and provides direct ColorBgra* pixel access.
/// On little-endian x86_64, QImage::Format_ARGB32 stores bytes as [B,G,R,A],
/// matching ColorBgra layout exactly.
class Surface {
public:
    /// Create a new surface with given dimensions, filled with transparent black.
    Surface(int width, int height);

    /// Create a surface wrapping an existing QImage (takes ownership via move).
    /// The image will be converted to Format_ARGB32 if necessary.
    explicit Surface(QImage image);

    /// Copy constructor (deep copy)
    Surface(const Surface& other);
    Surface& operator=(const Surface& other);

    /// Move constructor
    Surface(Surface&& other) noexcept = default;
    Surface& operator=(Surface&& other) noexcept = default;

    ~Surface() = default;

    [[nodiscard]] int width() const { return m_image.width(); }
    [[nodiscard]] int height() const { return m_image.height(); }
    [[nodiscard]] int stride() const { return m_image.bytesPerLine(); }
    [[nodiscard]] QSize size() const { return m_image.size(); }
    [[nodiscard]] QRect bounds() const { return QRect(0, 0, width(), height()); }

    /// Access underlying QImage (for Qt interop, rendering, etc.)
    [[nodiscard]] const QImage& qimage() const { return m_image; }
    [[nodiscard]] QImage& qimage() { return m_image; }

    // --- Pixel access ---

    /// Get pointer to start of row y (unchecked).
    [[nodiscard]] ColorBgra* rowPtr(int y) {
        return reinterpret_cast<ColorBgra*>(m_image.scanLine(y));
    }

    [[nodiscard]] const ColorBgra* rowPtr(int y) const {
        return reinterpret_cast<const ColorBgra*>(m_image.constScanLine(y));
    }

    /// Get pointer to pixel at (x, y) (unchecked).
    [[nodiscard]] ColorBgra* pixelPtr(int x, int y) {
        return rowPtr(y) + x;
    }

    [[nodiscard]] const ColorBgra* pixelPtr(int x, int y) const {
        return rowPtr(y) + x;
    }

    /// Get pixel value at (x, y) with bounds checking.
    [[nodiscard]] ColorBgra getPoint(int x, int y) const;

    /// Set pixel value at (x, y) with bounds checking.
    void setPoint(int x, int y, ColorBgra color);

    /// Check if coordinates are within bounds.
    [[nodiscard]] bool isVisible(int x, int y) const {
        return x >= 0 && x < width() && y >= 0 && y < height();
    }

    // --- Operations ---

    /// Fill entire surface with color.
    void clear(ColorBgra color);

    /// Fill rectangle with color.
    void clear(const QRect& rect, ColorBgra color);

    /// Copy pixels from source surface into this surface at (0,0).
    void copySurface(const Surface& source);

    /// Copy pixels from source surface ROI into this surface at dstOffset.
    void copySurface(const Surface& source, QPoint dstOffset, const QRect& sourceRoi);

    /// Deep clone of this surface.
    [[nodiscard]] Surface clone() const;

    // --- Bilinear sampling ---

    /// Bilinear sample with clamping (out-of-bounds returns transparent black).
    [[nodiscard]] ColorBgra getBilinearSample(float x, float y) const;

    /// Bilinear sample with clamping to edge colors.
    [[nodiscard]] ColorBgra getBilinearSampleClamped(float x, float y) const;

    // --- Resampling ---

    /// Fit the source surface into this surface using the given resampling algorithm.
    void fitSurface(ResamplingAlgorithm algorithm, const Surface& source);

    /// Fill with checkerboard pattern (for transparency visualization).
    void clearWithCheckerboard();

private:
    QImage m_image;
};

} // namespace paintnux
