#pragma once

#include "core/colorbgra.h"
#include "core/surface.h"

namespace paintnux {

// --- In-place adjustments (for instant-apply / final commit) ---

void adjustInvertColors(Surface& surface);
void adjustGrayscale(Surface& surface);
void adjustSepia(Surface& surface);
void adjustAutoLevel(Surface& surface);

// --- Source→destination adjustments (for live preview, avoids copy+transform) ---

/// Brightness/contrast. brightness: -100..+100, contrast: -100..+100.
void adjustBrightnessContrast(const Surface& src, Surface& dst, int brightness, int contrast);

/// Hue/saturation/lightness. hue: -180..+180, saturation: 0..200, lightness: -100..+100.
void adjustHueSaturationLightness(const Surface& src, Surface& dst, int hue, int saturation, int lightness);

/// Posterize per channel. levels: 2..64 each.
void adjustPosterize(const Surface& src, Surface& dst, int redLevels, int greenLevels, int blueLevels);

/// Levels with per-channel input/output black/white points and gamma.
void adjustLevels(const Surface& src, Surface& dst,
                  ColorBgra inputBlack, ColorBgra inputWhite,
                  ColorBgra outputBlack, ColorBgra outputWhite,
                  float gammaR, float gammaG, float gammaB);

} // namespace paintnux
