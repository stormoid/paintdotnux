#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace paintnux {

/// BGRA pixel struct matching Paint.NET's ColorBgra layout.
/// On little-endian x86_64, bytes are stored as [B, G, R, A] which maps
/// directly to QImage::Format_ARGB32's memory layout.
struct ColorBgra {
    union {
        struct {
            uint8_t b, g, r, a;
        };
        uint32_t bgra;
    };

    static constexpr int BlueChannel = 0;
    static constexpr int GreenChannel = 1;
    static constexpr int RedChannel = 2;
    static constexpr int AlphaChannel = 3;
    static constexpr int SizeOf = 4;

    // Factory methods
    [[nodiscard]] static constexpr ColorBgra fromBgra(uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
        ColorBgra c{};
        c.b = b;
        c.g = g;
        c.r = r;
        c.a = a;
        return c;
    }

    [[nodiscard]] static constexpr ColorBgra fromBgr(uint8_t b, uint8_t g, uint8_t r) {
        return fromBgra(b, g, r, 255);
    }

    [[nodiscard]] static constexpr ColorBgra fromUInt32(uint32_t value) {
        ColorBgra c{};
        c.bgra = value;
        return c;
    }

    [[nodiscard]] static constexpr ColorBgra transparent() {
        return fromUInt32(0);
    }

    [[nodiscard]] static constexpr ColorBgra black() {
        return fromBgra(0, 0, 0, 255);
    }

    [[nodiscard]] static constexpr ColorBgra white() {
        return fromBgra(255, 255, 255, 255);
    }

    // Clamped factory (int inputs)
    [[nodiscard]] static constexpr ColorBgra fromBgraClamped(int b, int g, int r, int a) {
        return fromBgra(clampToByte(b), clampToByte(g), clampToByte(r), clampToByte(a));
    }

    // Clamped factory (float inputs)
    [[nodiscard]] static ColorBgra fromBgraClamped(float b, float g, float r, float a) {
        return fromBgra(clampToByte(b), clampToByte(g), clampToByte(r), clampToByte(a));
    }

    // Channel access by index
    [[nodiscard]] constexpr uint8_t operator[](int channel) const {
        switch (channel) {
        case 0: return b;
        case 1: return g;
        case 2: return r;
        case 3: return a;
        default: return 0;
        }
    }

    // Create copy with different alpha
    [[nodiscard]] constexpr ColorBgra newAlpha(uint8_t newA) const {
        return fromBgra(b, g, r, newA);
    }

    // Intensity/Luminance (ITU-R BT.601 weights)
    [[nodiscard]] constexpr uint8_t getIntensityByte() const {
        return static_cast<uint8_t>((7471 * b + 38470 * g + 19595 * r) >> 16);
    }

    [[nodiscard]] constexpr double getIntensity() const {
        return (0.114 * b + 0.587 * g + 0.299 * r) / 255.0;
    }

    [[nodiscard]] constexpr uint8_t getMaxColorChannelValue() const {
        return std::max({b, g, r});
    }

    [[nodiscard]] constexpr uint8_t getAverageColorChannelValue() const {
        return static_cast<uint8_t>((b + g + r) / 3);
    }

    // Equality
    [[nodiscard]] constexpr bool operator==(const ColorBgra& other) const {
        return bgra == other.bgra;
    }

    [[nodiscard]] constexpr bool operator!=(const ColorBgra& other) const {
        return bgra != other.bgra;
    }

    // --- Blending helpers ---

    /// Fast (a * b + 128) >> 8, approximately a * b / 255 with rounding.
    [[nodiscard]] static constexpr uint8_t fastScaleByteByByte(uint8_t a, uint8_t b) {
        int i = a * b + 0x80;
        return static_cast<uint8_t>((i + (i >> 8)) >> 8);
    }

    /// Clamp int to [0, 255]
    [[nodiscard]] static constexpr uint8_t clampToByte(int x) {
        return static_cast<uint8_t>(std::clamp(x, 0, 255));
    }

    /// Clamp float to [0, 255]
    [[nodiscard]] static uint8_t clampToByte(float x) {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(x + 0.5f), 0, 255));
    }

    /// Clamp double to [0, 255]
    [[nodiscard]] static uint8_t clampToByte(double x) {
        return static_cast<uint8_t>(std::clamp(static_cast<int>(x + 0.5), 0, 255));
    }

    /// Lerp between two colors
    [[nodiscard]] static ColorBgra lerp(ColorBgra from, ColorBgra to, float frac) {
        return fromBgra(
            clampToByte(from.b + (to.b - from.b) * frac),
            clampToByte(from.g + (to.g - from.g) * frac),
            clampToByte(from.r + (to.r - from.r) * frac),
            clampToByte(from.a + (to.a - from.a) * frac));
    }

    /// Blend two colors: result = ca blended with cb at cbAlpha opacity
    [[nodiscard]] static constexpr ColorBgra blend(ColorBgra ca, ColorBgra cb, uint8_t cbAlpha) {
        uint8_t a2 = fastScaleByteByByte(cb.a, cbAlpha);
        uint8_t a1 = static_cast<uint8_t>(255 - a2);
        return fromBgra(
            static_cast<uint8_t>((ca.b * a1 + cb.b * a2) / 255),
            static_cast<uint8_t>((ca.g * a1 + cb.g * a2) / 255),
            static_cast<uint8_t>((ca.r * a1 + cb.r * a2) / 255),
            static_cast<uint8_t>((ca.a * a1 + a2) / 255));
    }

    /// Blend 4 colors with 16-bit fixed-point weights (sum = 65536)
    [[nodiscard]] static constexpr ColorBgra blendColors4W16IP(
        ColorBgra c1, uint32_t w1,
        ColorBgra c2, uint32_t w2,
        ColorBgra c3, uint32_t w3,
        ColorBgra c4, uint32_t w4) {
        return fromBgra(
            static_cast<uint8_t>((c1.b * w1 + c2.b * w2 + c3.b * w3 + c4.b * w4) >> 16),
            static_cast<uint8_t>((c1.g * w1 + c2.g * w2 + c3.g * w3 + c4.g * w4) >> 16),
            static_cast<uint8_t>((c1.r * w1 + c2.r * w2 + c3.r * w3 + c4.r * w4) >> 16),
            static_cast<uint8_t>((c1.a * w1 + c2.a * w2 + c3.a * w3 + c4.a * w4) >> 16));
    }

    /// Premultiply alpha
    [[nodiscard]] constexpr ColorBgra premultiply() const {
        return fromBgra(
            fastScaleByteByByte(b, a),
            fastScaleByteByByte(g, a),
            fastScaleByteByByte(r, a),
            a);
    }

    /// Unpremultiply alpha
    [[nodiscard]] ColorBgra unpremultiply() const {
        if (a == 0) return transparent();
        if (a == 255) return *this;
        float invA = 255.0f / a;
        return fromBgraClamped(b * invA, g * invA, r * invA, static_cast<float>(a));
    }
};

static_assert(sizeof(ColorBgra) == 4, "ColorBgra must be 4 bytes");

} // namespace paintnux
