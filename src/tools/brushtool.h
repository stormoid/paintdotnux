#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <QRect>
#include <QRegion>
#include <vector>

namespace paintnux {

/// Common base for PaintBrush, Pencil, and Eraser tools.
/// Handles stroke interpolation, region tracking, and undo creation.
class BrushToolBase : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void deactivate() override;

protected:
    /// Get the color to paint with, given which button started the stroke.
    [[nodiscard]] virtual ColorBgra strokeColor(Qt::MouseButton button) const = 0;

    /// Whether antialiasing is used.
    [[nodiscard]] virtual bool useAntialiasing() const { return settings().antialiased; }

    /// Whether to show the brush circle guide on hover.
    [[nodiscard]] virtual bool showBrushCircle() const { return true; }

    /// Brush radius in pixels. Override for fixed-size tools.
    [[nodiscard]] virtual int brushRadius() const { return settings().brushSize / 2; }

    /// Draw a single dot at the given position.
    void drawDot(Surface& surf, QPointF pos, ColorBgra color, int radius);

    /// Interpolate points between two positions and draw dots.
    void drawLine(Surface& surf, QPointF from, QPointF to, ColorBgra color, int radius);

private:
    void commitStroke();

    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_lastPos;
    QRegion m_dirtyRegion;
    Surface m_savedSurface{1, 1}; // scratch copy for undo
    bool m_hasSaved = false;
};

/// PaintBrush tool - paints with antialiased brush.
class PaintBrushTool : public BrushToolBase {
    Q_OBJECT
public:
    using BrushToolBase::BrushToolBase;
    [[nodiscard]] QString name() const override { return tr("Paintbrush"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }
protected:
    [[nodiscard]] ColorBgra strokeColor(Qt::MouseButton button) const override;
};

/// Pencil tool - hard-edged single-pixel or small brush.
class PencilTool : public BrushToolBase {
    Q_OBJECT
public:
    using BrushToolBase::BrushToolBase;
    [[nodiscard]] QString name() const override { return tr("Pencil"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }
protected:
    [[nodiscard]] ColorBgra strokeColor(Qt::MouseButton button) const override;
    [[nodiscard]] bool useAntialiasing() const override { return false; }
    [[nodiscard]] bool showBrushCircle() const override { return false; }
    [[nodiscard]] int brushRadius() const override { return 0; }
};

/// Eraser tool - paints with transparent.
class EraserTool : public BrushToolBase {
    Q_OBJECT
public:
    using BrushToolBase::BrushToolBase;
    [[nodiscard]] QString name() const override { return tr("Eraser"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }
protected:
    [[nodiscard]] ColorBgra strokeColor(Qt::MouseButton) const override {
        return ColorBgra::transparent();
    }
};

} // namespace paintnux
