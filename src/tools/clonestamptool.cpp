#include "tools/clonestamptool.h"
#include "ui/documentworkspace.h"
#include "ui/canvaswidget.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace paintnux {

void CloneStampTool::activate() {
    Tool::activate();
}

void CloneStampTool::deactivate() {
    workspace()->canvas()->clearBrushCircles();
    Tool::deactivate();
}

void CloneStampTool::keyDown(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control && !m_drawing) {
        emit cursorChanged(Qt::PointingHandCursor);
    }
}

void CloneStampTool::keyUp(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control && !m_drawing) {
        emit cursorChanged(Qt::CrossCursor);
    }
}

void CloneStampTool::updateBrushCircles(QPointF docPos) {
    auto* canvas = workspace()->canvas();
    if (m_hasSource) {
        QPointF srcPos;
        if (m_hasOffset) {
            srcPos = QPointF(docPos.x() + m_offset.x(), docPos.y() + m_offset.y());
        } else {
            srcPos = m_sourceOrigin;
        }
        canvas->setBrushCircles({docPos, srcPos}, settings().brushSize);
    } else {
        canvas->setBrushCircles({docPos}, settings().brushSize);
    }
}

void CloneStampTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;

    // Ctrl+click: set source point
    if (mods & Qt::ControlModifier) {
        m_sourceOrigin = docPos;
        m_hasSource = true;
        m_hasOffset = false;
        updateBrushCircles(docPos);
        emit statusChanged(tr("Source set at (%1, %2)")
                               .arg(static_cast<int>(docPos.x()))
                               .arg(static_cast<int>(docPos.y())));
        return;
    }

    // No source set yet — do nothing
    if (!m_hasSource) {
        emit statusChanged(tr("Ctrl+click to set clone source"));
        return;
    }

    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_lastPos = docPos;
    m_dirtyRegion = QRegion();

    // Compute offset on first stroke only; subsequent strokes keep it
    if (!m_hasOffset) {
        m_offset = QPointF(m_sourceOrigin.x() - docPos.x(),
                           m_sourceOrigin.y() - docPos.y());
        m_hasOffset = true;
    }

    // Save pre-stroke surface for sampling (prevents feedback) and undo
    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;

    int radius = settings().brushSize / 2;
    drawCloneDot(layer->surface(), docPos);

    QRect affected(static_cast<int>(docPos.x()) - radius - 1,
                   static_cast<int>(docPos.y()) - radius - 1,
                   radius * 2 + 3, radius * 2 + 3);
    m_dirtyRegion += affected;
    updateBrushCircles(docPos);
    invalidateCanvas();
}

void CloneStampTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    // Always update brush circles (hover and drawing)
    updateBrushCircles(docPos);

    if (!m_drawing) return;
    auto* layer = activeLayer();
    if (!layer) return;

    int radius = settings().brushSize / 2;
    drawCloneLine(layer->surface(), m_lastPos, docPos);

    int x1 = static_cast<int>(std::min(m_lastPos.x(), docPos.x())) - radius - 1;
    int y1 = static_cast<int>(std::min(m_lastPos.y(), docPos.y())) - radius - 1;
    int x2 = static_cast<int>(std::max(m_lastPos.x(), docPos.x())) + radius + 2;
    int y2 = static_cast<int>(std::max(m_lastPos.y(), docPos.y())) + radius + 2;
    m_dirtyRegion += QRect(x1, y1, x2 - x1, y2 - y1);

    m_lastPos = docPos;
    invalidateCanvas();
}

void CloneStampTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!m_drawing || button != m_button) return;
    m_drawing = false;
    commitStroke();
    updateBrushCircles(docPos);
}

void CloneStampTool::commitStroke() {
    if (!m_hasSaved || m_dirtyRegion.isEmpty()) return;
    auto* layer = activeLayer();
    if (!layer || !history()) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        m_dirtyRegion, std::move(m_savedSurface));

    history()->pushNewMemento(std::move(memento));
    m_hasSaved = false;
}

void CloneStampTool::drawCloneDot(Surface& dest, QPointF pos) {
    int cx = static_cast<int>(pos.x());
    int cy = static_cast<int>(pos.y());
    int radius = settings().brushSize / 2;
    bool aa = settings().antialiased;

    int offsetX = static_cast<int>(m_offset.x());
    int offsetY = static_cast<int>(m_offset.y());

    int w = dest.width();
    int h = dest.height();

    auto* sel = workspace()->selection();
    bool hasSelection = sel && !sel->isEmpty();
    const QRegion& selRegion = hasSelection ? sel->region() : QRegion();

    float rf = static_cast<float>(radius) + 0.5f;

    for (int y = cy - radius - 1; y <= cy + radius + 1; ++y) {
        if (y < 0 || y >= h) continue;
        ColorBgra* row = dest.rowPtr(y);
        int srcY = y + offsetY;
        if (srcY < 0 || srcY >= h) continue;
        const ColorBgra* srcRow = m_savedSurface.rowPtr(srcY);

        for (int x = cx - radius - 1; x <= cx + radius + 1; ++x) {
            if (x < 0 || x >= w) continue;
            if (hasSelection && !selRegion.contains(QPoint(x, y))) continue;

            int srcX = x + offsetX;
            if (srcX < 0 || srcX >= w) continue;

            int dx = x - cx, dy = y - cy;
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));

            ColorBgra srcColor = srcRow[srcX];

            if (aa) {
                float edge = rf - dist;
                if (edge >= 1.0f) {
                    row[x] = srcColor;
                } else if (edge > 0.0f) {
                    uint8_t srcA = static_cast<uint8_t>(srcColor.a * edge);
                    if (srcA > 0) {
                        ColorBgra dst = row[x];
                        uint8_t ia = 255 - srcA;
                        row[x].b = static_cast<uint8_t>((srcColor.b * srcA + dst.b * ia) / 255);
                        row[x].g = static_cast<uint8_t>((srcColor.g * srcA + dst.g * ia) / 255);
                        row[x].r = static_cast<uint8_t>((srcColor.r * srcA + dst.r * ia) / 255);
                        row[x].a = static_cast<uint8_t>(srcA + dst.a * ia / 255);
                    }
                }
            } else {
                if (dist <= rf) {
                    row[x] = srcColor;
                }
            }
        }
    }
}

void CloneStampTool::drawCloneLine(Surface& dest, QPointF from, QPointF to) {
    float dx = static_cast<float>(to.x() - from.x());
    float dy = static_cast<float>(to.y() - from.y());
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) {
        drawCloneDot(dest, to);
        return;
    }

    float step = 1.0f / std::max(1.0f, dist);
    for (float t = 0.0f; t <= 1.0f; t += step) {
        QPointF pt(from.x() + dx * t, from.y() + dy * t);
        drawCloneDot(dest, pt);
    }
}

} // namespace paintnux
