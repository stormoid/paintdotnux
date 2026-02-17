#include "history/bitmaphistorymemento.h"
#include "data/bitmaplayer.h"

#include <cstring>

namespace paintnux {

BitmapHistoryMemento::BitmapHistoryMemento(
    const QString& name,
    Document* document,
    int layerIndex,
    const QRegion& savedRegion)
    : HistoryMemento(name)
    , m_document(document)
    , m_layerIndex(layerIndex)
    , m_savedRegion(savedRegion)
    , m_savedData(document->width(), document->height()) {
    // Copy pixels from the current layer surface into our saved data
    auto* layer = dynamic_cast<BitmapLayer*>(document->layerAt(layerIndex));
    if (!layer) return;

    const Surface& src = layer->surface();
    for (const QRect& rect : m_savedRegion) {
        QRect clipped = rect.intersected(src.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            const ColorBgra* srcRow = src.rowPtr(y) + clipped.left();
            ColorBgra* dstRow = m_savedData.rowPtr(y) + clipped.left();
            std::memcpy(dstRow, srcRow, clipped.width() * sizeof(ColorBgra));
        }
    }
}

BitmapHistoryMemento::BitmapHistoryMemento(
    const QString& name,
    Document* document,
    int layerIndex,
    const QRegion& savedRegion,
    Surface savedData)
    : HistoryMemento(name)
    , m_document(document)
    , m_layerIndex(layerIndex)
    , m_savedRegion(savedRegion)
    , m_savedData(std::move(savedData)) {
}

std::unique_ptr<HistoryMemento> BitmapHistoryMemento::onUndo() {
    auto* layer = dynamic_cast<BitmapLayer*>(m_document->layerAt(m_layerIndex));
    if (!layer) return nullptr;

    Surface& layerSurf = layer->surface();

    // Create redo memento with CURRENT state before we overwrite
    auto redo = std::make_unique<BitmapHistoryMemento>(
        name(), m_document, m_layerIndex, m_savedRegion);

    // Restore saved pixels to the layer
    for (const QRect& rect : m_savedRegion) {
        QRect clipped = rect.intersected(layerSurf.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            const ColorBgra* srcRow = m_savedData.rowPtr(y) + clipped.left();
            ColorBgra* dstRow = layerSurf.rowPtr(y) + clipped.left();
            std::memcpy(dstRow, srcRow, clipped.width() * sizeof(ColorBgra));
        }
    }

    return redo;
}

} // namespace paintnux
