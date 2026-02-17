#include "core/blendops.h"

#include <memory>

namespace paintnux {

// --- BinaryPixelOp default scanline implementations ---

void BinaryPixelOp::apply(ColorBgra* dst, const ColorBgra* src, int length) const {
    for (int i = 0; i < length; ++i) {
        dst[i] = apply(dst[i], src[i]);
    }
}

void BinaryPixelOp::apply(ColorBgra* dst, const ColorBgra* lhs, const ColorBgra* rhs, int length) const {
    for (int i = 0; i < length; ++i) {
        dst[i] = apply(lhs[i], rhs[i]);
    }
}

// --- NormalBlendOp ---

ColorBgra NormalBlendOp::apply(ColorBgra lhs, ColorBgra rhs) const {
    // Fast paths
    if (rhs.a == 255) return rhs;          // Fully opaque foreground
    if (rhs.a == 0) return lhs;            // Fully transparent foreground

    // y = lhsA * (255 - rhsA) / 255  (background's contribution weight)
    int y = ColorBgra::fastScaleByteByByte(lhs.a, 255 - rhs.a);
    int totalA = y + rhs.a;

    if (totalA == 0) return ColorBgra::transparent();

    int rhsA = rhs.a;

    // For Normal blend, F(A,B) = B, so the formula simplifies to:
    // result.C = (lhs.C * y + rhs.C * rhsA) / totalA
    uint8_t newB = static_cast<uint8_t>((lhs.b * y + rhs.b * rhsA + totalA / 2) / totalA);
    uint8_t newG = static_cast<uint8_t>((lhs.g * y + rhs.g * rhsA + totalA / 2) / totalA);
    uint8_t newR = static_cast<uint8_t>((lhs.r * y + rhs.r * rhsA + totalA / 2) / totalA);

    return ColorBgra::fromBgra(newB, newG, newR, static_cast<uint8_t>(totalA));
}

void NormalBlendOp::apply(ColorBgra* dst, const ColorBgra* src, int length) const {
    for (int i = 0; i < length; ++i) {
        dst[i] = apply(dst[i], src[i]);
    }
}

std::unique_ptr<BinaryPixelOp> NormalBlendOp::createWithOpacity(uint8_t opacity) const {
    if (opacity == 255) {
        return std::make_unique<NormalBlendOp>();
    }
    return std::make_unique<NormalBlendOpWithOpacity>(opacity);
}

// --- NormalBlendOpWithOpacity ---

ColorBgra NormalBlendOpWithOpacity::apply(ColorBgra lhs, ColorBgra rhs) const {
    uint8_t scaledA = ColorBgra::fastScaleByteByByte(rhs.a, m_opacity);

    if (scaledA == 0) return lhs;

    int y = ColorBgra::fastScaleByteByByte(lhs.a, 255 - scaledA);
    int totalA = y + scaledA;

    if (totalA == 0) return ColorBgra::transparent();

    uint8_t newB = static_cast<uint8_t>((lhs.b * y + rhs.b * scaledA + totalA / 2) / totalA);
    uint8_t newG = static_cast<uint8_t>((lhs.g * y + rhs.g * scaledA + totalA / 2) / totalA);
    uint8_t newR = static_cast<uint8_t>((lhs.r * y + rhs.r * scaledA + totalA / 2) / totalA);

    return ColorBgra::fromBgra(newB, newG, newR, static_cast<uint8_t>(totalA));
}

void NormalBlendOpWithOpacity::apply(ColorBgra* dst, const ColorBgra* src, int length) const {
    const int opacity = m_opacity;
    for (int i = 0; i < length; ++i) {
        ColorBgra rhs = src[i];
        if (rhs.a == 0) continue;

        int scaledA = ColorBgra::fastScaleByteByByte(rhs.a, opacity);
        if (scaledA == 0) continue;

        ColorBgra lhs = dst[i];
        int y = ColorBgra::fastScaleByteByByte(lhs.a, 255 - scaledA);
        int totalA = y + scaledA;

        dst[i] = ColorBgra::fromBgra(
            static_cast<uint8_t>((lhs.b * y + rhs.b * scaledA + totalA / 2) / totalA),
            static_cast<uint8_t>((lhs.g * y + rhs.g * scaledA + totalA / 2) / totalA),
            static_cast<uint8_t>((lhs.r * y + rhs.r * scaledA + totalA / 2) / totalA),
            static_cast<uint8_t>(totalA));
    }
}

// --- BlendMode utilities ---

const char* blendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal:     return "Normal";
    case BlendMode::Multiply:   return "Multiply";
    case BlendMode::Additive:   return "Additive";
    case BlendMode::ColorBurn:  return "Color Burn";
    case BlendMode::ColorDodge: return "Color Dodge";
    case BlendMode::Reflect:    return "Reflect";
    case BlendMode::Glow:       return "Glow";
    case BlendMode::Overlay:    return "Overlay";
    case BlendMode::Difference: return "Difference";
    case BlendMode::Negation:   return "Negation";
    case BlendMode::Lighten:    return "Lighten";
    case BlendMode::Darken:     return "Darken";
    case BlendMode::Screen:     return "Screen";
    case BlendMode::Xor:        return "Xor";
    default:                    return "Unknown";
    }
}

std::unique_ptr<UserBlendOp> createBlendOp(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal:     return std::make_unique<NormalBlendOp>();
    case BlendMode::Multiply:   return std::make_unique<GenericBlendOp<BlendFuncMultiply>>();
    case BlendMode::Additive:   return std::make_unique<GenericBlendOp<BlendFuncAdditive>>();
    case BlendMode::ColorBurn:  return std::make_unique<GenericBlendOp<BlendFuncColorBurn>>();
    case BlendMode::ColorDodge: return std::make_unique<GenericBlendOp<BlendFuncColorDodge>>();
    case BlendMode::Reflect:    return std::make_unique<GenericBlendOp<BlendFuncReflect>>();
    case BlendMode::Glow:       return std::make_unique<GenericBlendOp<BlendFuncGlow>>();
    case BlendMode::Overlay:    return std::make_unique<GenericBlendOp<BlendFuncOverlay>>();
    case BlendMode::Difference: return std::make_unique<GenericBlendOp<BlendFuncDifference>>();
    case BlendMode::Negation:   return std::make_unique<GenericBlendOp<BlendFuncNegation>>();
    case BlendMode::Lighten:    return std::make_unique<GenericBlendOp<BlendFuncLighten>>();
    case BlendMode::Darken:     return std::make_unique<GenericBlendOp<BlendFuncDarken>>();
    case BlendMode::Screen:     return std::make_unique<GenericBlendOp<BlendFuncScreen>>();
    case BlendMode::Xor:        return std::make_unique<GenericBlendOp<BlendFuncXor>>();
    default:                    return std::make_unique<NormalBlendOp>();
    }
}

} // namespace paintnux
