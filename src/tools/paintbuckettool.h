#pragma once

#include "tools/tool.h"

namespace paintnux {

/// Paint bucket (flood fill) tool.
class PaintBucketTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Paint Bucket"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    /// Queue-based scanline flood fill (contiguous).
    void floodFill(Surface& surf, int startX, int startY, ColorBgra fillColor, int tolerance);

    /// Global fill: replace all matching pixels anywhere in the image.
    void globalFill(Surface& surf, int startX, int startY, ColorBgra fillColor, int tolerance);

    /// Check if two colors are within tolerance.
    [[nodiscard]] static bool colorMatch(ColorBgra a, ColorBgra b, int tolerance);
};

} // namespace paintnux
