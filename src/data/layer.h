#pragma once

#include "core/surface.h"

#include <QObject>
#include <QRect>
#include <QString>

#include <memory>

namespace paintnux {

/// Abstract base class for document layers.
class Layer : public QObject {
    Q_OBJECT

public:
    Layer(int width, int height, QObject* parent = nullptr);
    ~Layer() override = default;

    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }
    [[nodiscard]] QSize size() const { return {m_width, m_height}; }
    [[nodiscard]] QRect bounds() const { return {0, 0, m_width, m_height}; }

    [[nodiscard]] const QString& name() const { return m_name; }
    void setName(const QString& name);

    [[nodiscard]] bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    [[nodiscard]] uint8_t opacity() const { return m_opacity; }
    void setOpacity(uint8_t opacity);

    [[nodiscard]] bool isBackground() const { return m_isBackground; }
    void setIsBackground(bool bg) { m_isBackground = bg; }

    /// Render this layer onto the destination surface within the given ROI.
    virtual void render(Surface& dst, const QRect& roi) const = 0;

    /// Create a thumbnail of this layer.
    [[nodiscard]] virtual Surface renderThumbnail(int maxEdge) const = 0;

    /// Clone this layer.
    [[nodiscard]] virtual std::unique_ptr<Layer> clone() const = 0;

signals:
    void invalidated(const QRect& rect);
    void propertyChanged();

protected:
    void invalidate();
    void invalidate(const QRect& rect);

private:
    int m_width;
    int m_height;
    QString m_name;
    bool m_visible = true;
    bool m_isBackground = false;
    uint8_t m_opacity = 255;
};

} // namespace paintnux
