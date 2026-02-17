#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <QRectF>
#include <QPen>
#include <QBrush>
#include <QPainterPath>

#include <array>
#include <vector>

namespace paintnux {

/// Base class for shape-drawing tools (Rectangle, RoundedRect, Ellipse).
/// Handles the drag-preview-commit cycle: save surface on mouseDown, restore+render
/// on each mouseMove, and push BitmapHistoryMemento on mouseUp.
class ShapeToolBase : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

protected:
    /// Subclasses override this to draw their specific shape.
    virtual void renderShape(QPainter& painter, QPointF start, QPointF end,
                             const QPen& pen, const QBrush& brush,
                             ShapeDrawType drawType) = 0;

    /// Apply shift constraint to the endpoint. Default: force square aspect.
    virtual QPointF constrainShift(QPointF start, QPointF end) const;

    /// Build the normalized rect from start/end points.
    [[nodiscard]] static QRectF buildRect(QPointF start, QPointF end);

    void restoreSavedSurface();
    void setupPainter(QPainter& painter);
    void commitToHistory();

    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_startPos;
    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;

private:
    void renderToLayer(QPointF endPos, Qt::KeyboardModifiers mods);
};

// --- Line / Curve Tool ---

/// Line / Curve tool — draw a line, then drag 4 control nubs to bend it
/// into a spline (left-click drag) or bezier (right-click drag) curve.
class LineTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;
    [[nodiscard]] QString name() const override { return tr("Line / Curve"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void keyDown(QKeyEvent* event) override;

    void deactivate() override;

    /// Nub positions in document coordinates (for canvas to draw).
    [[nodiscard]] const std::array<QPointF, 4>& nubPositions() const { return m_nubs; }
    [[nodiscard]] bool showingNubs() const { return m_state == State::CurveEditing; }

private:
    enum class State { Idle, DrawingLine, CurveEditing };
    enum class CurveType { NotDecided, Spline, Bezier };

    void commitCurve();
    void renderCurve();
    void restoreSavedSurface();
    int hitTestNub(QPointF docPos) const;
    static QPointF constrainLineShift(QPointF start, QPointF end);

    State m_state = State::Idle;
    Qt::MouseButton m_lineButton = Qt::NoButton;
    QPointF m_lineStart;
    QPointF m_lineEnd;

    // Curve editing
    std::array<QPointF, 4> m_nubs;
    int m_dragNub = -1;
    CurveType m_curveType = CurveType::NotDecided;

    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;
};

/// Rectangle tool — drag to draw a rectangle.
class RectangleTool : public ShapeToolBase {
    Q_OBJECT
public:
    using ShapeToolBase::ShapeToolBase;
    [[nodiscard]] QString name() const override { return tr("Rectangle"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

protected:
    void renderShape(QPainter& painter, QPointF start, QPointF end,
                     const QPen& pen, const QBrush& brush,
                     ShapeDrawType drawType) override;
};

/// Rounded Rectangle tool — drag to draw a rounded rectangle.
class RoundedRectangleTool : public ShapeToolBase {
    Q_OBJECT
public:
    using ShapeToolBase::ShapeToolBase;
    [[nodiscard]] QString name() const override { return tr("Rounded Rectangle"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

protected:
    void renderShape(QPainter& painter, QPointF start, QPointF end,
                     const QPen& pen, const QBrush& brush,
                     ShapeDrawType drawType) override;
};

/// Ellipse tool — drag to draw an ellipse.
class EllipseTool : public ShapeToolBase {
    Q_OBJECT
public:
    using ShapeToolBase::ShapeToolBase;
    [[nodiscard]] QString name() const override { return tr("Ellipse"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

protected:
    void renderShape(QPainter& painter, QPointF start, QPointF end,
                     const QPen& pen, const QBrush& brush,
                     ShapeDrawType drawType) override;
};

/// Freeform Shape tool — drag to draw a freehand closed polygon.
class FreeformShapeTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    [[nodiscard]] QString name() const override { return tr("Freeform Shape"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    void renderFreeform();

    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    std::vector<QPointF> m_points;
    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;
};

} // namespace paintnux
