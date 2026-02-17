#pragma once

#include "core/colorbgra.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace paintnux {

// ── BlendMode enum ──────────────────────────────────────────────────────────

enum class BlendMode : uint8_t {
    Normal, Multiply, Additive, ColorBurn, ColorDodge,
    Reflect, Glow, Overlay, Difference, Negation,
    Lighten, Darken, Screen, Xor,
    Count  // sentinel
};

/// Number of valid blend modes.
inline constexpr int blendModeCount() {
    return static_cast<int>(BlendMode::Count);
}

/// Human-readable name for a blend mode.
const char* blendModeName(BlendMode mode);

// ── Abstract base classes ───────────────────────────────────────────────────

/// Abstract base for binary pixel operations: F(lhs, rhs) -> result
class BinaryPixelOp {
public:
    virtual ~BinaryPixelOp() = default;

    /// Apply blend to a single pixel pair.
    [[nodiscard]] virtual ColorBgra apply(ColorBgra lhs, ColorBgra rhs) const = 0;

    /// Apply blend in-place: dst[i] = F(dst[i], src[i]) for length pixels.
    virtual void apply(ColorBgra* dst, const ColorBgra* src, int length) const;

    /// Apply blend to separate buffers: dst[i] = F(lhs[i], rhs[i]).
    virtual void apply(ColorBgra* dst, const ColorBgra* lhs, const ColorBgra* rhs, int length) const;
};

/// Abstract base for user-selectable blend modes.
class UserBlendOp : public BinaryPixelOp {
public:
    /// Create a version of this blend op with opacity factored in.
    [[nodiscard]] virtual std::unique_ptr<BinaryPixelOp> createWithOpacity(uint8_t opacity) const = 0;
};

// ── Blend function functors (F(A,B) per channel) ───────────────────────────

struct BlendFuncNormal {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(b); }
};

struct BlendFuncMultiply {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(a * b / 255); }
};

struct BlendFuncAdditive {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(std::min(255, a + b)); }
};

struct BlendFuncColorBurn {
    static uint8_t apply(int a, int b) {
        if (b == 0) return 0;
        int v = 255 - (255 - a) * 255 / b;
        return static_cast<uint8_t>(std::max(0, v));
    }
};

struct BlendFuncColorDodge {
    static uint8_t apply(int a, int b) {
        if (b == 255) return 255;
        int v = a * 255 / (255 - b);
        return static_cast<uint8_t>(std::min(255, v));
    }
};

struct BlendFuncReflect {
    static uint8_t apply(int a, int b) {
        if (b == 255) return 255;
        int v = a * a / (255 - b);
        return static_cast<uint8_t>(std::min(255, v));
    }
};

struct BlendFuncGlow {
    static uint8_t apply(int a, int b) {
        if (a == 255) return 255;
        int v = b * b / (255 - a);
        return static_cast<uint8_t>(std::min(255, v));
    }
};

struct BlendFuncOverlay {
    static uint8_t apply(int a, int b) {
        if (a < 128)
            return static_cast<uint8_t>(2 * a * b / 255);
        else
            return static_cast<uint8_t>(255 - 2 * (255 - a) * (255 - b) / 255);
    }
};

struct BlendFuncDifference {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(std::abs(b - a)); }
};

struct BlendFuncNegation {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(255 - std::abs(255 - a - b)); }
};

struct BlendFuncLighten {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(std::max(a, b)); }
};

struct BlendFuncDarken {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(std::min(a, b)); }
};

struct BlendFuncScreen {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(a + b - a * b / 255); }
};

struct BlendFuncXor {
    static uint8_t apply(int a, int b) { return static_cast<uint8_t>(a ^ b); }
};

// ── GenericBlendOp template ─────────────────────────────────────────────────

template <typename BlendFunc>
class GenericBlendOpWithOpacity;

