#include "data/layer.h"

namespace paintnux {

Layer::Layer(int width, int height, QObject* parent)
    : QObject(parent)
    , m_width(width)
    , m_height(height) {
}

void Layer::setName(const QString& name) {
    if (m_name != name) {
        m_name = name;
        emit propertyChanged();
    }
}

void Layer::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        invalidate();
        emit propertyChanged();
    }
}

void Layer::setOpacity(uint8_t opacity) {
    if (m_opacity != opacity) {
        m_opacity = opacity;
        invalidate();
        emit propertyChanged();
    }
}

void Layer::invalidate() {
    emit invalidated(bounds());
}

void Layer::invalidate(const QRect& rect) {
    emit invalidated(rect.intersected(bounds()));
}

} // namespace paintnux
