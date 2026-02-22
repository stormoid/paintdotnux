#include "tools/selecttools.h"
#include "ui/documentworkspace.h"
#include "history/selectionhistorymemento.h"
#include "data/document.h"

#include <QTransform>
#include <cmath>

namespace paintnux {

// --- SelectionResizeHelper ---

SelectionHandle SelectionResizeHelper::hitTest(const QRectF& bounds, QPointF docPos, qreal threshold) {
    if (bounds.isEmpty()) return SelectionHandle::None;

    auto positions = handlePositions(bounds);
    // Order matches: TL, T, TR, R, BR, B, BL, L
    static const SelectionHandle handles[] = {
        SelectionHandle::TopLeft, SelectionHandle::Top, SelectionHandle::TopRight,
        SelectionHandle::Right,
        SelectionHandle::BottomRight, SelectionHandle::Bottom, SelectionHandle::BottomLeft,
        SelectionHandle::Left
    };

    for (int i = 0; i < positions.size(); ++i) {
        qreal dx = docPos.x() - positions[i].x();
        qreal dy = docPos.y() - positions[i].y();
        if (dx * dx + dy * dy <= threshold * threshold) {
            return handles[i];
        }
    }
    return SelectionHandle::None;
}

QCursor SelectionResizeHelper::cursorForHandle(SelectionHandle handle) {
    switch (handle) {
        case SelectionHandle::TopLeft:
        case SelectionHandle::BottomRight:
            return QCursor(Qt::SizeFDiagCursor);
        case SelectionHandle::TopRight:
        case SelectionHandle::BottomLeft:
            return QCursor(Qt::SizeBDiagCursor);
        case SelectionHandle::Top:
        case SelectionHandle::Bottom:
            return QCursor(Qt::SizeVerCursor);
        case SelectionHandle::Left:
        case SelectionHandle::Right:
            return QCursor(Qt::SizeHorCursor);
        default:
            return QCursor(Qt::ArrowCursor);
    }
}

QPainterPath SelectionResizeHelper::resizePath(const QPainterPath& original,
                                                const QRectF& origBounds,
                                                SelectionHandle handle,
                                                QPointF delta) {
    if (origBounds.isEmpty()) return original;

    QRectF newBounds = origBounds;

    switch (handle) {
        case SelectionHandle::TopLeft:
            newBounds.setTopLeft(origBounds.topLeft() + delta);
            break;
        case SelectionHandle::Top:
            newBounds.setTop(origBounds.top() + delta.y());
            break;
        case SelectionHandle::TopRight:
            newBounds.setTopRight(origBounds.topRight() + delta);
            break;
        case SelectionHandle::Right:
            newBounds.setRight(origBounds.right() + delta.x());
            break;
        case SelectionHandle::BottomRight:
            newBounds.setBottomRight(origBounds.bottomRight() + delta);
            break;
        case SelectionHandle::Bottom:
            newBounds.setBottom(origBounds.bottom() + delta.y());
            break;
        case SelectionHandle::BottomLeft:
            newBounds.setBottomLeft(origBounds.bottomLeft() + delta);
            break;
        case SelectionHandle::Left:
            newBounds.setLeft(origBounds.left() + delta.x());
            break;
        default:
            return original;
    }

    // Clamp minimum size to prevent degenerate transforms
    if (newBounds.width() < 1.0) {
        if (handle == SelectionHandle::Left || handle == SelectionHandle::TopLeft || handle == SelectionHandle::BottomLeft)
            newBounds.setLeft(newBounds.right() - 1.0);
        else
            newBounds.setRight(newBounds.left() + 1.0);
    }
    if (newBounds.height() < 1.0) {
        if (handle == SelectionHandle::Top || handle == SelectionHandle::TopLeft || handle == SelectionHandle::TopRight)
            newBounds.setTop(newBounds.bottom() - 1.0);
        else
            newBounds.setBottom(newBounds.top() + 1.0);
    }

    // Transform original path from origBounds to newBounds
    QTransform t;
    t.translate(newBounds.x(), newBounds.y());
    t.scale(newBounds.width() / origBounds.width(),
            newBounds.height() / origBounds.height());
    t.translate(-origBounds.x(), -origBounds.y());

    return t.map(original);
}

QList<QPointF> SelectionResizeHelper::handlePositions(const QRectF& bounds) {
    qreal cx = bounds.center().x();
    qreal cy = bounds.center().y();
    return {
        bounds.topLeft(),
        QPointF(cx, bounds.top()),
        bounds.topRight(),
        QPointF(bounds.right(), cy),
        bounds.bottomRight(),
        QPointF(cx, bounds.bottom()),
        bounds.bottomLeft(),
        QPointF(bounds.left(), cy)
    };
}

bool SelectionResizeHelper::tryBeginResize(Selection* sel, QPointF docPos, qreal threshold) {
    if (!sel || sel->isEmpty()) return false;

    QRectF bounds = sel->path().boundingRect();
    SelectionHandle handle = hitTest(bounds, docPos, threshold);
    if (handle == SelectionHandle::None) return false;

    m_resizing = true;
    m_activeHandle = handle;
    m_dragStart = docPos;
    m_originalPath = sel->path();
    m_originalBounds = bounds;
    m_savedPath = sel->path();
    return true;
}

void SelectionResizeHelper::updateResize(Selection* sel, QPointF docPos) {
    if (!m_resizing || !sel) return;

    QPointF delta = docPos - m_dragStart;
    QPainterPath newPath = resizePath(m_originalPath, m_originalBounds, m_activeHandle, delta);
    sel->setPath(newPath);
}

bool SelectionResizeHelper::finishResize(Selection* sel, HistoryStack* history, const QString& toolName) {
    if (!m_resizing) return false;
    m_resizing = false;

    if (!sel || !history) return false;

    // Push undo memento
    auto memento = std::make_unique<SelectionHistoryMemento>(
        toolName, sel, m_savedPath);
    history->pushNewMemento(std::move(memento));
    return true;
}

void SelectionResizeHelper::cancelResize() {
    m_resizing = false;
}

// --- SelectionToolBase ---

void SelectionToolBase::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return;

