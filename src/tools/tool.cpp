#include "tools/tool.h"
#include "ui/documentworkspace.h"
#include "data/document.h"
#include "data/bitmaplayer.h"
#include "history/historystack.h"

namespace paintnux {

Tool::Tool(DocumentWorkspace* workspace, QObject* parent)
    : QObject(parent)
    , m_workspace(workspace) {
}

Document* Tool::document() const {
    return m_workspace ? m_workspace->document() : nullptr;
}

HistoryStack* Tool::history() const {
    return m_workspace ? m_workspace->historyStack() : nullptr;
}

BitmapLayer* Tool::activeLayer() const {
    auto* doc = document();
    if (!doc || doc->layerCount() == 0) return nullptr;
    int idx = m_workspace->activeLayerIndex();
    if (idx < 0 || idx >= doc->layerCount()) return nullptr;
    return dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
}

void Tool::activate() {}
void Tool::deactivate() {}

void Tool::mouseDown(QPointF, Qt::MouseButton, Qt::KeyboardModifiers) {}
void Tool::mouseMove(QPointF, Qt::MouseButtons, Qt::KeyboardModifiers) {}
void Tool::mouseUp(QPointF, Qt::MouseButton, Qt::KeyboardModifiers) {}
void Tool::keyDown(QKeyEvent*) {}
void Tool::keyUp(QKeyEvent*) {}

void Tool::invalidateCanvas() {
    if (m_workspace) m_workspace->invalidateAll();
}

void Tool::invalidateCanvas(const QRect& rect) {
    if (m_workspace) m_workspace->invalidateAll(); // TODO: partial invalidation
}

} // namespace paintnux
