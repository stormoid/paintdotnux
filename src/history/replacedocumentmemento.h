#pragma once

#include "history/historystack.h"
#include "data/document.h"

#include <functional>
#include <memory>
#include <utility>

namespace paintnux {

/// Callback type for replacing the document in the workspace.
/// Takes the new document + active layer index, returns the old document + old active index.
using ReplaceDocumentFn = std::function<
    std::pair<std::unique_ptr<Document>, int>(std::unique_ptr<Document> newDoc, int activeLayerIndex)>;

/// History memento that swaps the entire Document for undo/redo.
/// Used by dimension-changing operations (resize, canvas size, rotate, crop, flatten).
class ReplaceDocumentMemento : public HistoryMemento {
public:
    ReplaceDocumentMemento(const QString& name,
                           ReplaceDocumentFn replaceFn,
                           std::unique_ptr<Document> savedDocument,
                           int savedActiveLayerIndex);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    ReplaceDocumentFn m_replaceFn;
    std::unique_ptr<Document> m_savedDocument;
    int m_savedActiveLayerIndex;
};

} // namespace paintnux
