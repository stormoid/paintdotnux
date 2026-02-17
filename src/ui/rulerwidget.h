#pragma once

#include <QWidget>

namespace paintnux {

enum class RulerUnits {
    Pixels,
    Inches,
    Centimeters
};

/// Ruler widget drawn along the top or left edge of the canvas.
class RulerWidget : public QWidget {
    Q_OBJECT

public:
    enum Orientation { Horizontal, Vertical };

    explicit RulerWidget(Orientation orientation, QWidget* parent = nullptr);

    void setZoom(double zoom);
    void setOffset(double offset);      // scroll offset in document pixels
    void setCursorPos(double docPos);    // cursor position in document coords
    void setDpu(double dpu);            // dots per unit (pixels per inch/cm)
    void setUnits(RulerUnits units);

    [[nodiscard]] RulerUnits units() const { return m_units; }

    QSize sizeHint() const override;

    static constexpr int RulerThickness = 24;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Orientation m_orientation;
    double m_zoom = 1.0;
    double m_offset = 0.0;
    double m_cursorPos = -1.0;
    double m_dpu = 96.0;       // dots per unit
    RulerUnits m_units = RulerUnits::Pixels;
};

} // namespace paintnux
