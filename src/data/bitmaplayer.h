#pragma once

#include "data/layer.h"
#include "core/blendops.h"

#include <memory>

namespace paintnux {

/// A layer containing a bitmap surface.
class BitmapLayer : public Layer {
    Q_OBJECT

public:
    BitmapLayer(int width, int height, QObject* parent = nullptr);
    BitmapLayer(int width, int height, ColorBgra fillColor, QObject* parent = nullptr);

    /// Create from an existing surface (takes ownership via move).
    explicit BitmapLayer(Surface surface, QObject* parent = nullptr);

    [[nodiscard]] Surface& surface() { return m_surface; }
    [[nodiscard]] const Surface& surface() const { return m_surface; }

    /// Get the blend operation for this layer.
    [[nodiscard]] UserBlendOp& blendOp() const { return *m_blendOp; }

    /// Set the blend operation (takes ownership).
    void setBlendOp(std::unique_ptr<UserBlendOp> op);

    /// Get the current blend mode.
    [[nodiscard]] BlendMode blendMode() const { return m_blendMode; }

    /// Set the blend mode (recreates the blend op).
    void setBlendMode(BlendMode mode);

    // Layer overrides
    void render(Surface& dst, const QRect& roi) const override;
    [[nodiscard]] Surface renderThumbnail(int maxEdge) const override;
    [[nodiscard]] std::unique_ptr<Layer> clone() const override;

    /// Create a default white background layer.
    [[nodiscard]] static std::unique_ptr<BitmapLayer> createBackground(int width, int height);

private:
    void compileBlendOp();

    Surface m_surface;
    BlendMode m_blendMode = BlendMode::Normal;
    std::unique_ptr<UserBlendOp> m_blendOp;
    std::unique_ptr<BinaryPixelOp> m_compiledBlendOp;
};

} // namespace paintnux
