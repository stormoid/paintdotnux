#pragma once

#include "history/historystack.h"

#include <QPainterPath>

namespace paintnux {

class Selection;

/// History memento that saves/restores a Selection's QPainterPath.
/// Implements the symmetric swap pattern: undo swaps saved path with current.
/// Takes Selection* directly (not DocumentWorkspace) to avoid circular dependency.
class SelectionHistoryMemento : public HistoryMemento {
public:
    SelectionHistoryMemento(const QString& name, Selection* selection);

    /// Construct with a pre-captured path.
    SelectionHistoryMemento(const QString& name, Selection* selection, QPainterPath savedPath);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Selection* m_selection;
    QPainterPath m_savedPath;
};

} // namespace paintnux
