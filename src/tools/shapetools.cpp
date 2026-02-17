#include "tools/shapetools.h"
#include "ui/documentworkspace.h"
#include "ui/canvaswidget.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <QPainter>
#include <QPolygonF>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace paintnux {

// ============================================================
// ShapeToolBase
// ============================================================

void ShapeToolBase::restoreSavedSurface() {
    auto* layer = activeLayer();
    if (!layer) return;
    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();
    for (int y = 0; y < h; ++y) {
        std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
    }
}

void ShapeToolBase::setupPainter(QPainter& painter) {
    if (settings().antialiased)
        painter.setRenderHint(QPainter::Antialiasing);
    if (settings().blendMode == ToolBlendMode::Overwrite)
        painter.setCompositionMode(QPainter::CompositionMode_Source);
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty())
        painter.setClipRegion(sel->region());
}

void ShapeToolBase::commitToHistory() {
    if (!m_hasSaved) return;
    auto* layer = activeLayer();
    if (!layer || !history()) return;
    QRegion fullRegion(QRect(0, 0, layer->surface().width(), layer->surface().height()));
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        fullRegion, std::move(m_savedSurface));
    history()->pushNewMemento(std::move(memento));
    m_hasSaved = false;
}

void ShapeToolBase::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_startPos = docPos;

    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;
}

void ShapeToolBase::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers mods) {
    if (!m_drawing) return;
    renderToLayer(docPos, mods);
    invalidateCanvas();
}

void ShapeToolBase::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!m_drawing || button != m_button) return;
    m_drawing = false;

    renderToLayer(docPos, mods);
    invalidateCanvas();
    commitToHistory();
}