    auto* sel = workspace()->selection();
    if (!sel) return;

    // Check for resize handle hit first
    qreal threshold = 5.0 / workspace()->canvas()->zoomFactor();
    if (m_resizeHelper.tryBeginResize(sel, docPos, threshold)) {
        return;
    }

    m_dragging = true;
    m_dragStart = docPos;
    m_combineMode = combineModeFromModifiers(mods, settings().selectionCombineMode);

    // Save current path for undo
    m_savedPath = sel->path();
}

void SelectionToolBase::mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers) {
    auto* sel = workspace()->selection();
    if (!sel) return;

    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.updateResize(sel, docPos);
        return;
    }

    if (!m_dragging) return;

    QRectF rect = constrainRect(m_dragStart, docPos);
    if (rect.width() < 1 && rect.height() < 1) return;

    QPainterPath shapePath = createShapePath(rect);
    sel->setContinuation(shapePath, m_combineMode);
}

void SelectionToolBase::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.finishResize(workspace()->selection(), history(), name());
        return;
    }

    if (!m_dragging) return;
    m_dragging = false;

    auto* sel = workspace()->selection();
    if (!sel) return;

    QRectF rect = constrainRect(m_dragStart, docPos);
    if (rect.width() < 1 && rect.height() < 1) {
        // Tiny drag -- if Replace mode, deselect; otherwise cancel
        sel->clearContinuation();
        if (m_combineMode == SelectionCombineMode::Replace && !sel->isEmpty()) {
            // Push undo for deselect
            auto memento = std::make_unique<SelectionHistoryMemento>(
                name(), sel, m_savedPath);
            sel->reset();
            history()->pushNewMemento(std::move(memento));
        }
        return;
    }

    // Commit the continuation, then clip to canvas bounds
    sel->commitContinuation();
    QPainterPath canvasClip;
    canvasClip.addRect(QRect(0, 0, document()->width(), document()->height()));
    sel->setPath(sel->path().intersected(canvasClip));

    // Push undo memento
    auto memento = std::make_unique<SelectionHistoryMemento>(
        name(), sel, m_savedPath);
    history()->pushNewMemento(std::move(memento));
}

