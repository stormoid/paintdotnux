#include "tools/navtools.h"
#include "ui/documentworkspace.h"
#include "ui/canvaswidget.h"

namespace paintnux {

// --- PanTool ---

void PanTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;
    m_panning = true;
    m_lastDocPos = docPos;
    emit cursorChanged(Qt::ClosedHandCursor);
}

void PanTool::mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers) {
    if (!m_panning) return;
    QPointF delta = docPos - m_lastDocPos;
    auto* canvas = workspace()->canvas();
    canvas->setScrollPosition(canvas->scrollPosition() - delta);
    // Don't update m_lastDocPos — it's in document coords and the scroll changed
}

void PanTool::mouseUp(QPointF, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton) return;
    m_panning = false;
    emit cursorChanged(Qt::OpenHandCursor);
}

// --- ZoomTool ---

void ZoomTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    auto* canvas = workspace()->canvas();
    if (button == Qt::LeftButton) {
        canvas->zoomInCoarse();
    } else if (button == Qt::RightButton) {
        canvas->zoomOutCoarse();
    }
    canvas->centerOn(docPos);
}

} // namespace paintnux
