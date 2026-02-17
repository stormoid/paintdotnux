#pragma once

#include "history/historystack.h"
#include "core/surface.h"
#include "data/document.h"

#include <QRect>
#include <QRegion>

namespace paintnux {

/// History memento that saves/restores a region of a bitmap layer's surface.
/// Implements the symmetric swap pattern: undoing swaps saved pixels with current pixels.
class BitmapHistoryMemento : public HistoryMemento {
public:
    /// Save the given region of the layer's surface for undo.
    /// Copies pixels from the layer at construction time.
    BitmapHistoryMemento(const QString& name,
                         Document* document,
                         int layerIndex,
                         const QRegion& savedRegion);

    /// Save using an already-captured surface (e.g. from scratch surface).
    /// Takes ownership of savedData via move.
    BitmapHistoryMemento(const QString& name,
                         Document* document,
                         int layerIndex,
                         const QRegion& savedRegion,
                         Surface savedData);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_layerIndex;
    QRegion m_savedRegion;
    Surface m_savedData;
};

} // namespace paintnux
