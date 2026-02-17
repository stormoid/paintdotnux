#pragma once

#include "tools/tool.h"

namespace paintnux {

/// Pan tool - click and drag to pan the canvas.
class PanTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Pan"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::OpenHandCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    bool m_panning = false;
    QPointF m_lastDocPos;
};

/// Zoom tool - click to zoom in, right-click to zoom out.
class ZoomTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Zoom"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
};

} // namespace paintnux
