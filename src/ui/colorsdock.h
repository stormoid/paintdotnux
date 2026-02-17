#pragma once

#include "core/colorbgra.h"

#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QSpinBox>
#include <QWidget>
#include <QImage>

namespace paintnux {

/// HSV hue ring + saturation/value square widget.
class ColorWheelWidget : public QWidget {
    Q_OBJECT

public:
    explicit ColorWheelWidget(QWidget* parent = nullptr);

    void setColor(const QColor& color);
    [[nodiscard]] QColor color() const;

    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int w) const override { return w; }

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildImages();
    void handleMouse(QPoint pos);
    QPointF svSquareTopLeft() const;
    qreal svSquareSize() const;

    int m_hue = 0;         // 0-359
    qreal m_sat = 1.0;     // 0-1
    qreal m_val = 1.0;     // 0-1
    int m_alpha = 255;

    qreal m_ringInnerRadius = 0;
    qreal m_ringOuterRadius = 0;

    bool m_draggingRing = false;
    bool m_draggingSquare = false;

    QImage m_ringImage;
    QImage m_squareImage;
    bool m_dirty = true;
};

/// Dock showing an embedded color picker with HSV wheel, hex input, and swatches.
class ColorsDock : public QDockWidget {
    Q_OBJECT

public:
    explicit ColorsDock(QWidget* parent = nullptr);

    [[nodiscard]] ColorBgra primaryColor() const { return m_primary; }
    [[nodiscard]] ColorBgra secondaryColor() const { return m_secondary; }

    void setPrimaryColor(ColorBgra color);
    void setSecondaryColor(ColorBgra color);

signals:
    void primaryColorChanged(ColorBgra color);
    void secondaryColorChanged(ColorBgra color);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void updateSwatches();
    void updateFromWheel(const QColor& color);
    void updateFromHex();
    void updateFromAlpha(int alpha);
    void updateFromRgb();
    void updateFromHsv();
    void syncControlsToColor(ColorBgra color);
    void swapColors();

    bool m_editingPrimary = true;
    bool m_updating = false;

    ColorBgra m_primary = ColorBgra::black();
    ColorBgra m_secondary = ColorBgra::white();

    ColorWheelWidget* m_wheel;
    QLineEdit* m_hexEdit;
    QSlider* m_alphaSlider;
    QSpinBox* m_alphaSpin;

    // HSV sliders + spin boxes
    QSlider* m_hueSlider;
    QSpinBox* m_hueSpin;
    QSlider* m_satSlider;
    QSpinBox* m_satSpin;
    QSlider* m_valSlider;
    QSpinBox* m_valSpin;

    // RGB sliders + spin boxes
    QSlider* m_redSlider;
    QSpinBox* m_redSpin;
    QSlider* m_greenSlider;
    QSpinBox* m_greenSpin;
    QSlider* m_blueSlider;
    QSpinBox* m_blueSpin;

    QLabel* m_primarySwatch;
    QLabel* m_secondarySwatch;
};

} // namespace paintnux
