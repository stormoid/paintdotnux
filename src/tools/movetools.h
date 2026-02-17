#pragma once

#include "tools/tool.h"
#include "tools/selecttools.h"

#include <QPointF>
#include <QPoint>
#include <QPainterPath>

namespace paintnux {

/// MoveTool — lifts selected pixels into a floating overlay, drags to reposition, commits on drop.
/// Also handles externally-created overlays (e.g. from Paste).
class MoveTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Move Pixels"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::SizeAllCursor; }

    void activate() override;
    void deactivate() override;

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

    /// Reset internal state and adopt the current workspace overlay (e.g. after paste).
    /// Does NOT commit anything.
    void adoptOverlay();

    /// Get the selection path saved at the start of the current drag.
    [[nodiscard]] const QPainterPath& savedSelectionPath() const { return m_savedSelPath; }

private:
    void liftPixels();
    void commitPixels();

    bool m_dragging = false;
    bool m_lifted = false;       // We have a floating overlay (from lift or paste)
    QPointF m_lastDocPos;
    QPoint m_currentOffset{0, 0};
    QPainterPath m_savedSelPath;  // Selection path at drag start (for undo)
    SelectionResizeHelper m_resizeHelper;

    // Overlay state saved at resize start (for scaling overlay with selection)
    QPoint m_resizeOrigOverlayOffset{0, 0};
    QSizeF m_resizeOrigOverlaySize{0, 0};
};

/// MoveSelectionTool — moves the selection outline only (translates QPainterPath).
class MoveSelectionTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Move Selection"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::SizeAllCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    bool m_dragging = false;
    QPointF m_lastDocPos;
    QPainterPath m_savedPath; // for undo
    SelectionResizeHelper m_resizeHelper;
};

} // namespace paintnux
