#include "tools/movetools.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"
#include "history/selectionhistorymemento.h"

#include <cmath>
#include <memory>

namespace paintnux {

// --- MoveTool ---

void MoveTool::activate() {
    Tool::activate();

    // Adopt any existing overlay (e.g. from Paste)
    if (workspace()->overlaySurface()) {
        m_lifted = true;
        m_currentOffset = workspace()->overlayOffset();
    }
}

void MoveTool::adoptOverlay() {
    m_dragging = false;
    if (workspace()->overlaySurface()) {
        m_lifted = true;
        m_currentOffset = workspace()->overlayOffset();
    } else {
        m_lifted = false;
        m_currentOffset = QPoint(0, 0);
    }
}

void MoveTool::liftPixels() {
    auto* layer = activeLayer();
    auto* sel = workspace()->selection();
    if (!layer || !sel || sel->isEmpty()) return;

    Surface& src = layer->surface();
    const QRegion& region = sel->region();
    QRect bounds = region.boundingRect().intersected(src.bounds());
    if (bounds.isEmpty()) return;

    // Save layer state for undo
    auto memento = std::make_unique<BitmapHistoryMemento>(
        name(), document(), workspace()->activeLayerIndex(),
        region);

    // Create overlay surface (bounds-sized)
    auto overlay = std::make_unique<Surface>(bounds.width(), bounds.height());
    overlay->clear(ColorBgra::transparent());

    // Copy selected pixels into overlay and erase from layer
    for (const QRect& r : region) {
        QRect clipped = r.intersected(src.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            ColorBgra* srcRow = src.rowPtr(y);
            ColorBgra* dstRow = overlay->rowPtr(y - bounds.top());
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                dstRow[x - bounds.left()] = srcRow[x];
                srcRow[x] = ColorBgra::transparent();
            }
        }
    }

    history()->pushNewMemento(std::move(memento));

    m_currentOffset = bounds.topLeft();
    workspace()->setOverlay(std::move(overlay), m_currentOffset);
    m_lifted = true;

    invalidateCanvas();
}

void MoveTool::commitPixels() {
    if (!m_lifted) return;
    if (!workspace()->overlaySurface()) {
        m_lifted = false;
        return;
    }

    // Save layer state for undo before committing overlay
    auto* layer = activeLayer();
    if (layer) {
        const Surface* overlay = workspace()->overlaySurface();
        QPoint offset = workspace()->overlayOffset();
        QRect overlayBounds(offset, QSize(overlay->width(), overlay->height()));
        QRegion region(overlayBounds.intersected(layer->surface().bounds()));

        auto memento = std::make_unique<BitmapHistoryMemento>(
            name(), document(), workspace()->activeLayerIndex(),
            region);

        workspace()->commitOverlay();
        history()->pushNewMemento(std::move(memento));
    } else {
        workspace()->clearOverlay();
    }

    m_lifted = false;
    m_currentOffset = QPoint(0, 0);
}

void MoveTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    // Sync lifted state with actual overlay (may have been cleared by undo)
    if (m_lifted && !workspace()->overlaySurface()) {
        m_lifted = false;
        m_currentOffset = QPoint(0, 0);
    }

    auto* sel = workspace()->selection();

    // Check for resize handle hit first
    qreal threshold = 5.0 / workspace()->canvas()->zoomFactor();
    if (m_resizeHelper.tryBeginResize(sel, docPos, threshold)) {
        // Save overlay state for scaling during resize
        if (m_lifted && workspace()->overlaySurface()) {
            m_resizeOrigOverlayOffset = workspace()->overlayOffset();
            auto* ovl = workspace()->overlaySurface();
            m_resizeOrigOverlaySize = QSizeF(ovl->width(), ovl->height());
        }
        return;
    }

    // Save selection path before any dragging (for undo)
    if (sel) m_savedSelPath = sel->path();

    if (m_lifted) {
        // Already have a floating overlay — start dragging it
        m_dragging = true;
        m_lastDocPos = docPos;
        return;
    }

    // No overlay yet — try to lift from selection
    if (!sel || sel->isEmpty()) return;

    liftPixels();
    m_dragging = true;
    m_lastDocPos = docPos;
}

void MoveTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    if (m_resizeHelper.isResizing()) {
        auto* sel = workspace()->selection();
        if (sel) {
            m_resizeHelper.updateResize(sel, docPos);

            // Scale the overlay to match the new selection bounds
            if (m_lifted && workspace()->overlaySurface()) {
                QRectF newBounds = sel->path().boundingRect();
                QPoint newOffset(static_cast<int>(newBounds.x()),
                                 static_cast<int>(newBounds.y()));
                m_currentOffset = newOffset;
                workspace()->updateOverlayOffset(newOffset);
                workspace()->setOverlayDisplaySize(newBounds.size());
            }
        }
        return;
    }

    if (!m_dragging || !m_lifted) return;

    QPointF delta = docPos - m_lastDocPos;
    QPoint intDelta(static_cast<int>(delta.x()), static_cast<int>(delta.y()));
    if (intDelta.isNull()) return;

    m_currentOffset += intDelta;
    workspace()->updateOverlayOffset(m_currentOffset);

    // Move the selection outline along with the pixels
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty()) {
        QPainterPath moved = sel->path();
        moved.translate(intDelta.x(), intDelta.y());
        sel->setPath(moved);
    }

    // Snap lastDocPos to the integer delta we actually applied,
    // so sub-pixel remainder accumulates correctly
    m_lastDocPos += QPointF(intDelta);
}

void MoveTool::mouseUp(QPointF, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    if (m_resizeHelper.isResizing()) {
        // Resample the overlay surface to its new display size
        if (m_lifted && workspace()->overlaySurface()) {
            QSizeF displaySize = workspace()->overlayDisplaySize();
            if (displaySize.width() > 0 && displaySize.height() > 0) {
                const Surface* old = workspace()->overlaySurface();
                Qt::TransformationMode txMode =
                    (settings().resamplingAlgorithm == ResamplingAlgorithm::NearestNeighbor)
                        ? Qt::FastTransformation
                        : Qt::SmoothTransformation;
                QImage scaled = old->qimage().scaled(
                    static_cast<int>(displaySize.width()),
                    static_cast<int>(displaySize.height()),
                    Qt::IgnoreAspectRatio,
                    txMode);
                if (scaled.format() != QImage::Format_ARGB32)
                    scaled = scaled.convertToFormat(QImage::Format_ARGB32);
                auto newSurface = std::make_unique<Surface>(scaled);
                workspace()->setOverlay(std::move(newSurface), m_currentOffset);
            }
        }

        m_resizeHelper.finishResize(workspace()->selection(), history(), name());
        return;
    }

    if (!m_dragging) return;
    m_dragging = false;
    // Don't commit yet — user might want to continue adjusting.
    // Commit happens on next click outside, tool switch, or deactivate.
}

void MoveTool::deactivate() {
    // Sync with actual overlay state (may have been cleared by undo)
    if (m_lifted && !workspace()->overlaySurface()) {
        m_lifted = false;
    }
    if (m_lifted) {
        commitPixels();
    }
    m_dragging = false;
    m_resizeHelper.cancelResize();
    Tool::deactivate();
}

// --- MoveSelectionTool ---

void MoveSelectionTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    auto* sel = workspace()->selection();
    if (!sel || sel->isEmpty()) return;

    // Check for resize handle hit first
    qreal threshold = 5.0 / workspace()->canvas()->zoomFactor();
    if (m_resizeHelper.tryBeginResize(sel, docPos, threshold)) {
        return;
    }

    m_dragging = true;
    m_lastDocPos = docPos;
    m_savedPath = sel->path();
}

void MoveSelectionTool::mouseMove(QPointF docPos, Qt::MouseButtons, Qt::KeyboardModifiers) {
    if (m_resizeHelper.isResizing()) {
        auto* sel = workspace()->selection();
        if (sel) m_resizeHelper.updateResize(sel, docPos);
        return;
    }

    if (!m_dragging) return;

    auto* sel = workspace()->selection();
    if (!sel) return;

    QPointF delta = docPos - m_lastDocPos;

    // Translate the selection path by delta
    QPainterPath translated = sel->path();
    translated.translate(delta);
    sel->setPath(translated);

    m_lastDocPos = docPos;
}

void MoveSelectionTool::mouseUp(QPointF, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;

    if (m_resizeHelper.isResizing()) {
        m_resizeHelper.finishResize(workspace()->selection(), history(), name());
        return;
    }

    if (!m_dragging) return;
    m_dragging = false;

    auto* sel = workspace()->selection();
    if (!sel) return;

    // Push undo with the path saved at drag start
    auto memento = std::make_unique<SelectionHistoryMemento>(
        name(), sel, m_savedPath);
    history()->pushNewMemento(std::move(memento));
}

} // namespace paintnux
