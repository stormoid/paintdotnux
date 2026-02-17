#include "tools/colorpickertool.h"
#include "ui/documentworkspace.h"

namespace paintnux {

void ColorPickerTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;
    m_picking = true;
    m_button = button;
    pickColor(docPos, button);
}

void ColorPickerTool::mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers) {
    if (!m_picking) return;
    pickColor(docPos, m_button);
}

void ColorPickerTool::mouseUp(QPointF, Qt::MouseButton, Qt::KeyboardModifiers) {
    if (!m_picking) return;
    m_picking = false;

    auto behavior = settings().colorPickerBehavior;
    if (behavior == ColorPickerBehavior::SwitchToPrevious) {
        Tool* prev = workspace()->previousActiveTool();
        if (prev) emit requestToolSwitch(prev);
    } else if (behavior == ColorPickerBehavior::SwitchToPencil) {
        emit requestToolSwitch(nullptr); // nullptr = pencil (handled by MainWindow)
    }
}

void ColorPickerTool::pickColor(QPointF docPos, Qt::MouseButton button) {
    auto* surface = workspace()->compositeSurface();
    if (!surface) return;

    int x = static_cast<int>(docPos.x());
    int y = static_cast<int>(docPos.y());

    if (!surface->isVisible(x, y)) return;

    ColorBgra color = surface->getPoint(x, y);
    bool primary = (button == Qt::LeftButton);

    if (primary) {
        settingsRef().primaryColor = color;
    } else {
        settingsRef().secondaryColor = color;
    }

    emit colorPicked(color, primary);
}

} // namespace paintnux