void ShapeToolBase::renderToLayer(QPointF endPos, Qt::KeyboardModifiers mods) {
    auto* layer = activeLayer();
    if (!layer) return;

    restoreSavedSurface();

    QPointF constrainedEnd = endPos;
    if (mods & Qt::ShiftModifier)
        constrainedEnd = constrainShift(m_startPos, endPos);

    ColorBgra outlineColor, fillColor;
    if (m_button == Qt::LeftButton) {
        outlineColor = settings().primaryColor;
        fillColor = settings().secondaryColor;
    } else {
        outlineColor = settings().secondaryColor;
        fillColor = settings().primaryColor;
    }

    QPen pen(QColor(outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a),
             settings().brushSize, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    QBrush brush(QColor(fillColor.r, fillColor.g, fillColor.b, fillColor.a));

    QPainter painter(&layer->surface().qimage());
    setupPainter(painter);
    renderShape(painter, m_startPos, constrainedEnd, pen, brush, settings().shapeDrawType);
    painter.end();
}

QPointF ShapeToolBase::constrainShift(QPointF start, QPointF end) const {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal size = std::max(std::abs(dx), std::abs(dy));
    return QPointF(start.x() + std::copysign(size, dx),
                   start.y() + std::copysign(size, dy));
}

QRectF ShapeToolBase::buildRect(QPointF start, QPointF end) {
    return QRectF(std::min(start.x(), end.x()),
                  std::min(start.y(), end.y()),
                  std::abs(end.x() - start.x()),
                  std::abs(end.y() - start.y()));
}

// ============================================================
// LineTool (Line / Curve)
// ============================================================

QPointF LineTool::constrainLineShift(QPointF start, QPointF end) {
    qreal dx = end.x() - start.x();
    qreal dy = end.y() - start.y();
    qreal angle = std::atan2(dy, dx);
    qreal snapped = std::round(angle / (M_PI / 4.0)) * (M_PI / 4.0);
    qreal dist = std::sqrt(dx * dx + dy * dy);
    return QPointF(start.x() + dist * std::cos(snapped),
                   start.y() + dist * std::sin(snapped));
}

void LineTool::restoreSavedSurface() {
    auto* layer = activeLayer();
    if (!layer) return;
    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();
    for (int y = 0; y < h; ++y) {
        std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
    }
}

int LineTool::hitTestNub(QPointF docPos) const {
    // Hit radius in document coords — scale by zoom so nubs are easy to grab
    qreal threshold = 6.0 / workspace()->canvas()->zoomFactor();
    for (int i = 0; i < 4; ++i) {
        qreal dx = docPos.x() - m_nubs[i].x();
        qreal dy = docPos.y() - m_nubs[i].y();
        if (dx * dx + dy * dy <= threshold * threshold)
            return i;
    }
    return -1;
}

void LineTool::renderCurve() {
    auto* layer = activeLayer();
    if (!layer) return;

    restoreSavedSurface();

    ColorBgra outlineColor;
    if (m_lineButton == Qt::LeftButton)
        outlineColor = settings().primaryColor;
    else
        outlineColor = settings().secondaryColor;

    QPen pen(QColor(outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a),
             settings().brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

    QPainter painter(&layer->surface().qimage());
    if (settings().antialiased)
        painter.setRenderHint(QPainter::Antialiasing);
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty())
        painter.setClipRegion(sel->region());

    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Check if all nubs are still on the line (no curve editing done yet)
    bool isStraight = (m_curveType == CurveType::NotDecided);

    if (isStraight) {
        painter.drawLine(m_nubs[0], m_nubs[3]);
    } else if (m_curveType == CurveType::Bezier) {
        QPainterPath path;
        path.moveTo(m_nubs[0]);
        path.cubicTo(m_nubs[1], m_nubs[2], m_nubs[3]);
        painter.drawPath(path);
    } else {
        // Spline: use QPainterPath with quadratic segments through the control points.
        // Qt doesn't have AddCurve like .NET, so we approximate a cardinal spline
        // using cubic bezier segments.
        // For 4 points, we create a smooth path through all of them.
        QPainterPath path;
        path.moveTo(m_nubs[0]);

        // Cardinal spline with tension=0 (Catmull-Rom) through 4 points
        // For each segment [i, i+1], control points are:
        //   cp1 = P[i]   + (P[i+1] - P[i-1]) / 6
        //   cp2 = P[i+1] - (P[i+2] - P[i])   / 6
        for (int i = 0; i < 3; ++i) {
            QPointF p0 = m_nubs[std::max(0, i - 1)];
            QPointF p1 = m_nubs[i];
            QPointF p2 = m_nubs[i + 1];
            QPointF p3 = m_nubs[std::min(3, i + 2)];

            QPointF cp1(p1.x() + (p2.x() - p0.x()) / 6.0,
                        p1.y() + (p2.y() - p0.y()) / 6.0);
            QPointF cp2(p2.x() - (p3.x() - p1.x()) / 6.0,
                        p2.y() - (p3.y() - p1.y()) / 6.0);

            path.cubicTo(cp1, cp2, p2);
        }
        painter.drawPath(path);
    }

    painter.end();
}

void LineTool::commitCurve() {
    if (!m_hasSaved) return;
    auto* layer = activeLayer();
    if (!layer || !history()) return;

    // Final render
    renderCurve();
    invalidateCanvas();

    QRegion fullRegion(QRect(0, 0, layer->surface().width(), layer->surface().height()));
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        fullRegion, std::move(m_savedSurface));
    history()->pushNewMemento(std::move(memento));
    m_hasSaved = false;
    m_state = State::Idle;
    m_curveType = CurveType::NotDecided;
    invalidateCanvas();
}

void LineTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    auto* layer = activeLayer();
    if (!layer) return;

    if (m_state == State::Idle) {
        // Start drawing a new line
        m_state = State::DrawingLine;
        m_lineButton = button;
        m_lineStart = docPos;
        m_lineEnd = docPos;

        m_savedSurface = layer->surface().clone();
        m_hasSaved = true;

    } else if (m_state == State::CurveEditing) {
        // Hit-test nubs
        int nub = hitTestNub(docPos);
        if (nub >= 0) {
            // Start dragging this nub
            m_dragNub = nub;
            if (m_curveType == CurveType::NotDecided) {
                m_curveType = (button == Qt::RightButton) ? CurveType::Bezier : CurveType::Spline;
            }
        } else {
            // Clicked away from nubs — commit current curve and start new line
            commitCurve();
            // Start a fresh line
            m_state = State::DrawingLine;
            m_lineButton = button;
            m_lineStart = docPos;
            m_lineEnd = docPos;
            m_savedSurface = layer->surface().clone();
            m_hasSaved = true;
        }
    }
}

void LineTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers mods) {
    if (m_state == State::DrawingLine) {
        m_lineEnd = docPos;
        if (mods & Qt::ShiftModifier)
            m_lineEnd = constrainLineShift(m_lineStart, docPos);

        // Render preview: restore + draw line
        restoreSavedSurface();

        ColorBgra outlineColor;
        if (m_lineButton == Qt::LeftButton)
            outlineColor = settings().primaryColor;
        else
            outlineColor = settings().secondaryColor;

        QPen pen(QColor(outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a),
                 settings().brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        auto* layer = activeLayer();
        if (layer) {
            QPainter painter(&layer->surface().qimage());
            if (settings().antialiased)
                painter.setRenderHint(QPainter::Antialiasing);
            auto* sel = workspace()->selection();
            if (sel && !sel->isEmpty())
                painter.setClipRegion(sel->region());
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(m_lineStart, m_lineEnd);
            painter.end();
        }
        invalidateCanvas();

    } else if (m_state == State::CurveEditing && m_dragNub >= 0) {
        m_nubs[m_dragNub] = docPos;
        renderCurve();
        invalidateCanvas();
    }
}

void LineTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (m_state == State::DrawingLine && button == m_lineButton) {
        m_lineEnd = docPos;
        if (mods & Qt::ShiftModifier)
            m_lineEnd = constrainLineShift(m_lineStart, docPos);

        // Transition to curve editing — place 4 nubs evenly along the line
        m_nubs[0] = m_lineStart;
        m_nubs[1] = QPointF(m_lineStart.x() + (m_lineEnd.x() - m_lineStart.x()) / 3.0,
                             m_lineStart.y() + (m_lineEnd.y() - m_lineStart.y()) / 3.0);
        m_nubs[2] = QPointF(m_lineStart.x() + (m_lineEnd.x() - m_lineStart.x()) * 2.0 / 3.0,
                             m_lineStart.y() + (m_lineEnd.y() - m_lineStart.y()) * 2.0 / 3.0);
        m_nubs[3] = m_lineEnd;

        m_state = State::CurveEditing;
        m_curveType = CurveType::NotDecided;
        m_dragNub = -1;

        // Render the straight line (nubs haven't moved yet)
        renderCurve();
        invalidateCanvas();

        emit statusChanged(tr("Drag nubs to curve (right-click for Bezier). Enter to finish."));

    } else if (m_state == State::CurveEditing && m_dragNub >= 0) {
        m_dragNub = -1;
        renderCurve();
        invalidateCanvas();
    }
}

void LineTool::keyDown(QKeyEvent* event) {
    if (m_state == State::CurveEditing && event->key() == Qt::Key_Return) {
        commitCurve();
    } else if (m_state == State::CurveEditing && event->key() == Qt::Key_Escape) {
        // Cancel — restore saved surface
        if (m_hasSaved) {
            restoreSavedSurface();
            m_hasSaved = false;
        }
        m_state = State::Idle;
        m_curveType = CurveType::NotDecided;
        invalidateCanvas();
    }
}

void LineTool::deactivate() {
    if (m_state == State::CurveEditing) {
        commitCurve();
    }
    m_state = State::Idle;
    Tool::deactivate();
}

// ============================================================
// RectangleTool
// ============================================================

void RectangleTool::renderShape(QPainter& painter, QPointF start, QPointF end,
                                const QPen& pen, const QBrush& brush,
                                ShapeDrawType drawType) {
    if (drawType == ShapeDrawType::Outline) {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    } else if (drawType == ShapeDrawType::Fill) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
    } else {
        painter.setPen(pen);
        painter.setBrush(brush);
    }
    painter.drawRect(buildRect(start, end));
}

