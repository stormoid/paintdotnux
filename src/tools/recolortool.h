#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <QRegion>
#include <vector>

namespace paintnux {

/// Recolor tool — replaces colors similar to a target with a per-channel delta
/// shift toward the replacement color, preserving shading.
class RecolorTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Recolor"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void deactivate() override;

    /// Euclidean RGB distance: ceil(sqrt((dR² + dG² + dB²) / 3))
    static int colorDifference(ColorBgra a, ColorBgra b);

private:
    void drawRecolorDot(Surface& dest, int cx, int cy);
    void drawRecolorLine(Surface& dest, QPointF from, QPointF to);
    void commitStroke();
    void buildBrushMask();

    // Stroke state
    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_lastPos;
    QRegion m_dirtyRegion;

    // Pre-stroke surface snapshot (for undo)
    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;

    // Recolor parameters (set on mouseDown, constant during stroke)
    ColorBgra m_colorToReplace{};
    ColorBgra m_colorReplaceWith{};
    int m_toleranceSq = 0;  // squared tolerance * 3 for direct comparison
    int m_deltaB = 0, m_deltaG = 0, m_deltaR = 0;

    // Pre-rendered brush mask: alpha values (0-255) for each pixel in brush bounding box
    std::vector<uint8_t> m_brushMask;
    int m_brushDiam = 0;   // brush mask side length
    int m_halfBrush = 0;   // half pen width (offset from center to top-left)

    // Cached selection rectangles (decomposed from QRegion for fast clipping)
    bool m_hasSelection = false;
    QRegion m_selRegion;
    int m_surfW = 0, m_surfH = 0;
};

} // namespace paintnux
