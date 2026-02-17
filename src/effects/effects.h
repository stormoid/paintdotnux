#pragma once

#include "core/surface.h"

namespace paintnux {

// --- Blurs ---
void effectGaussianBlur(const Surface& src, Surface& dst, int radius);
void effectMotionBlur(const Surface& src, Surface& dst, double angle, int distance);
void effectUnfocus(const Surface& src, Surface& dst, int radius);

// --- Noise ---
void effectAddNoise(const Surface& src, Surface& dst, int intensity, int saturation, int coverage);
void effectMedian(const Surface& src, Surface& dst, int radius, int percentile);

// --- Distort ---
void effectPixelate(const Surface& src, Surface& dst, int cellSize);

// --- Stylize ---
void effectEdgeDetect(const Surface& src, Surface& dst, double angle);
void effectEmboss(const Surface& src, Surface& dst, double angle);
void effectRelief(const Surface& src, Surface& dst, double angle);
void effectOutline(const Surface& src, Surface& dst, int intensity, int radius);

// --- Photo ---
void effectGlow(const Surface& src, Surface& dst, int radius, int brightness, int contrast);
void effectSharpen(const Surface& src, Surface& dst, int amount);

// --- Artistic ---
void effectOilPainting(const Surface& src, Surface& dst, int brushSize, int coarseness);
void effectPencilSketch(const Surface& src, Surface& dst, int pencilSize, int range);
void effectInkSketch(const Surface& src, Surface& dst, int inkOutline, int coloring);

} // namespace paintnux
