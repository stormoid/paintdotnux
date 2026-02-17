#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <vector>

namespace paintnux {

class GradientTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Gradient"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

private:
    void renderGradient(QPointF endPos, Qt::KeyboardModifiers mods);
    void commitToHistory();
    static QPointF constrainShift(QPointF start, QPointF end);

    bool m_drawing = false;
    Qt::MouseButton m_button = Qt::NoButton;
    QPointF m_startPos;
    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;

    // Cached selection mask (built once on mouseDown)
    std::vector<uint8_t> m_selMask;
    bool m_hasSel = false;
};

} // namespace paintnux
