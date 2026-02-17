#include "ui/rulerwidget.h"

#include <QPainter>
#include <QPaintEvent>

#include <cmath>

namespace paintnux {

RulerWidget::RulerWidget(Orientation orientation, QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation) {
    setMouseTracking(true);
}

void RulerWidget::setZoom(double zoom) {
    m_zoom = zoom;
    update();
}

void RulerWidget::setOffset(double offset) {
    m_offset = offset;
    update();
}

void RulerWidget::setCursorPos(double docPos) {
    m_cursorPos = docPos;
    update();
}

void RulerWidget::setDpu(double dpu) {
    m_dpu = dpu;
    update();
}

void RulerWidget::setUnits(RulerUnits units) {
    m_units = units;
    update();
}

QSize RulerWidget::sizeHint() const {
    if (m_orientation == Horizontal)
        return QSize(100, RulerThickness);
    else
        return QSize(RulerThickness, 100);
}

void RulerWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());

    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    QFontMetrics fm(font);

    QPen pen(palette().windowText().color());
    p.setPen(pen);

    // Determine the unit size in document pixels
    // For Pixels: 1 unit = 1 pixel, for Inches: 1 unit = dpu pixels, for Cm: 1 unit = dpu/2.54 pixels
    double docPixelsPerUnit;
    switch (m_units) {
        case RulerUnits::Pixels:      docPixelsPerUnit = 1.0; break;
        case RulerUnits::Inches:      docPixelsPerUnit = m_dpu; break;
        case RulerUnits::Centimeters: docPixelsPerUnit = m_dpu / 2.54; break;
    }

    // Screen pixels per major unit
    double screenPixelsPerUnit = docPixelsPerUnit * m_zoom;

    // Adaptive major division: ensure labels don't overlap
    // Start with 1 unit per major tick, multiply by {2, 2.5, 2} until spacing >= 60px
    static const double divisors[] = { 2.0, 2.5, 2.0 };
    double majorSkip = 1.0;
    int skipPower = 0;
    while (screenPixelsPerUnit * majorSkip < 60.0) {
        majorSkip *= divisors[skipPower % 3];
        ++skipPower;
    }

    double majorScreenPx = screenPixelsPerUnit * majorSkip;

    int length = (m_orientation == Horizontal) ? width() : height();

    // Range of major ticks visible
    double startUnit = m_offset / docPixelsPerUnit;
    int startMajor = static_cast<int>(std::floor(startUnit / majorSkip)) - 1;
    int endMajor = static_cast<int>(std::ceil((startUnit + length / m_zoom) / majorSkip)) + 1;

    for (int major = startMajor; major <= endMajor; ++major) {
        double unitVal = major * majorSkip;
        double docPos = unitVal * docPixelsPerUnit;
        double screenPos = (docPos - m_offset) * m_zoom;

        // Major tick
        QString label;
        if (m_units == RulerUnits::Pixels) {
            label = QString::number(static_cast<int>(unitVal));
        } else {
            // Show 1 decimal if majorSkip < 1, else integer
            if (majorSkip < 1.0)
                label = QString::number(unitVal, 'f', 1);
            else
                label = QString::number(static_cast<int>(unitVal));
        }

        if (m_orientation == Horizontal) {
            int x = static_cast<int>(screenPos);
            p.drawLine(x, RulerThickness - 1, x, RulerThickness - 8);
            p.drawText(x + 2, RulerThickness - 10, label);
        } else {
            int y = static_cast<int>(screenPos);
            p.drawLine(RulerThickness - 1, y, RulerThickness - 8, y);
            p.save();
            p.translate(2, y + 2);
            p.rotate(0); // vertical text without rotation — draw chars stacked
            p.drawText(0, fm.ascent(), label);
            p.restore();
        }

        // Subdivisions: halves for inches, halves+fifths for cm, halves for pixels
        int numSubdivs;
        switch (m_units) {
            case RulerUnits::Centimeters: numSubdivs = 5; break;
            default:                      numSubdivs = 2; break;
        }

        double subScreenPx = majorScreenPx / numSubdivs;
        if (subScreenPx >= 4.0) {
            for (int s = 1; s < numSubdivs; ++s) {
                double subPos = screenPos + s * subScreenPx;
                int tickLen = (numSubdivs == 2 || s == numSubdivs / 2) ? 5 : 3;
                if (m_orientation == Horizontal) {
                    int x = static_cast<int>(subPos);
                    p.drawLine(x, RulerThickness - 1, x, RulerThickness - 1 - tickLen);
                } else {
                    int y = static_cast<int>(subPos);
                    p.drawLine(RulerThickness - 1, y, RulerThickness - 1 - tickLen, y);
                }
            }
        }
    }

    // Draw cursor position indicator
    if (m_cursorPos >= 0) {
        double cursorScreen = (m_cursorPos - m_offset) * m_zoom;
        QColor highlight = palette().highlight().color();
        highlight.setAlpha(128);
        p.setPen(Qt::NoPen);
        p.setBrush(highlight);
        if (m_orientation == Horizontal) {
            int x = static_cast<int>(cursorScreen);
            int w = std::max(1, static_cast<int>(m_zoom));
            p.drawRect(x, 0, w, RulerThickness);
        } else {
            int y = static_cast<int>(cursorScreen);
            int h = std::max(1, static_cast<int>(m_zoom));
            p.drawRect(0, y, RulerThickness, h);
        }
    }

    // Border line
    p.setPen(pen);
    if (m_orientation == Horizontal) {
        p.drawLine(0, RulerThickness - 1, width(), RulerThickness - 1);
    } else {
        p.drawLine(RulerThickness - 1, 0, RulerThickness - 1, height());
    }
}

} // namespace paintnux
