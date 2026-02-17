#pragma once

#include "data/bitmaplayer.h"
#include "core/surface.h"

#include <QObject>
#include <QRect>
#include <QSize>

#include <memory>
#include <vector>

namespace paintnux {

/// A document containing an ordered list of layers.
class Document : public QObject {
    Q_OBJECT

public:
    Document(int width, int height, QObject* parent = nullptr);
    ~Document() override = default;

    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] QSize size() const { return {m_width, m_height}; }
    [[nodiscard]] QRect bounds() const { return {0, 0, m_width, m_height}; }

    // --- Layer management ---

    [[nodiscard]] int layerCount() const { return static_cast<int>(m_layers.size()); }
    [[nodiscard]] Layer* layerAt(int index) const { return m_layers[index].get(); }

    /// Add a layer at the top of the stack.
    void addLayer(std::unique_ptr<Layer> layer);

    /// Insert a layer at the given index.
    void insertLayer(int index, std::unique_ptr<Layer> layer);

    /// Remove and return the layer at the given index.
    std::unique_ptr<Layer> removeLayer(int index);

    /// Move a layer from one index to another.
    void moveLayer(int fromIndex, int toIndex);

    // --- Rendering ---

    /// Render (composite) all visible layers into the destination surface.
    void render(Surface& dst) const;

    /// Render only within the given ROI.
    void render(Surface& dst, const QRect& roi) const;

    /// Create a fully composited surface of the entire document.
    [[nodiscard]] Surface flatten() const;

    // --- Dirty tracking ---

    [[nodiscard]] bool isDirty() const { return m_dirty; }
    void setDirty(bool dirty = true) { m_dirty = dirty; }

signals:
    void layerAdded(int index);
    void layerRemoved(int index);
    void layerChanged(int index);
    void layersReordered();
    void invalidated(const QRect& rect);

private:
    void connectLayer(Layer* layer, int index);
    void renderStrip(Surface& dst, const QRect& strip) const;

    int m_width;
    int m_height;
    bool m_dirty = false;
    std::vector<std::unique_ptr<Layer>> m_layers;
};

} // namespace paintnux
