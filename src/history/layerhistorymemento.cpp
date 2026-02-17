#include "history/layerhistorymemento.h"

#include <cstring>

namespace paintnux {

// --- LayerPropertyMemento ---

LayerPropertyMemento::LayerPropertyMemento(const QString& mementoName,
                                           Document* document,
                                           int layerIndex,
                                           Property prop,
                                           QVariant savedValue)
    : HistoryMemento(mementoName)
    , m_document(document)
    , m_layerIndex(layerIndex)
    , m_property(prop)
    , m_savedValue(std::move(savedValue)) {
}

std::unique_ptr<HistoryMemento> LayerPropertyMemento::onUndo() {
    Layer* layer = m_document->layerAt(m_layerIndex);
    QVariant currentValue;

    switch (m_property) {
    case Name:
        currentValue = layer->name();
        layer->setName(m_savedValue.toString());
        break;
    case Visible:
        currentValue = layer->isVisible();
        layer->setVisible(m_savedValue.toBool());
        break;
    case Opacity:
        currentValue = static_cast<int>(layer->opacity());
        layer->setOpacity(static_cast<uint8_t>(m_savedValue.toInt()));
        break;
    case BlendMode: {
        auto* bmpLayer = dynamic_cast<BitmapLayer*>(layer);
        if (bmpLayer) {
            currentValue = static_cast<int>(bmpLayer->blendMode());
            bmpLayer->setBlendMode(static_cast<paintnux::BlendMode>(m_savedValue.toInt()));
        }
        break;
    }
    }

    return std::make_unique<LayerPropertyMemento>(
        name(), m_document, m_layerIndex, m_property, currentValue);
}

// --- AddLayerMemento ---

AddLayerMemento::AddLayerMemento(const QString& mementoName,
                                 Document* document,
                                 int insertIndex,
                                 SetActiveIndexFn setActiveIndex,
                                 int savedActiveIndex)
    : HistoryMemento(mementoName)
    , m_document(document)
    , m_insertIndex(insertIndex)
    , m_setActiveIndex(std::move(setActiveIndex))
    , m_savedActiveIndex(savedActiveIndex) {
}

std::unique_ptr<HistoryMemento> AddLayerMemento::onUndo() {
    // Undo add = remove the layer
    int currentActive = m_savedActiveIndex; // doesn't matter, we restore below
    // The layer we added is at m_insertIndex; capture current active before removal
    // (we need to figure out what the active index is now — but we don't have it,
    //  so we save the new active index as m_insertIndex since that's what was set after add)
    auto removed = m_document->removeLayer(m_insertIndex);
    m_setActiveIndex(m_savedActiveIndex);

    return std::make_unique<DeleteLayerMemento>(
        name(), m_document, m_insertIndex, std::move(removed),
        m_setActiveIndex, m_insertIndex);
}

// --- DeleteLayerMemento ---

DeleteLayerMemento::DeleteLayerMemento(const QString& mementoName,
                                       Document* document,
                                       int removedIndex,
                                       std::unique_ptr<Layer> savedLayer,
                                       SetActiveIndexFn setActiveIndex,
                                       int savedActiveIndex)
    : HistoryMemento(mementoName)
    , m_document(document)
    , m_removedIndex(removedIndex)
    , m_savedLayer(std::move(savedLayer))
    , m_setActiveIndex(std::move(setActiveIndex))
    , m_savedActiveIndex(savedActiveIndex) {
}

std::unique_ptr<HistoryMemento> DeleteLayerMemento::onUndo() {
    // Undo delete = re-insert the saved layer
    int currentActive = m_savedActiveIndex; // active index after deletion was applied
    m_document->insertLayer(m_removedIndex, std::move(m_savedLayer));
    m_setActiveIndex(m_savedActiveIndex);

    return std::make_unique<AddLayerMemento>(
        name(), m_document, m_removedIndex,
        m_setActiveIndex, m_removedIndex);
}

// --- MoveLayerMemento ---

MoveLayerMemento::MoveLayerMemento(const QString& mementoName,
                                   Document* document,
                                   int fromIndex,
                                   int toIndex,
                                   SetActiveIndexFn setActiveIndex,
                                   int savedActiveIndex)
    : HistoryMemento(mementoName)
    , m_document(document)
    , m_fromIndex(fromIndex)
    , m_toIndex(toIndex)
    , m_setActiveIndex(std::move(setActiveIndex))
    , m_savedActiveIndex(savedActiveIndex) {
}

std::unique_ptr<HistoryMemento> MoveLayerMemento::onUndo() {
    // Undo: move from toIndex back to fromIndex
    m_document->moveLayer(m_toIndex, m_fromIndex);
    int newActive = m_toIndex; // the active index after the original move
    m_setActiveIndex(m_savedActiveIndex);

    return std::make_unique<MoveLayerMemento>(
        name(), m_document, m_toIndex, m_fromIndex,
        m_setActiveIndex, m_toIndex);
}

// --- MergeLayerDownMemento ---

MergeLayerDownMemento::MergeLayerDownMemento(const QString& mementoName,
                                             Document* document,
                                             int mergeIndex,
                                             std::unique_ptr<Layer> savedTopLayer,
                                             Surface savedBottomSurface,
                                             SetActiveIndexFn setActiveIndex,
                                             int savedActiveIndex)
    : HistoryMemento(mementoName)
    , m_document(document)
    , m_mergeIndex(mergeIndex)
    , m_savedTopLayer(std::move(savedTopLayer))
    , m_savedBottomSurface(std::move(savedBottomSurface))
    , m_setActiveIndex(std::move(setActiveIndex))
    , m_savedActiveIndex(savedActiveIndex) {
}

std::unique_ptr<HistoryMemento> MergeLayerDownMemento::onUndo() {
    // The merge removed the top layer (at m_mergeIndex) and blended its pixels
    // into the bottom layer (now at m_mergeIndex - 1).
    // Undo: restore bottom layer pixels, re-insert the top layer.
    int bottomIndex = m_mergeIndex - 1;
    auto* bottomLayer = dynamic_cast<BitmapLayer*>(m_document->layerAt(bottomIndex));

    // Save current bottom surface for redo
    Surface currentBottomSurface(bottomLayer->surface().width(), bottomLayer->surface().height());
    for (int y = 0; y < currentBottomSurface.height(); ++y) {
        std::memcpy(currentBottomSurface.rowPtr(y),
                     bottomLayer->surface().rowPtr(y),
                     currentBottomSurface.width() * sizeof(ColorBgra));
    }

    // Restore bottom surface
    for (int y = 0; y < m_savedBottomSurface.height(); ++y) {
        std::memcpy(bottomLayer->surface().rowPtr(y),
                     m_savedBottomSurface.rowPtr(y),
                     m_savedBottomSurface.width() * sizeof(ColorBgra));
    }

    // Clone saved top layer for redo (we need to keep a copy)
    auto topClone = m_savedTopLayer->clone();

    // Re-insert top layer
    m_document->insertLayer(m_mergeIndex, std::move(m_savedTopLayer));
    m_setActiveIndex(m_savedActiveIndex);

    // Return redo memento: will re-merge
    return std::make_unique<MergeLayerDownMemento>(
        name(), m_document, m_mergeIndex,
        std::move(topClone), std::move(currentBottomSurface),
        m_setActiveIndex, m_mergeIndex - 1);
}

} // namespace paintnux
