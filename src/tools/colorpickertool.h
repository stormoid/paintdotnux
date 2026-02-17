#pragma once

#include "tools/tool.h"

namespace paintnux {

/// Color picker (eyedropper) tool - picks color from the canvas.
class ColorPickerTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Color Picker"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;

signals:
    void colorPicked(ColorBgra color, bool primary);
    void requestToolSwitch(Tool* tool);

private:
    void pickColor(QPointF docPos, Qt::MouseButton button);
    bool m_picking = false;
    Qt::MouseButton m_button = Qt::NoButton;
};

} // namespace paintnux
