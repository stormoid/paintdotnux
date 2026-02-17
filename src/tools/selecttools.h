#pragma once

#include "tools/tool.h"
#include "data/selection.h"

#include <QPainterPath>
#include <QPointF>
#include <QCursor>
#include <QRectF>

namespace paintnux {

/// Which resize handle on the selection bounding rect.
enum class SelectionHandle {
    None,
    TopLeft, Top, TopRight,
    Right,
    BottomRight, Bottom, BottomLeft,
    Left
};

/// Composable helper that manages selection resize-by-handle interaction.
/// Each selection tool owns one of these.
class SelectionResizeHelper {
public:
    /// Hit-test: returns which handle (if any) is near docPos.
    /// threshold is in document-coordinate pixels.
    static SelectionHandle hitTest(const QRectF& bounds, QPointF docPos, qreal threshold);

    /// Returns the appropriate resize cursor for the given handle.
    static QCursor cursorForHandle(SelectionHandle handle);

    /// Scale the original path so its bounding rect maps from origBounds to newBounds.
    static QPainterPath resizePath(const QPainterPath& original,
                                   const QRectF& origBounds,
                                   SelectionHandle handle,
                                   QPointF delta);

    /// Returns the 8 handle center positions (in document coordinates) for a bounding rect.
    static QList<QPointF> handlePositions(const QRectF& bounds);

    // --- Resize interaction state ---

    /// Try to begin a resize. Returns true if a handle was hit and resize mode entered.
    bool tryBeginResize(Selection* sel, QPointF docPos, qreal threshold);

    /// Update the resize preview during drag.
    void updateResize(Selection* sel, QPointF docPos);

    /// Finish the resize, push undo. Returns true if a resize was active.
    bool finishResize(Selection* sel, HistoryStack* history, const QString& toolName);

    /// Cancel any in-progress resize.
    void cancelResize();

    /// Is a resize currently in progress?
    [[nodiscard]] bool isResizing() const { return m_resizing; }

    /// Accessors for active resize state (valid only while isResizing()).
    [[nodiscard]] SelectionHandle activeHandle() const { return m_activeHandle; }
    [[nodiscard]] QPointF dragStart() const { return m_dragStart; }
    [[nodiscard]] const QRectF& originalBounds() const { return m_originalBounds; }

private:
    bool m_resizing = false;
    SelectionHandle m_activeHandle = SelectionHandle::None;
    QPointF m_dragStart;
    QPainterPath m_originalPath;
    QRectF m_originalBounds;
    QPainterPath m_savedPath; // for undo
};

/// Base class for drag-to-select tools (Rectangle, Ellipse).
/// Handles drag geometry, modifier->combine mode, undo, and continuation pattern.
class SelectionToolBase : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

protected:
    /// Subclass creates the shape path from the drag rectangle.
    [[nodiscard]] virtual QPainterPath createShapePath(const QRectF& rect) const = 0;

    /// Apply Normal / Fixed Ratio / Fixed Size constraint.
    [[nodiscard]] virtual QRectF constrainRect(QPointF start, QPointF end) const;

private:
    bool m_dragging = false;
    QPointF m_dragStart;
    SelectionCombineMode m_combineMode = SelectionCombineMode::Replace;
    QPainterPath m_savedPath; // for undo
    SelectionResizeHelper m_resizeHelper;
};

/// Rectangle selection tool.
class RectangleSelectTool : public SelectionToolBase {
    Q_OBJECT
public:
    using SelectionToolBase::SelectionToolBase;
    [[nodiscard]] QString name() const override { return tr("Rectangle Select"); }
protected:
    [[nodiscard]] QPainterPath createShapePath(const QRectF& rect) const override;
};

/// Ellipse selection tool.
class EllipseSelectTool : public SelectionToolBase {
    Q_OBJECT
public:
    using SelectionToolBase::SelectionToolBase;
    [[nodiscard]] QString name() const override { return tr("Ellipse Select"); }
protected:
    [[nodiscard]] QPainterPath createShapePath(const QRectF& rect) const override;
    [[nodiscard]] QRectF constrainRect(QPointF start, QPointF end) const override {
        return QRectF(start, end).normalized();
    }
};

/// Freehand lasso selection tool.
class LassoSelectTool : public Tool {
    Q_OBJECT
public:
    using Tool::Tool;
    [[nodiscard]] QString name() const override { return tr("Lasso Select"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    bool m_dragging = false;
    QList<QPointF> m_points;
    SelectionCombineMode m_combineMode = SelectionCombineMode::Replace;
    QPainterPath m_savedPath; // for undo
    SelectionResizeHelper m_resizeHelper;
};

} // namespace paintnux
