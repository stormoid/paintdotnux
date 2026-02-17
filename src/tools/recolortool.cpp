#include "tools/recolortool.h"
#include "ui/documentworkspace.h"
#include "ui/canvaswidget.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <cmath>
#include <algorithm>

namespace paintnux {

int RecolorTool::colorDifference(ColorBgra a, ColorBgra b) {
    int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
    int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
    int db = static_cast<int>(a.b) - static_cast<int>(b.b);
    return static_cast<int>(std::ceil(std::sqrt((dr * dr + dg * dg + db * db) / 3.0)));
}

void RecolorTool::buildBrushMask() {
    int brushSize = settings().brushSize;
    bool aa = settings().antialiased;

    m_halfBrush = brushSize / 2;
    m_brushDiam = m_halfBrush * 2 + 3;  // +1 each side for AA fringe + center
    m_brushMask.resize(m_brushDiam * m_brushDiam);

    float rf = static_cast<float>(m_halfBrush) + 0.5f;

    for (int by = 0; by < m_brushDiam; ++by) {
        int dy = by - m_halfBrush - 1;
        for (int bx = 0; bx < m_brushDiam; ++bx) {
            int dx = bx - m_halfBrush - 1;
            float distSq = static_cast<float>(dx * dx + dy * dy);
            float dist = std::sqrt(distSq);

            uint8_t alpha;
            if (aa) {
                float edge = rf - dist;
                if (edge >= 1.0f)
                    alpha = 255;
                else if (edge > 0.0f)
                    alpha = static_cast<uint8_t>(255.0f * edge);
                else
                    alpha = 0;
            } else {
                alpha = (dist <= rf) ? 255 : 0;
            }

            m_brushMask[by * m_brushDiam + bx] = alpha;
        }
    }
}

void RecolorTool::deactivate() {
    workspace()->canvas()->clearBrushCircles();
    Tool::deactivate();
}

void RecolorTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;

    auto* layer = activeLayer();
    if (!layer) return;

    m_drawing = true;
    m_button = button;
    m_lastPos = docPos;
    m_dirtyRegion = QRegion();

    // Left-click: replace secondary with primary; Right-click: swap
    if (button == Qt::LeftButton) {
        m_colorToReplace = settings().secondaryColor;
        m_colorReplaceWith = settings().primaryColor;
    } else {
        m_colorToReplace = settings().primaryColor;
        m_colorReplaceWith = settings().secondaryColor;
    }

    // Pre-compute per-channel deltas (constant for entire stroke)
    m_deltaB = static_cast<int>(m_colorReplaceWith.b) - static_cast<int>(m_colorToReplace.b);
    m_deltaG = static_cast<int>(m_colorReplaceWith.g) - static_cast<int>(m_colorToReplace.g);
    m_deltaR = static_cast<int>(m_colorReplaceWith.r) - static_cast<int>(m_colorToReplace.r);

    // Compute squared tolerance for comparison without sqrt in hot loop.
    // colorDifference = ceil(sqrt(diffSq/3)) <= tol  ⟺  diffSq <= tol² * 3
    // (ceil adds at most 1, so we use (tol+1)² to be slightly generous, matching
    //  the original ceil-based check exactly for integer tolerances)
    int maxDiff = colorDifference(m_colorToReplace, m_colorReplaceWith);
    int scaledTol = settings().tolerance * 256 / 100;
    int tol = std::min(scaledTol, maxDiff);
    m_toleranceSq = tol * tol * 3;

    // Cache selection state (constant during stroke)
    auto* sel = workspace()->selection();
    m_hasSelection = sel && !sel->isEmpty();
    if (m_hasSelection)
        m_selRegion = sel->region();

    // Cache surface dimensions
    m_surfW = layer->surface().width();
    m_surfH = layer->surface().height();

    // Build brush mask once per stroke
    buildBrushMask();

    // Save pre-stroke surface for undo
    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;

    int cx = static_cast<int>(docPos.x());
    int cy = static_cast<int>(docPos.y());
    drawRecolorDot(layer->surface(), cx, cy);

    QRect affected(cx - m_halfBrush - 1, cy - m_halfBrush - 1, m_brushDiam, m_brushDiam);
    m_dirtyRegion += affected;

    workspace()->canvas()->setBrushCircles({docPos}, settings().brushSize);
    invalidateCanvas();
}

void RecolorTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    workspace()->canvas()->setBrushCircles({docPos}, settings().brushSize);

    if (!m_drawing) return;
    auto* layer = activeLayer();
    if (!layer) return;

    drawRecolorLine(layer->surface(), m_lastPos, docPos);

    int x1 = static_cast<int>(std::min(m_lastPos.x(), docPos.x())) - m_halfBrush - 1;
    int y1 = static_cast<int>(std::min(m_lastPos.y(), docPos.y())) - m_halfBrush - 1;
    int x2 = static_cast<int>(std::max(m_lastPos.x(), docPos.x())) + m_halfBrush + 2;
    int y2 = static_cast<int>(std::max(m_lastPos.y(), docPos.y())) + m_halfBrush + 2;
    m_dirtyRegion += QRect(x1, y1, x2 - x1, y2 - y1);

    m_lastPos = docPos;
    invalidateCanvas();
}

void RecolorTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (!m_drawing || button != m_button) return;
    m_drawing = false;
    commitStroke();
    workspace()->canvas()->setBrushCircles({docPos}, settings().brushSize);
}

void RecolorTool::commitStroke() {
    if (!m_hasSaved || m_dirtyRegion.isEmpty()) return;
    auto* layer = activeLayer();
    if (!layer || !history()) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        m_dirtyRegion, std::move(m_savedSurface));

    history()->pushNewMemento(std::move(memento));
    m_hasSaved = false;
}

void RecolorTool::drawRecolorDot(Surface& dest, int cx, int cy) {
    // Compute brush bounding box clipped to surface
    int bx0 = cx - m_halfBrush - 1;
    int by0 = cy - m_halfBrush - 1;

    int yStart = std::max(0, by0);
    int yEnd   = std::min(m_surfH, by0 + m_brushDiam);
    int xStart = std::max(0, bx0);
    int xEnd   = std::min(m_surfW, bx0 + m_brushDiam);

    if (yStart >= yEnd || xStart >= xEnd) return;

    // Precompute color-to-replace channels for inline difference check
    int trB = m_colorToReplace.b, trG = m_colorToReplace.g, trR = m_colorToReplace.r;

    for (int y = yStart; y < yEnd; ++y) {
        ColorBgra* row = dest.rowPtr(y);
        int maskRowOff = (y - by0) * m_brushDiam - bx0;  // offset so mask index = maskRowOff + x

        for (int x = xStart; x < xEnd; ++x) {
            uint8_t brushAlpha = m_brushMask[maskRowOff + x];
            if (brushAlpha == 0) continue;

            if (m_hasSelection && !m_selRegion.contains(QPoint(x, y))) continue;

            // Read from live surface (Paint.NET style): already-recolored pixels
            // will fail the tolerance check, naturally preventing feedback.
            ColorBgra pixel = row[x];

            // Inline squared color difference: (dR²+dG²+dB²) <= tol²*3
            int db = static_cast<int>(pixel.b) - trB;
            int dg = static_cast<int>(pixel.g) - trG;
            int dr = static_cast<int>(pixel.r) - trR;
            int diffSq = dr * dr + dg * dg + db * db;
            if (diffSq > m_toleranceSq) continue;

            // Apply per-channel delta shift (RGB only; alpha is always preserved)
            ColorBgra result;
            result.b = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel.b) + m_deltaB, 0, 255));
            result.g = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel.g) + m_deltaG, 0, 255));
            result.r = static_cast<uint8_t>(std::clamp(static_cast<int>(pixel.r) + m_deltaR, 0, 255));

            if (brushAlpha == 255) {
                row[x].b = result.b;
                row[x].g = result.g;
                row[x].r = result.r;
                // alpha preserved — recolor only affects color, not transparency
            } else {
                // AA fringe: brush alpha controls strength of RGB shift, alpha preserved
                uint8_t ia = 255 - brushAlpha;
                row[x].b = static_cast<uint8_t>((result.b * brushAlpha + pixel.b * ia) / 255);
                row[x].g = static_cast<uint8_t>((result.g * brushAlpha + pixel.g * ia) / 255);
                row[x].r = static_cast<uint8_t>((result.r * brushAlpha + pixel.r * ia) / 255);
            }
        }
    }
}

void RecolorTool::drawRecolorLine(Surface& dest, QPointF from, QPointF to) {
    float dx = static_cast<float>(to.x() - from.x());
    float dy = static_cast<float>(to.y() - from.y());
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.5f) {
        drawRecolorDot(dest, static_cast<int>(to.x()), static_cast<int>(to.y()));
        return;
    }

    // Paint.NET uses sqrt(brushWidth) spacing for smoother interpolation
    float bw = static_cast<float>(settings().brushSize) / 2.0f;
    float fInc = std::sqrt(bw) / length;
    if (fInc > 1.0f) fInc = 1.0f;

    for (float t = 0.0f; t <= 1.0f; t += fInc) {
        float px = from.x() + dx * t;
        float py = from.y() + dy * t;
        drawRecolorDot(dest, static_cast<int>(px), static_cast<int>(py));
    }
}

} // namespace paintnux
