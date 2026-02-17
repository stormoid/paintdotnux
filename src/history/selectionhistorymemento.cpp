#include "history/selectionhistorymemento.h"
#include "data/selection.h"

namespace paintnux {

SelectionHistoryMemento::SelectionHistoryMemento(const QString& name, Selection* selection)
    : HistoryMemento(name)
    , m_selection(selection)
    , m_savedPath(selection->path()) {
}

SelectionHistoryMemento::SelectionHistoryMemento(const QString& name, Selection* selection, QPainterPath savedPath)
    : HistoryMemento(name)
    , m_selection(selection)
    , m_savedPath(std::move(savedPath)) {
}

std::unique_ptr<HistoryMemento> SelectionHistoryMemento::onUndo() {
    // Save current state for redo
    QPainterPath currentPath = m_selection->path();

    // Restore saved path
    m_selection->setPath(m_savedPath);

    // Return redo memento with the state we just replaced
    return std::make_unique<SelectionHistoryMemento>(name(), m_selection, std::move(currentPath));
}

} // namespace paintnux
