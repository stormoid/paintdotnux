#include "data/bitmaplayer.h"

#include <algorithm>

namespace paintnux {

BitmapLayer::BitmapLayer(int width, int height, QObject* parent)
    : Layer(width, height, parent)
    , m_surface(width, height)
    , m_blendOp(std::make_unique<NormalBlendOp>()) {
    compileBlendOp();
    connect(this, &Layer::propertyChanged, this, [this]() { compileBlendOp(); });
}

BitmapLayer::BitmapLayer(int width, int height, ColorBgra fillColor, QObject* parent)
    : Layer(width, height, parent)
    , m_surface(width, height)
    , m_blendOp(std::make_unique<NormalBlendOp>()) {
    m_surface.clear(fillColor);
    compileBlendOp();
    connect(this, &Layer::propertyChanged, this, [this]() { compileBlendOp(); });
}

BitmapLayer::BitmapLayer(Surface surface, QObject* parent)
    : Layer(surface.width(), surface.height(), parent)
    , m_surface(std::move(surface))
    , m_blendOp(std::make_unique<NormalBlendOp>()) {
    compileBlendOp();
    connect(this, &Layer::propertyChanged, this, [this]() { compileBlendOp(); });
}

void BitmapLayer::setBlendOp(std::unique_ptr<UserBlendOp> op) {
    m_blendOp = std::move(op);
    compileBlendOp();
    invalidate();
    emit propertyChanged();
}

void BitmapLayer::setBlendMode(BlendMode mode) {
    if (m_blendMode == mode) return;
    m_blendMode = mode;
    setBlendOp(createBlendOp(mode));
}

void BitmapLayer::compileBlendOp() {
    m_compiledBlendOp = m_blendOp->createWithOpacity(opacity());
}

void BitmapLayer::render(Surface& dst, const QRect& roi) const {
    if (opacity() == 0 || !isVisible()) return;

    QRect clipped = roi.intersected(bounds()).intersected(dst.bounds());
    if (clipped.isEmpty()) return;

    for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        ColorBgra* dstRow = dst.rowPtr(y) + clipped.left();
        const ColorBgra* srcRow = m_surface.rowPtr(y) + clipped.left();
        m_compiledBlendOp->apply(dstRow, srcRow, clipped.width());
    }
}

Surface BitmapLayer::renderThumbnail(int maxEdge) const {
    int tw, th;
    if (width() >= height()) {
        tw = maxEdge;
        th = std::max(1, height() * maxEdge / width());
    } else {
        th = maxEdge;
        tw = std::max(1, width() * maxEdge / height());
    }

    Surface thumb(tw, th);

    // Simple nearest-neighbor downsample for thumbnail
    for (int y = 0; y < th; ++y) {
        int sy = y * height() / th;
        ColorBgra* dstRow = thumb.rowPtr(y);
        const ColorBgra* srcRow = m_surface.rowPtr(sy);
        for (int x = 0; x < tw; ++x) {
            int sx = x * width() / tw;
            dstRow[x] = srcRow[sx];
        }
    }

    return thumb;
}

std::unique_ptr<Layer> BitmapLayer::clone() const {
    auto layer = std::make_unique<BitmapLayer>(m_surface.clone());
    layer->setName(name());
    layer->setVisible(isVisible());
    layer->setOpacity(opacity());
    layer->setIsBackground(isBackground());
    layer->setBlendMode(m_blendMode);
    return layer;
}

std::unique_ptr<BitmapLayer> BitmapLayer::createBackground(int width, int height) {
    auto layer = std::make_unique<BitmapLayer>(width, height, ColorBgra::white());
    layer->setName(QStringLiteral("Background"));
    layer->setIsBackground(true);
    return layer;
}

} // namespace paintnux