/// Generic blend op using the generalized compositing formula.
template <typename BlendFunc>
class GenericBlendOp : public UserBlendOp {
public:
    [[nodiscard]] ColorBgra apply(ColorBgra lhs, ColorBgra rhs) const override {
        if (rhs.a == 0) return lhs;

        int y = ColorBgra::fastScaleByteByByte(lhs.a, 255 - rhs.a);
        int totalA = y + rhs.a;
        if (totalA == 0) return ColorBgra::transparent();

        int x = ColorBgra::fastScaleByteByByte(lhs.a, rhs.a);  // overlap weight
        int z = rhs.a - x;  // foreground-only weight

        uint8_t newB = static_cast<uint8_t>((lhs.b * y + rhs.b * z + BlendFunc::apply(lhs.b, rhs.b) * x + totalA / 2) / totalA);
        uint8_t newG = static_cast<uint8_t>((lhs.g * y + rhs.g * z + BlendFunc::apply(lhs.g, rhs.g) * x + totalA / 2) / totalA);
        uint8_t newR = static_cast<uint8_t>((lhs.r * y + rhs.r * z + BlendFunc::apply(lhs.r, rhs.r) * x + totalA / 2) / totalA);

        return ColorBgra::fromBgra(newB, newG, newR, static_cast<uint8_t>(totalA));
    }

    [[nodiscard]] std::unique_ptr<BinaryPixelOp> createWithOpacity(uint8_t opacity) const override {
        if (opacity == 255)
            return std::make_unique<GenericBlendOp<BlendFunc>>();
        return std::make_unique<GenericBlendOpWithOpacity<BlendFunc>>(opacity);
    }
};

/// Generic blend op with pre-applied opacity on source alpha.
template <typename BlendFunc>
class GenericBlendOpWithOpacity : public BinaryPixelOp {
public:
    explicit GenericBlendOpWithOpacity(uint8_t opacity) : m_opacity(opacity) {}

    [[nodiscard]] ColorBgra apply(ColorBgra lhs, ColorBgra rhs) const override {
        uint8_t scaledA = ColorBgra::fastScaleByteByByte(rhs.a, m_opacity);
        if (scaledA == 0) return lhs;

        int y = ColorBgra::fastScaleByteByByte(lhs.a, 255 - scaledA);
        int totalA = y + scaledA;
        if (totalA == 0) return ColorBgra::transparent();

        int x = ColorBgra::fastScaleByteByByte(lhs.a, scaledA);
        int z = scaledA - x;

        uint8_t newB = static_cast<uint8_t>((lhs.b * y + rhs.b * z + BlendFunc::apply(lhs.b, rhs.b) * x + totalA / 2) / totalA);
        uint8_t newG = static_cast<uint8_t>((lhs.g * y + rhs.g * z + BlendFunc::apply(lhs.g, rhs.g) * x + totalA / 2) / totalA);
        uint8_t newR = static_cast<uint8_t>((lhs.r * y + rhs.r * z + BlendFunc::apply(lhs.r, rhs.r) * x + totalA / 2) / totalA);

        return ColorBgra::fromBgra(newB, newG, newR, static_cast<uint8_t>(totalA));
    }

private:
    uint8_t m_opacity;
};

// ── Normal blend (kept as-is with optimized fast paths) ─────────────────────

/// Normal (source-over) compositing.
class NormalBlendOp : public UserBlendOp {
public:
    [[nodiscard]] ColorBgra apply(ColorBgra lhs, ColorBgra rhs) const override;
    void apply(ColorBgra* dst, const ColorBgra* src, int length) const override;
    [[nodiscard]] std::unique_ptr<BinaryPixelOp> createWithOpacity(uint8_t opacity) const override;
};

/// Normal blend with pre-applied opacity on the source.
class NormalBlendOpWithOpacity : public BinaryPixelOp {
public:
    explicit NormalBlendOpWithOpacity(uint8_t opacity) : m_opacity(opacity) {}
    [[nodiscard]] ColorBgra apply(ColorBgra lhs, ColorBgra rhs) const override;
    void apply(ColorBgra* dst, const ColorBgra* src, int length) const override;

private:
    uint8_t m_opacity;
};

// ── Factory ─────────────────────────────────────────────────────────────────

/// Create a UserBlendOp for the given blend mode.
std::unique_ptr<UserBlendOp> createBlendOp(BlendMode mode);

} // namespace paintnux
