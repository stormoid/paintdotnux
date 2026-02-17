#include "tools/magicwandtool.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/selectionhistorymemento.h"

#include <QPainterPath>
#include <QRegion>
#include <queue>
#include <vector>

namespace paintnux {

bool MagicWandTool::colorMatch(ColorBgra a, ColorBgra b, int tolerance) {
    int dr = a.r - b.r;
    int dg = a.g - b.g;
    int db = a.b - b.b;
    int da = a.a - b.a;
    int sum = dr * dr + dg * dg + db * db + da * da;
    return sum <= tolerance * tolerance * 4;
}

void MagicWandTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (button != Qt::LeftButton) return;

    auto* sel = workspace()->selection();
    if (!sel) return;

    // Check for resize handle hit first
    qreal threshold = 5.0 / workspace()->canvas()->zoomFactor();
    if (m_resizeHelper.tryBeginResize(sel, docPos, threshold)) {
        return;
    }

    auto* layer = activeLayer();
    if (!layer) return;

    int startX = static_cast<int>(docPos.x());
    int startY = static_cast<int>(docPos.y());
    const Surface& surf = layer->surface();

    if (!surf.isVisible(startX, startY)) return;

    int w = surf.width();
    int h = surf.height();
    int tolerance = settings().tolerance;
    ColorBgra targetColor = surf.getPoint(startX, startY);

    // Build stencil based on flood mode
    std::vector<bool> stencil(w * h, false);

    if (settings().floodMode == FloodMode::Global) {
        // Global: select all matching pixels anywhere
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (colorMatch(targetColor, *surf.pixelPtr(x, y), tolerance)) {
                    stencil[y * w + x] = true;
                }
            }
        }
    } else {
        // Contiguous: flood fill from click point
        struct Point { int x, y; };
        std::queue<Point> queue;
        queue.push({startX, startY});
        stencil[startY * w + startX] = true;

        while (!queue.empty()) {
            Point pt = queue.front();
            queue.pop();

            int left = pt.x;
            while (left > 0 && !stencil[pt.y * w + (left - 1)] &&
                   colorMatch(targetColor, *surf.pixelPtr(left - 1, pt.y), tolerance)) {
                --left;
                stencil[pt.y * w + left] = true;
            }

            int right = pt.x;
            while (right < w - 1 && !stencil[pt.y * w + (right + 1)] &&
                   colorMatch(targetColor, *surf.pixelPtr(right + 1, pt.y), tolerance)) {
                ++right;
                stencil[pt.y * w + right] = true;
            }

            for (int x = left; x <= right; ++x) {
                stencil[pt.y * w + x] = true;
            }

            for (int dy : {-1, 1}) {
                int ny = pt.y + dy;
                if (ny < 0 || ny >= h) continue;

                bool inSegment = false;
                for (int x = left; x <= right; ++x) {
                    if (!stencil[ny * w + x] &&
                        colorMatch(targetColor, *surf.pixelPtr(x, ny), tolerance)) {
                        if (!inSegment) {
                            queue.push({x, ny});
                            stencil[ny * w + x] = true;
                            inSegment = true;
                        }
                    } else {
                        inSegment = false;
                    }
                }
            }
        }
    }

    // Convert stencil to QRegion via scanline runs
    QRegion region;
    for (int y = 0; y < h; ++y) {
        int x = 0;
        while (x < w) {
            // Find start of run
            while (x < w && !stencil[y * w + x]) ++x;
            if (x >= w) break;
            int runStart = x;
            // Find end of run
            while (x < w && stencil[y * w + x]) ++x;
            region += QRect(runStart, y, x - runStart, 1);
        }
    }

    // Convert QRegion to QPainterPath
    QPainterPath wandPath;
    for (const QRect& r : region) {
        wandPath.addRect(r);
    }
    wandPath = wandPath.simplified();

    // Save current path for undo
    QPainterPath savedPath = sel->path();
    SelectionCombineMode mode = combineModeFromModifiers(mods, settings().selectionCombineMode);

    // Apply via continuation pattern
    sel->setContinuation(wandPath, mode);
    sel->commitContinuation();

    // Push undo
    auto memento = std::make_unique<SelectionHistoryMemento>(
        name(), sel, savedPath);
    history()->pushNewMemento(std::move(memento));
}

void MagicWandTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    if (m_resizeHelper.isResizing()) {
        auto* sel = workspace()->selection();
        if (sel) m_resizeHelper.updateResize(sel, docPos);
    }
}

void MagicWandTool::mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;
    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.finishResize(workspace()->selection(), history(), name());
    }
}

} // namespace paintnux