QRectF SelectionToolBase::constrainRect(QPointF start, QPointF end) const {
    SelectionDrawMode mode = settings().selectionDrawMode;

    if (mode == SelectionDrawMode::FixedRatio) {
        double ratioW = settings().selectionDrawWidth;
        double ratioH = settings().selectionDrawHeight;
        if (ratioW <= 0 || ratioH <= 0)
            return QRectF(start, end).normalized();

        double aspect = ratioW / ratioH;
        double dx = end.x() - start.x();
        double dy = end.y() - start.y();
        double adx = std::abs(dx);
        double ady = std::abs(dy);

        // Constrain to aspect ratio
        if (adx / aspect <= ady) {
            // Width is the limiting dimension
            double h = adx / aspect;
            return QRectF(start, QPointF(end.x(), start.y() + (dy >= 0 ? h : -h))).normalized();
        } else {
            double w = ady * aspect;
            return QRectF(start, QPointF(start.x() + (dx >= 0 ? w : -w), end.y())).normalized();
        }
    }

    if (mode == SelectionDrawMode::FixedSize) {
        double w = settings().selectionDrawWidth;
        double h = settings().selectionDrawHeight;
        if (w <= 0 || h <= 0)
            return QRectF(start, end).normalized();
        // Fixed size follows the mouse — current position is the top-left corner
        return QRectF(end.x(), end.y(), w, h);
    }

    // Normal mode
    return QRectF(start, end).normalized();
}

// --- RectangleSelectTool ---

QPainterPath RectangleSelectTool::createShapePath(const QRectF& rect) const {
    QPainterPath path;
    path.addRect(rect);
    return path;
}

// --- EllipseSelectTool ---

QPainterPath EllipseSelectTool::createShapePath(const QRectF& rect) const {
    QPainterPath path;
    path.addEllipse(rect);
    return path;
}

// --- LassoSelectTool ---

void LassoSelectTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return;

    auto* sel = workspace()->selection();
    if (!sel) return;

    // Check for resize handle hit first
    qreal threshold = 5.0 / workspace()->canvas()->zoomFactor();
    if (m_resizeHelper.tryBeginResize(sel, docPos, threshold)) {
        return;
    }

    m_dragging = true;
    m_combineMode = combineModeFromModifiers(mods, settings().selectionCombineMode);
    m_savedPath = sel->path();

    m_points.clear();
    m_points.append(docPos);
}

void LassoSelectTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    auto* sel = workspace()->selection();
    if (!sel) return;

    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.updateResize(sel, docPos);
        return;
    }

    if (!m_dragging) return;

    m_points.append(docPos);

    if (m_points.size() < 3) return;

    QPainterPath lassoPath;
    lassoPath.moveTo(m_points.first());
    for (int i = 1; i < m_points.size(); ++i)
        lassoPath.lineTo(m_points[i]);
    lassoPath.closeSubpath();

    sel->setContinuation(lassoPath, m_combineMode);
}

void LassoSelectTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.finishResize(workspace()->selection(), history(), name());
        return;
    }

    if (!m_dragging) return;
    m_dragging = false;

    auto* sel = workspace()->selection();
    if (!sel) return;

    m_points.append(docPos);

    if (m_points.size() < 3) {
        // Too few points -- if Replace mode, deselect; otherwise cancel
        sel->clearContinuation();
        if (m_combineMode == SelectionCombineMode::Replace && !sel->isEmpty()) {
            auto memento = std::make_unique<SelectionHistoryMemento>(
                name(), sel, m_savedPath);
            sel->reset();
            history()->pushNewMemento(std::move(memento));
        }
        return;
    }

    sel->commitContinuation();
    QPainterPath canvasClipL;
    canvasClipL.addRect(QRect(0, 0, document()->width(), document()->height()));
    sel->setPath(sel->path().intersected(canvasClipL));

    auto memento = std::make_unique<SelectionHistoryMemento>(
        name(), sel, m_savedPath);
    history()->pushNewMemento(std::move(memento));
}

} // namespace paintnux
