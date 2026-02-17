#pragma once

#include "data/document.h"
#include "data/selection.h"
#include "core/surface.h"
#include "ui/canvaswidget.h"
#include "history/historystack.h"
#include "tools/tool.h"

#include <QObject>
#include <QPoint>
#include <QPainterPath>

#include <memory>
#include <vector>

namespace paintnux {

/// Snapshot of workspace state for tab switching.
struct WorkspaceState {
    std::unique_ptr<Document> document;
    std::unique_ptr<HistoryStack> history;
    QPainterPath selectionPath;
    int activeLayerIndex = 0;
};

/// DocumentWorkspace binds a Document to a CanvasWidget.
/// It manages the composited render surface, history stack, selection, and active tool.
class DocumentWorkspace : public QObject {
    Q_OBJECT

public:
    explicit DocumentWorkspace(QObject* parent = nullptr);
    ~DocumentWorkspace() override = default;

    /// Set the document this workspace manages. Takes ownership.
    void setDocument(std::unique_ptr<Document> doc);

    /// Save the current workspace state (document, history, selection) for tab switching.
    WorkspaceState takeState();

    /// Restore a previously saved workspace state.
    void restoreState(WorkspaceState state);
    [[nodiscard]] Document* document() const { return m_document.get(); }

    /// Get the canvas widget for embedding in the UI.
    [[nodiscard]] CanvasWidget* canvas() const { return m_canvas; }

    /// Get the composited surface (read-only).
    [[nodiscard]] const Surface* compositeSurface() const { return m_composite.get(); }

    /// Force a full recomposite and repaint.
    void invalidateAll();

    /// Replace the document for a history operation (keeps history stack intact).
    /// Returns the old document and previous active layer index.
    std::pair<std::unique_ptr<Document>, int> replaceDocumentForHistory(
        std::unique_ptr<Document> newDoc, int newActiveLayerIndex);

    /// Get the active layer index.
    [[nodiscard]] int activeLayerIndex() const { return m_activeLayerIndex; }
    void setActiveLayerIndex(int index);

    /// History stack for undo/redo.
    [[nodiscard]] HistoryStack* historyStack() const { return m_history.get(); }

    /// Selection (always exists, may be empty).
    [[nodiscard]] Selection* selection() const { return m_selection; }

    /// Active tool management.
    [[nodiscard]] Tool* activeTool() const { return m_activeTool; }
    [[nodiscard]] Tool* previousActiveTool() const { return m_previousTool; }
    void setActiveTool(Tool* tool);

    // --- Floating overlay for MoveTool ---

    /// Set a floating overlay surface (e.g., lifted pixels during move).
    void setOverlay(std::unique_ptr<Surface> surface, QPoint offset);

    /// Clear the floating overlay.
    void clearOverlay();

    /// Update overlay position without changing the surface.
    void updateOverlayOffset(QPoint offset);

    /// Get the overlay surface (may be null).
    [[nodiscard]] const Surface* overlaySurface() const { return m_overlay.get(); }

    /// Take ownership of the overlay surface (removes it from workspace).
    [[nodiscard]] std::unique_ptr<Surface> takeOverlay();

    /// Get the overlay offset in document coordinates.
    [[nodiscard]] QPoint overlayOffset() const { return m_overlayOffset; }

    /// Set an override display size for the overlay (for resize preview).
    void setOverlayDisplaySize(QSizeF size);

    /// Get the overlay display size (0,0 = native).
    [[nodiscard]] QSizeF overlayDisplaySize() const { return m_overlayDisplaySize; }

    /// Commit the overlay back onto the active layer at the current offset.
    void commitOverlay();

signals:
    void documentChanged();
    void activeLayerChanged(int index);
    void compositionUpdated();
    void activeToolChanged(Tool* tool);

private:
    void onDocumentInvalidated(const QRect& rect);
    void updateComposition(const QRect& roi);
    void ensureCompositeSurface();

    std::unique_ptr<Document> m_document;
    std::unique_ptr<Surface> m_composite;
    std::unique_ptr<HistoryStack> m_history;
    Selection* m_selection;  // Owned as child QObject
    CanvasWidget* m_canvas;
    Tool* m_activeTool = nullptr;
    Tool* m_previousTool = nullptr;
    int m_activeLayerIndex = 0;

    // Floating overlay for MoveTool
    std::unique_ptr<Surface> m_overlay;
    QPoint m_overlayOffset{0, 0};
    QSizeF m_overlayDisplaySize{0, 0};
};

} // namespace paintnux
