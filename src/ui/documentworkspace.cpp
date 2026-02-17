#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "core/blendops.h"

namespace paintnux {

DocumentWorkspace::DocumentWorkspace(QObject* parent)
    : QObject(parent)
    , m_selection(new Selection(this))
    , m_canvas(new CanvasWidget) {
    // Wire selection changes to canvas repaint and marching ants
    connect(m_selection, &Selection::changed, this, [this]() {
        m_canvas->setSelectionPath(m_selection->displayPath());
    });
}

void DocumentWorkspace::setDocument(std::unique_ptr<Document> doc) {
    if (m_activeTool) {
        m_activeTool->deactivate();
    }

    if (m_document) {
        m_document->disconnect(this);
    }

    m_document = std::move(doc);
    m_activeLayerIndex = 0;
    m_selection->reset();
    clearOverlay();

    if (m_document) {
        m_history = std::make_unique<HistoryStack>(m_document.get(), this);
        connect(m_document.get(), &Document::invalidated, this, &DocumentWorkspace::onDocumentInvalidated);
        ensureCompositeSurface();
        updateComposition(m_document->bounds());
    } else {
        m_history.reset();
        m_composite.reset();
        m_canvas->setRenderSource(nullptr);
    }

    if (m_activeTool) {
        m_activeTool->activate();
    }

    emit documentChanged();
}

WorkspaceState DocumentWorkspace::takeState() {
    if (m_activeTool) {
        m_activeTool->deactivate();
    }

    WorkspaceState state;
    state.selectionPath = m_selection->path();
    state.activeLayerIndex = m_activeLayerIndex;

    // Disconnect document signals before taking ownership
    if (m_document) {
        m_document->disconnect(this);
    }

    state.document = std::move(m_document);
    state.history = std::move(m_history);

    // Reset workspace to empty state
    m_selection->reset();
    clearOverlay();
    m_composite.reset();
    m_canvas->setRenderSource(nullptr);
    m_activeLayerIndex = 0;

    if (m_activeTool) {
        m_activeTool->activate();
    }

    return state;
}

void DocumentWorkspace::restoreState(WorkspaceState state) {
    if (m_activeTool) {
        m_activeTool->deactivate();
    }

    // Disconnect old document
    if (m_document) {
        m_document->disconnect(this);
    }

    m_document = std::move(state.document);
    m_history = std::move(state.history);
    m_selection->reset();
    clearOverlay();
    m_activeLayerIndex = 0;

    if (m_document) {
        // Point history at the (potentially same) document
        if (m_history) {
            m_history->setDocument(m_document.get());
        }

        connect(m_document.get(), &Document::invalidated, this, &DocumentWorkspace::onDocumentInvalidated);
        ensureCompositeSurface();
        updateComposition(m_document->bounds());

        m_activeLayerIndex = qBound(0, state.activeLayerIndex, m_document->layerCount() - 1);
    } else {
        m_composite.reset();
        m_canvas->setRenderSource(nullptr);
    }

    // Restore selection after document is set
    if (!state.selectionPath.isEmpty()) {
        m_selection->setPath(state.selectionPath);
    }

    if (m_activeTool) {
        m_activeTool->activate();
    }

    emit activeLayerChanged(m_activeLayerIndex);
    emit documentChanged();
}

void DocumentWorkspace::setActiveLayerIndex(int index) {
    if (m_activeLayerIndex != index) {
        m_activeLayerIndex = index;
        emit activeLayerChanged(index);
    }
}

void DocumentWorkspace::setActiveTool(Tool* tool) {
    if (m_activeTool == tool) return;

    if (m_activeTool) {
        m_previousTool = m_activeTool;
        m_activeTool->deactivate();
    }

    m_activeTool = tool;

    if (m_activeTool) {
        m_activeTool->activate();
        m_canvas->setCursor(m_activeTool->cursor());
    }

    emit activeToolChanged(m_activeTool);
}

void DocumentWorkspace::invalidateAll() {
    if (!m_document) return;
    ensureCompositeSurface();
    updateComposition(m_document->bounds());
}

std::pair<std::unique_ptr<Document>, int> DocumentWorkspace::replaceDocumentForHistory(
    std::unique_ptr<Document> newDoc, int newActiveLayerIndex) {
    // Save current state
    int oldActiveIdx = m_activeLayerIndex;

    // Disconnect old document signals
    if (m_document) {
        m_document->disconnect(this);
    }

    // Swap documents
    auto oldDoc = std::move(m_document);
    m_document = std::move(newDoc);

    // Update history stack to point to the new document
    m_history->setDocument(m_document.get());

    // Reset selection and overlay
    m_selection->reset();
    clearOverlay();

    // Clamp and set active layer index
    m_activeLayerIndex = qBound(0, newActiveLayerIndex, m_document->layerCount() - 1);

    // Rewire signals from new document
    connect(m_document.get(), &Document::invalidated, this, &DocumentWorkspace::onDocumentInvalidated);

    // Rebuild composite surface
    ensureCompositeSurface();
    updateComposition(m_document->bounds());

    // Notify UI
    emit activeLayerChanged(m_activeLayerIndex);
    emit documentChanged();

    return {std::move(oldDoc), oldActiveIdx};
}

void DocumentWorkspace::onDocumentInvalidated(const QRect& rect) {
    ensureCompositeSurface();
    updateComposition(rect);
}

void DocumentWorkspace::ensureCompositeSurface() {
    if (!m_document) return;
    if (!m_composite ||
        m_composite->width() != m_document->width() ||
        m_composite->height() != m_document->height()) {
        m_composite = std::make_unique<Surface>(m_document->width(), m_document->height());
    }
}

void DocumentWorkspace::updateComposition(const QRect& roi) {
    if (!m_document || !m_composite) return;

    m_document->render(*m_composite, roi);
    m_canvas->setRenderSource(m_composite.get());
    emit compositionUpdated();
}

// --- Overlay ---

void DocumentWorkspace::setOverlay(std::unique_ptr<Surface> surface, QPoint offset) {
    m_overlay = std::move(surface);
    m_overlayOffset = offset;
    m_overlayDisplaySize = QSizeF(0, 0);
    m_canvas->setOverlay(m_overlay.get(), m_overlayOffset);
}

void DocumentWorkspace::setOverlayDisplaySize(QSizeF size) {
    m_overlayDisplaySize = size;
    m_canvas->setOverlayDisplaySize(size);
}

void DocumentWorkspace::updateOverlayOffset(QPoint offset) {
    m_overlayOffset = offset;
    m_canvas->setOverlay(m_overlay.get(), m_overlayOffset);
}

std::unique_ptr<Surface> DocumentWorkspace::takeOverlay() {
    auto surface = std::move(m_overlay);
    m_canvas->setOverlay(nullptr, QPoint(0, 0));
    m_overlayOffset = QPoint(0, 0);
    m_overlayDisplaySize = QSizeF(0, 0);
    return surface;
}

void DocumentWorkspace::clearOverlay() {
    m_overlay.reset();
    m_overlayOffset = QPoint(0, 0);
    m_overlayDisplaySize = QSizeF(0, 0);
    m_canvas->setOverlay(nullptr, QPoint(0, 0));
}

void DocumentWorkspace::commitOverlay() {
    if (!m_overlay) return;

    auto* layer = dynamic_cast<BitmapLayer*>(m_document->layerAt(m_activeLayerIndex));
    if (!layer) {
        clearOverlay();
        return;
    }

    Surface& dst = layer->surface();
    const Surface& src = *m_overlay;
    NormalBlendOp blendOp;

    // Blend overlay onto layer at offset
    for (int sy = 0; sy < src.height(); ++sy) {
        int dy = sy + m_overlayOffset.y();
        if (dy < 0 || dy >= dst.height()) continue;

        const ColorBgra* srcRow = src.rowPtr(sy);
        ColorBgra* dstRow = dst.rowPtr(dy);

        for (int sx = 0; sx < src.width(); ++sx) {
            int dx = sx + m_overlayOffset.x();
            if (dx < 0 || dx >= dst.width()) continue;
            if (srcRow[sx].a == 0) continue;

            dstRow[dx] = blendOp.apply(dstRow[dx], srcRow[sx]);
        }
    }

    clearOverlay();
    invalidateAll();
}

} // namespace paintnux
