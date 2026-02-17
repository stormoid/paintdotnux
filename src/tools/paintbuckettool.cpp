#include "tools/paintbuckettool.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <queue>
#include <vector>

namespace paintnux {

static inline ColorBgra blendOver(ColorBgra dst, ColorBgra src) {
    uint8_t sa = src.a;
    if (sa == 255) return src;
    if (sa == 0) return dst;
    uint8_t ia = 255 - sa;
    return ColorBgra::fromBgra(
        static_cast<uint8_t>((src.b * sa + dst.b * ia) / 255),
        static_cast<uint8_t>((src.g * sa + dst.g * ia) / 255),
        static_cast<uint8_t>((src.r * sa + dst.r * ia) / 255),
        static_cast<uint8_t>(sa + (dst.a * ia) / 255));
}

bool PaintBucketTool::colorMatch(ColorBgra a, ColorBgra b, int tolerance) {
    int dr = a.r - b.r;
    int dg = a.g - b.g;
    int db = a.b - b.b;
    int da = a.a - b.a;
    int sum = dr * dr + dg * dg + db * db + da * da;
    return sum <= tolerance * tolerance * 4;
}

void PaintBucketTool::floodFill(Surface& surf, int startX, int startY, ColorBgra fillColor, int tolerance) {
    int w = surf.width();
    int h = surf.height();
    ColorBgra targetColor = surf.getPoint(startX, startY);
    bool overwrite = settings().blendMode == ToolBlendMode::Overwrite;

    if (colorMatch(targetColor, fillColor, 0) && tolerance == 0) return;

    // Get selection region for clipping — build a per-row bitmask for fast lookup
    auto* sel = workspace()->selection();
    bool hasSelection = sel && !sel->isEmpty();

    // Selection bitmask: one bit per pixel, packed by row
    // Only allocated if there's an active selection
    std::vector<uint8_t> selMask;
    if (hasSelection) {
        selMask.resize(static_cast<size_t>(w) * h, 0);
        const QRegion& region = sel->region();
        for (const QRect& r : region) {
            QRect clipped = r.intersected(surf.bounds());
            for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
                uint8_t* maskRow = selMask.data() + y * w;
                for (int x = clipped.left(); x <= clipped.right(); ++x) {
                    maskRow[x] = 1;
                }
            }
        }
    }

    // Visited bitmask
    std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);

    // Track bounding rect of affected area
    int minX = startX, maxX = startX, minY = startY, maxY = startY;

    struct Span { int y, left, right; };
    std::queue<Span> queue;

    // Helper: test if pixel at (x,y) is fillable
    auto canFill = [&](int x, int y) -> bool {
        if (visited[y * w + x]) return false;
        if (hasSelection && !selMask[y * w + x]) return false;
        return colorMatch(targetColor, *(surf.rowPtr(y) + x), tolerance);
    };

    // Seed: scan the initial row from startX
    if (hasSelection && !selMask[startY * w + startX]) return;

    // Scan left/right from start
    int left = startX, right = startX;
    const ColorBgra* startRow = surf.rowPtr(startY);
    while (left > 0 && (!hasSelection || selMask[startY * w + (left - 1)]) &&
           colorMatch(targetColor, startRow[left - 1], tolerance))
        --left;
    while (right < w - 1 && (!hasSelection || selMask[startY * w + (right + 1)]) &&
           colorMatch(targetColor, startRow[right + 1], tolerance))
        ++right;

    // Mark visited and fill
    ColorBgra* fillRow = surf.rowPtr(startY);
    for (int x = left; x <= right; ++x) {
        visited[startY * w + x] = 1;
        fillRow[x] = overwrite ? fillColor : blendOver(fillRow[x], fillColor);
    }
    minX = left; maxX = right; minY = startY; maxY = startY;
    queue.push({startY, left, right});

    while (!queue.empty()) {
        Span span = queue.front();
        queue.pop();

        // Check rows above and below
        for (int dy : {-1, 1}) {
            int ny = span.y + dy;
            if (ny < 0 || ny >= h) continue;

            int x = span.left;
            while (x <= span.right) {
                // Skip non-fillable pixels
                if (!canFill(x, ny)) { ++x; continue; }

                // Found start of a new span — scan left
                int sl = x;
                while (sl > 0 && canFill(sl - 1, ny)) --sl;

                // Scan right
                int sr = x;
                while (sr < w - 1 && canFill(sr + 1, ny)) ++sr;

                // Mark visited and fill
                ColorBgra* row = surf.rowPtr(ny);
                for (int fx = sl; fx <= sr; ++fx) {
                    visited[ny * w + fx] = 1;
                    row[fx] = overwrite ? fillColor : blendOver(row[fx], fillColor);
                }

                // Update bounds
                if (sl < minX) minX = sl;
                if (sr > maxX) maxX = sr;
                if (ny < minY) minY = ny;
                if (ny > maxY) maxY = ny;

                queue.push({ny, sl, sr});

                // Skip past this span
                x = sr + 1;
            }
        }
    }
}

void PaintBucketTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;

    auto* layer = activeLayer();
    if (!layer) return;

    int x = static_cast<int>(docPos.x());
    int y = static_cast<int>(docPos.y());
    Surface& surf = layer->surface();

    if (!surf.isVisible(x, y)) return;

    // Check if click is within selection (if there is one)
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty() && !sel->region().contains(QPoint(x, y))) return;

    ColorBgra fillColor = (button == Qt::LeftButton)
        ? settings().primaryColor
        : settings().secondaryColor;

    // Save undo
    QRegion region(surf.bounds());
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(), region);

    // Perform fill
    if (settings().floodMode == FloodMode::Global)
        globalFill(surf, x, y, fillColor, settings().tolerance);
    else
        floodFill(surf, x, y, fillColor, settings().tolerance);

    history()->pushNewMemento(std::move(memento));
    invalidateCanvas();
}

void PaintBucketTool::globalFill(Surface& surf, int startX, int startY, ColorBgra fillColor, int tolerance) {
    int w = surf.width();
    int h = surf.height();
    ColorBgra targetColor = surf.getPoint(startX, startY);
    bool overwrite = settings().blendMode == ToolBlendMode::Overwrite;

    if (colorMatch(targetColor, fillColor, 0) && tolerance == 0) return;

    auto* sel = workspace()->selection();
    bool hasSelection = sel && !sel->isEmpty();

    for (int y = 0; y < h; ++y) {
        ColorBgra* row = surf.rowPtr(y);
        for (int x = 0; x < w; ++x) {
            if (hasSelection && !sel->region().contains(QPoint(x, y))) continue;
            if (colorMatch(targetColor, row[x], tolerance)) {
                row[x] = overwrite ? fillColor : blendOver(row[x], fillColor);
            }
        }
    }
}

} // namespace paintnux
