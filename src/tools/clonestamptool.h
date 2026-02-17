#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <QRect>
#include <QRegion>

namespace paintnux {

/// Clone Stamp tool — Ctrl+click to set source, then paint to copy pixels
/// from source location with a fixed offset.
class CloneStampTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Clone Stamp"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void keyDown(QKeyEvent* event) override;
    void keyUp(QKeyEvent* event) override;
    void activate() override;
    void deactivate() override;

private:
    void drawCloneDot(Surface& dest, QPointF pos);
    void drawCloneLine(Surface& dest, QPointF from, QPointF to);
    void commitStroke();
    void updateBrushCircles(QPointF docPos);

    // Source point (persists across strokes)
    bool m_hasSource = false;
    QPointF m_sourceOrigin;

    // Stroke state
    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_lastPos;
    QRegion m_dirtyRegion;

    // Offset from current brush pos to source sample pos
    QPointF m_offset;
    bool m_hasOffset = false; // true after first stroke establishes offset

    // Pre-stroke surface snapshot (sample from this to avoid feedback)
    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;
};

} // namespace paintnux
