#include "tools/brushtool.h"
#include "ui/documentworkspace.h"
#include "ui/canvaswidget.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <cmath>
#include <algorithm>

namespace paintnux {

void BrushToolBase::drawDot(Surface& surf, QPointF pos, ColorBgra color, int radius) {
    int cx = static_cast<int>(pos.x());
    int cy = static_cast<int>(pos.y());

    // Get selection region for clipping (empty selection = no clipping)
    auto* sel = workspace()->selection();
    bool hasSelection = sel && !sel->isEmpty();
    const QRegion& selRegion = hasSelection ? sel->region() : QRegion();

    if (color.bgra == 0) {
        // Eraser mode: write transparent directly
        for (int y = cy - radius; y <= cy + radius; ++y) {
            if (y < 0 || y >= surf.height()) continue;
            ColorBgra* row = surf.rowPtr(y);
            for (int x = cx - radius; x <= cx + radius; ++x) {
                if (x < 0 || x >= surf.width()) continue;
                if (hasSelection && !selRegion.contains(QPoint(x, y))) continue;
                int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    row[x] = ColorBgra::transparent();
                }
            }
        }
    } else {
        bool overwrite = settings().blendMode == ToolBlendMode::Overwrite;
        float rf = static_cast<float>(radius) + 0.5f; // half-pixel outset for AA band
        for (int y = cy - radius - 1; y <= cy + radius + 1; ++y) {
            if (y < 0 || y >= surf.height()) continue;
            ColorBgra* row = surf.rowPtr(y);
            for (int x = cx - radius - 1; x <= cx + radius + 1; ++x) {
                if (x < 0 || x >= surf.width()) continue;
                if (hasSelection && !selRegion.contains(QPoint(x, y))) continue;
                int dx = x - cx, dy = y - cy;
                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (useAntialiasing()) {
                    float edge = rf - dist;
                    if (edge >= 1.0f) {
                        if (overwrite) {
                            row[x] = color;
                        } else {
                            ColorBgra dst = row[x];
                            uint8_t srcA = color.a;
                            uint8_t ia = 255 - srcA;
                            row[x].b = static_cast<uint8_t>((color.b * srcA + dst.b * ia) / 255);
                            row[x].g = static_cast<uint8_t>((color.g * srcA + dst.g * ia) / 255);
                            row[x].r = static_cast<uint8_t>((color.r * srcA + dst.r * ia) / 255);
                            row[x].a = std::max(dst.a, srcA);
                        }
                    } else if (edge > 0.0f) {
                        if (overwrite) {
                            ColorBgra c = color;
                            c.a = static_cast<uint8_t>(color.a * edge);
                            row[x] = c;
                        } else {
                            uint8_t srcA = static_cast<uint8_t>(color.a * edge);
                            if (srcA > 0) {
                                ColorBgra dst = row[x];
                                uint8_t ia = 255 - srcA;
                                row[x].b = static_cast<uint8_t>((color.b * srcA + dst.b * ia) / 255);
                                row[x].g = static_cast<uint8_t>((color.g * srcA + dst.g * ia) / 255);
                                row[x].r = static_cast<uint8_t>((color.r * srcA + dst.r * ia) / 255);
                                row[x].a = std::max(dst.a, srcA);
                            }
                        }
                    }
                } else {
                    if (dist <= rf) {
                        if (overwrite) {
                            row[x] = color;
                        } else {
                            ColorBgra dst = row[x];
                            uint8_t srcA = color.a;
                            uint8_t ia = 255 - srcA;
                            row[x].b = static_cast<uint8_t>((color.b * srcA + dst.b * ia) / 255);
                            row[x].g = static_cast<uint8_t>((color.g * srcA + dst.g * ia) / 255);
                            row[x].r = static_cast<uint8_t>((color.r * srcA + dst.r * ia) / 255);
                            row[x].a = std::max(dst.a, srcA);
                        }
                    }
                }
            }
        }
    }
}

void BrushToolBase::drawLine(Surface& surf, QPointF from, QPointF to, ColorBgra color, int radius) {
    float dx = static_cast<float>(to.x() - from.x());
    float dy = static_cast<float>(to.y() - from.y());
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) {
        drawDot(surf, to, color, radius);
        return;
    }

    // Step size: at most 1 pixel apart
    float step = 1.0f / std::max(1.0f, dist);
    for (float t = 0.0f; t <= 1.0f; t += step) {
        QPointF pt(from.x() + dx * t, from.y() + dy * t);
        drawDot(surf, pt, color, radius);
    }
}

void BrushToolBase::deactivate() {
    workspace()->canvas()->clearBrushCircles();
    Tool::deactivate();
}

void BrushToolBase::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_lastPos = docPos;
    m_dirtyRegion = QRegion();

    // Save the current layer state for undo
    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;

    int radius = brushRadius();
    ColorBgra color = strokeColor(button);
    drawDot(layer->surface(), docPos, color, radius);

    QRect affected(static_cast<int>(docPos.x()) - radius - 1,
                   static_cast<int>(docPos.y()) - radius - 1,
                   radius * 2 + 3, radius * 2 + 3);
    m_dirtyRegion += affected;
    invalidateCanvas();
}

void BrushToolBase::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    if (showBrushCircle())
        workspace()->canvas()->setBrushCircles({docPos}, settings().brushSize);
    if (!m_drawing) return;
    auto* layer = activeLayer();
    if (!layer) return;

    int radius = brushRadius();
    ColorBgra color = strokeColor(m_button);
    drawLine(layer->surface(), m_lastPos, docPos, color, radius);

    // Track dirty region
    int x1 = static_cast<int>(std::min(m_lastPos.x(), docPos.x())) - radius - 1;
    int y1 = static_cast<int>(std::min(m_lastPos.y(), docPos.y())) - radius - 1;
    int x2 = static_cast<int>(std::max(m_lastPos.x(), docPos.x())) + radius + 2;
    int y2 = static_cast<int>(std::max(m_lastPos.y(), docPos.y())) + radius + 2;
    m_dirtyRegion += QRect(x1, y1, x2 - x1, y2 - y1);

    m_lastPos = docPos;
    invalidateCanvas();
}

void BrushToolBase::mouseUp(QPointF, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!m_drawing || button != m_button) return;
    m_drawing = false;
    commitStroke();
}

void BrushToolBase::commitStroke() {
    if (!m_hasSaved || m_dirtyRegion.isEmpty()) return;
    auto* layer = activeLayer();
    if (!layer || !history()) return;

    // Create memento with the saved (pre-stroke) surface data
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        m_dirtyRegion, std::move(m_savedSurface));

    history()->pushNewMemento(std::move(memento));
    m_hasSaved = false;
}

// --- PaintBrushTool ---

ColorBgra PaintBrushTool::strokeColor(Qt::MouseButton button) const {
    return (button == Qt::LeftButton) ? settings().primaryColor : settings().secondaryColor;
}

// --- PencilTool ---

ColorBgra PencilTool::strokeColor(Qt::MouseButton button) const {
    return (button == Qt::LeftButton) ? settings().primaryColor : settings().secondaryColor;
}

} // namespace paintnux
