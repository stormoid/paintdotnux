#include "data/document.h"

#include <QThread>
#include <QtConcurrent>

#include <algorithm>
#include <cstring>

namespace paintnux {

Document::Document(int width, int height, QObject* parent)
    : QObject(parent)
    , m_width(width)
    , m_height(height) {
}

void Document::addLayer(std::unique_ptr<Layer> layer) {
    int index = layerCount();
    connectLayer(layer.get(), index);
    m_layers.push_back(std::move(layer));
    m_dirty = true;
    emit layerAdded(index);
    emit invalidated(bounds());
}

void Document::insertLayer(int index, std::unique_ptr<Layer> layer) {
    connectLayer(layer.get(), index);
    m_layers.insert(m_layers.begin() + index, std::move(layer));
    m_dirty = true;
    emit layerAdded(index);
    emit invalidated(bounds());
}

std::unique_ptr<Layer> Document::removeLayer(int index) {
    auto layer = std::move(m_layers[index]);
    layer->disconnect(this);
    m_layers.erase(m_layers.begin() + index);
    m_dirty = true;
    emit layerRemoved(index);
    emit invalidated(bounds());
    return layer;
}

void Document::moveLayer(int fromIndex, int toIndex) {
    if (fromIndex == toIndex) return;
    auto layer = std::move(m_layers[fromIndex]);
    m_layers.erase(m_layers.begin() + fromIndex);
    m_layers.insert(m_layers.begin() + toIndex, std::move(layer));
    m_dirty = true;
    emit layersReordered();
    emit invalidated(bounds());
}

void Document::connectLayer(Layer* layer, int /*index*/) {
    connect(layer, &Layer::invalidated, this, [this](const QRect& rect) {
        m_dirty = true;
        emit invalidated(rect);
    });
    connect(layer, &Layer::propertyChanged, this, [this, layer]() {
        int idx = -1;
        for (int i = 0; i < layerCount(); ++i) {
            if (layerAt(i) == layer) { idx = i; break; }
        }
        if (idx >= 0) emit layerChanged(idx);
    });
}

void Document::render(Surface& dst) const {
    render(dst, bounds());
}

void Document::render(Surface& dst, const QRect& roi) const {
    QRect clipped = roi.intersected(bounds()).intersected(dst.bounds());
    if (clipped.isEmpty()) return;

    int height = clipped.height();
    int threadCount = std::max(1, QThread::idealThreadCount());

    // For small regions, don't bother with threading overhead
    if (height < 64 || threadCount <= 1) {
        renderStrip(dst, clipped);
        return;
    }

    // Split into horizontal strips across CPU cores
    int stripHeight = std::max(1, height / threadCount);
    QList<QRect> strips;
    for (int y = clipped.top(); y <= clipped.bottom(); y += stripHeight) {
        int bottom = std::min(y + stripHeight - 1, clipped.bottom());
        strips.append(QRect(clipped.left(), y, clipped.width(), bottom - y + 1));
    }

    QtConcurrent::blockingMap(strips, [this, &dst](const QRect& strip) {
        renderStrip(dst, strip);
    });
}

void Document::renderStrip(Surface& dst, const QRect& strip) const {
    // Optimization: if the first visible layer is an opaque background BitmapLayer,
    // copy its pixels directly instead of clearing + blending.
    bool backgroundCopied = false;
    if (!m_layers.empty()) {
        Layer* first = m_layers[0].get();
        auto* bmpFirst = dynamic_cast<BitmapLayer*>(first);
        if (bmpFirst && first->isVisible() && first->opacity() == 255) {
            for (int y = strip.top(); y <= strip.bottom(); ++y) {
                const ColorBgra* srcRow = bmpFirst->surface().rowPtr(y) + strip.left();
                ColorBgra* dstRow = dst.rowPtr(y) + strip.left();
                std::memcpy(dstRow, srcRow, strip.width() * sizeof(ColorBgra));
            }
            backgroundCopied = true;
        }
    }

    if (!backgroundCopied) {
        dst.clear(strip, ColorBgra::transparent());
    }

    int startIndex = backgroundCopied ? 1 : 0;
    for (int i = startIndex; i < layerCount(); ++i) {
        Layer* layer = m_layers[i].get();
        if (layer->isVisible()) {
            layer->render(dst, strip);
        }
    }
}

Surface Document::flatten() const {
    Surface result(m_width, m_height);
    render(result);
    return result;
}

} // namespace paintnux
