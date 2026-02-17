#include "history/flipmemento.h"
#include "data/bitmaplayer.h"

namespace paintnux {

FlipMemento::FlipMemento(const QString& name, Document* document, FlipDirection direction)
    : HistoryMemento(name)
    , m_document(document)
    , m_direction(direction) {
}

void FlipMemento::applyFlip() {
    bool flipH = (m_direction == FlipDirection::Horizontal);
    bool flipV = (m_direction == FlipDirection::Vertical);

    for (int i = 0; i < m_document->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(m_document->layerAt(i));
        if (!layer) continue;

        QImage mirrored = layer->surface().qimage().mirrored(flipH, flipV);
        layer->surface() = Surface(std::move(mirrored));
    }

    emit m_document->invalidated(m_document->bounds());
}

std::unique_ptr<HistoryMemento> FlipMemento::onUndo() {
    applyFlip();  // Self-inverse: same flip undoes itself
    return std::make_unique<FlipMemento>(name(), m_document, m_direction);
}

} // namespace paintnux
