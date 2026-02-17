#include "history/historystack.h"

namespace paintnux {

HistoryStack::HistoryStack(Document* document, QObject* parent)
    : QObject(parent)
    , m_document(document) {
}

void HistoryStack::pushNewMemento(std::unique_ptr<HistoryMemento> memento) {
    m_redoStack.clear();
    m_undoStack.push_back(std::move(memento));
    emit undoPushed(m_undoStack.back()->name());
    emit changed();
}

void HistoryStack::stepBackward() {
    if (!canUndo() || m_isExecuting) return;

    m_isExecuting = true;
    auto undoMemento = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    auto redoMemento = undoMemento->performUndo();
    m_redoStack.push_front(std::move(redoMemento));

    m_isExecuting = false;
    emit steppedBackward();
    emit changed();
}

void HistoryStack::stepForward() {
    if (!canRedo() || m_isExecuting) return;

    m_isExecuting = true;
    auto redoMemento = std::move(m_redoStack.front());
    m_redoStack.pop_front();

    auto undoMemento = redoMemento->performUndo();
    m_undoStack.push_back(std::move(undoMemento));

    m_isExecuting = false;
    emit steppedForward();
    emit changed();
}

QString HistoryStack::undoName(int index) const {
    int i = static_cast<int>(m_undoStack.size()) - 1 - index;
    if (i < 0 || i >= static_cast<int>(m_undoStack.size())) return {};
    return m_undoStack[i]->name();
}

QStringList HistoryStack::undoNames() const {
    QStringList names;
    for (int i = static_cast<int>(m_undoStack.size()) - 1; i >= 0; --i) {
        names.append(m_undoStack[i]->name());
    }
    return names;
}

void HistoryStack::clearAll() {
    m_undoStack.clear();
    m_redoStack.clear();
    emit historyFlushed();
    emit changed();
}

} // namespace paintnux
