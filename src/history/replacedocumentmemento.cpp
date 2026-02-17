#include "history/replacedocumentmemento.h"

namespace paintnux {

ReplaceDocumentMemento::ReplaceDocumentMemento(const QString& name,
                                               ReplaceDocumentFn replaceFn,
                                               std::unique_ptr<Document> savedDocument,
                                               int savedActiveLayerIndex)
    : HistoryMemento(name)
    , m_replaceFn(std::move(replaceFn))
    , m_savedDocument(std::move(savedDocument))
    , m_savedActiveLayerIndex(savedActiveLayerIndex) {
}

std::unique_ptr<HistoryMemento> ReplaceDocumentMemento::onUndo() {
    // Swap: give the saved document to the workspace, get back the current one
    auto [oldDoc, oldActiveIdx] = m_replaceFn(std::move(m_savedDocument), m_savedActiveLayerIndex);

    // Return a reverse memento holding what we just took out
    return std::make_unique<ReplaceDocumentMemento>(
        name(), m_replaceFn, std::move(oldDoc), oldActiveIdx);
}

} // namespace paintnux