// ============================================================
// RoundedRectangleTool
// ============================================================

void RoundedRectangleTool::renderShape(QPainter& painter, QPointF start, QPointF end,
                                       const QPen& pen, const QBrush& brush,
                                       ShapeDrawType drawType) {
    if (drawType == ShapeDrawType::Outline) {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    } else if (drawType == ShapeDrawType::Fill) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
    } else {
        painter.setPen(pen);
        painter.setBrush(brush);
    }
    QRectF rect = buildRect(start, end);
    qreal r = std::min(rect.width(), rect.height()) / 4.0;
    painter.drawRoundedRect(rect, r, r);
}

// ============================================================
// EllipseTool
// ============================================================

void EllipseTool::renderShape(QPainter& painter, QPointF start, QPointF end,
                              const QPen& pen, const QBrush& brush,
                              ShapeDrawType drawType) {
    if (drawType == ShapeDrawType::Outline) {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    } else if (drawType == ShapeDrawType::Fill) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
    } else {
        painter.setPen(pen);
        painter.setBrush(brush);
    }
    painter.drawEllipse(buildRect(start, end));
}

// ============================================================
// FreeformShapeTool
// ============================================================

void FreeformShapeTool::renderFreeform() {
    auto* layer = activeLayer();
    if (!layer || m_points.size() < 2) return;

    // Restore saved surface
    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();
    for (int y = 0; y < h; ++y) {
        std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
    }

    // Determine colors based on button
    ColorBgra outlineColor, fillColor;
    if (m_button == Qt::LeftButton) {
        outlineColor = settings().primaryColor;
        fillColor = settings().secondaryColor;
    } else {
        outlineColor = settings().secondaryColor;
        fillColor = settings().primaryColor;
    }

    QPen pen(QColor(outlineColor.r, outlineColor.g, outlineColor.b, outlineColor.a),
             settings().brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QBrush brush(QColor(fillColor.r, fillColor.g, fillColor.b, fillColor.a));

    // Build closed polygon
    QPolygonF poly(static_cast<int>(m_points.size()));
    for (int i = 0; i < static_cast<int>(m_points.size()); ++i)
        poly[i] = m_points[i];

    QPainter painter(&layer->surface().qimage());
    if (settings().antialiased)
        painter.setRenderHint(QPainter::Antialiasing);
    if (settings().blendMode == ToolBlendMode::Overwrite)
        painter.setCompositionMode(QPainter::CompositionMode_Source);
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty())
        painter.setClipRegion(sel->region());

    ShapeDrawType drawType = settings().shapeDrawType;
    if (drawType == ShapeDrawType::Outline) {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
    } else if (drawType == ShapeDrawType::Fill) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
    } else {
        painter.setPen(pen);
        painter.setBrush(brush);
    }

    painter.drawPolygon(poly);
    painter.end();
}

void FreeformShapeTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_points.clear();
    m_points.push_back(docPos);

    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;
}

void FreeformShapeTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    if (!m_drawing) return;
    m_points.push_back(docPos);
    renderFreeform();
    invalidateCanvas();
}

void FreeformShapeTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!m_drawing || button != m_button) return;
    m_drawing = false;

    m_points.push_back(docPos);
    renderFreeform();
    invalidateCanvas();

    // Commit to history
    if (m_hasSaved) {
        auto* layer = activeLayer();
        if (layer && history()) {
            QRegion fullRegion(QRect(0, 0, layer->surface().width(), layer->surface().height()));
            auto memento = std::make_unique<BitmapHistoryMemento>(
                name(), document(), workspace()->activeLayerIndex(),
                fullRegion, std::move(m_savedSurface));
            history()->pushNewMemento(std::move(memento));
            m_hasSaved = false;
        }
    }
}

} // namespace paintnux
