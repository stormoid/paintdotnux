#include "ui/colorsdock.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

#include <cmath>

namespace paintnux {

static QColor toQColor(ColorBgra c) {
    return QColor(c.r, c.g, c.b, c.a);
}

static ColorBgra fromQColor(const QColor& c) {
    return ColorBgra::fromBgra(c.blue(), c.green(), c.red(), c.alpha());
}

// ==========================================================================
// ColorWheelWidget
// ==========================================================================

ColorWheelWidget::ColorWheelWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(120, 120);
    setMaximumSize(170, 170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ColorWheelWidget::setColor(const QColor& color) {
    int h, s, v;
    color.getHsv(&h, &s, &v);
    if (h < 0) h = 0;
    m_hue = h;
    m_sat = s / 255.0;
    m_val = v / 255.0;
    m_alpha = color.alpha();
    m_dirty = true;
    update();
}

QColor ColorWheelWidget::color() const {
    QColor c = QColor::fromHsv(m_hue, static_cast<int>(m_sat * 255),
                                static_cast<int>(m_val * 255));
    c.setAlpha(m_alpha);
    return c;
}

qreal ColorWheelWidget::svSquareSize() const {
    return m_ringInnerRadius * std::sqrt(2.0);
}

QPointF ColorWheelWidget::svSquareTopLeft() const {
    qreal sz = svSquareSize();
    qreal cx = width() / 2.0;
    qreal cy = height() / 2.0;
    return QPointF(cx - sz / 2.0, cy - sz / 2.0);
}

void ColorWheelWidget::rebuildImages() {
    int side = std::min(width(), height());
    m_ringOuterRadius = side / 2.0 - 2;
    m_ringInnerRadius = m_ringOuterRadius - std::max(12.0, side * 0.08);

    // Hue ring image
    int imgSize = side;
    m_ringImage = QImage(imgSize, imgSize, QImage::Format_ARGB32_Premultiplied);
    m_ringImage.fill(0);
    qreal cx = imgSize / 2.0;
    qreal cy = imgSize / 2.0;

    for (int y = 0; y < imgSize; ++y) {
        auto* row = reinterpret_cast<QRgb*>(m_ringImage.scanLine(y));
        for (int x = 0; x < imgSize; ++x) {
            qreal dx = x - cx;
            qreal dy = y - cy;
            qreal dist = std::sqrt(dx * dx + dy * dy);
            // 1-pixel antialiased fringe on both edges
            qreal alpha = 1.0;
            if (dist < m_ringInnerRadius - 0.5 || dist > m_ringOuterRadius + 0.5)
                continue;
            if (dist < m_ringInnerRadius + 0.5)
                alpha = dist - (m_ringInnerRadius - 0.5);
            else if (dist > m_ringOuterRadius - 0.5)
                alpha = (m_ringOuterRadius + 0.5) - dist;

            qreal angle = std::atan2(dy, dx);
            int hue = static_cast<int>((angle / (2 * M_PI) + 0.5) * 360) % 360;
            if (hue < 0) hue += 360;
            QColor c = QColor::fromHsv(hue, 255, 255);
            int a = std::clamp(static_cast<int>(alpha * 255 + 0.5), 0, 255);
            int r = c.red() * a / 255;
            int g = c.green() * a / 255;
            int b = c.blue() * a / 255;
            row[x] = qRgba(r, g, b, a);
        }
    }

    // SV square image
    int sqSz = static_cast<int>(svSquareSize());
    if (sqSz < 2) sqSz = 2;
    m_squareImage = QImage(sqSz, sqSz, QImage::Format_ARGB32);
    for (int y = 0; y < sqSz; ++y) {
        auto* row = reinterpret_cast<QRgb*>(m_squareImage.scanLine(y));
        qreal val = 1.0 - static_cast<qreal>(y) / (sqSz - 1);
        for (int x = 0; x < sqSz; ++x) {
            qreal sat = static_cast<qreal>(x) / (sqSz - 1);
            row[x] = QColor::fromHsvF(m_hue / 360.0, sat, val).rgb();
        }
    }

    m_dirty = false;
}

void ColorWheelWidget::paintEvent(QPaintEvent*) {
    if (m_dirty || m_ringImage.isNull()) {
        rebuildImages();
    } else {
        // Rebuild square when hue changes
        int sqSz = static_cast<int>(svSquareSize());
        if (sqSz < 2) sqSz = 2;
        m_squareImage = QImage(sqSz, sqSz, QImage::Format_ARGB32);
        for (int y = 0; y < sqSz; ++y) {
            auto* row = reinterpret_cast<QRgb*>(m_squareImage.scanLine(y));
            qreal val = 1.0 - static_cast<qreal>(y) / (sqSz - 1);
            for (int x = 0; x < sqSz; ++x) {
                qreal sat = static_cast<qreal>(x) / (sqSz - 1);
                row[x] = QColor::fromHsvF(m_hue / 360.0, sat, val).rgb();
            }
        }
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw hue ring
    qreal cx = width() / 2.0;
    qreal cy = height() / 2.0;
    QPointF imgOffset((width() - m_ringImage.width()) / 2.0,
                      (height() - m_ringImage.height()) / 2.0);
    p.drawImage(imgOffset, m_ringImage);

    // Draw SV square
    QPointF sqTL = svSquareTopLeft();
    p.drawImage(sqTL, m_squareImage);

    // Draw hue indicator on ring
    qreal hueAngle = (m_hue / 360.0 - 0.5) * 2 * M_PI;
    qreal ringMid = (m_ringInnerRadius + m_ringOuterRadius) / 2.0;
    QPointF huePos(cx + ringMid * std::cos(hueAngle),
                   cy + ringMid * std::sin(hueAngle));
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(huePos, 5, 5);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(huePos, 6, 6);

    // Draw SV indicator on square
    qreal sqSz = svSquareSize();
    QPointF svPos(sqTL.x() + m_sat * sqSz, sqTL.y() + (1.0 - m_val) * sqSz);
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(svPos, 5, 5);
    p.setPen(QPen(Qt::black, 1));
    p.drawEllipse(svPos, 6, 6);
}

void ColorWheelWidget::handleMouse(QPoint pos) {
    qreal cx = width() / 2.0;
    qreal cy = height() / 2.0;
    qreal dx = pos.x() - cx;
    qreal dy = pos.y() - cy;
    qreal dist = std::sqrt(dx * dx + dy * dy);

    if (m_draggingRing || (!m_draggingSquare && dist >= m_ringInnerRadius && dist <= m_ringOuterRadius + 4)) {
        m_draggingRing = true;
        qreal angle = std::atan2(dy, dx);
        m_hue = static_cast<int>((angle / (2 * M_PI) + 0.5) * 360) % 360;
        if (m_hue < 0) m_hue += 360;
        update();
        emit colorChanged(color());
    } else {
        m_draggingSquare = true;
        QPointF sqTL = svSquareTopLeft();
        qreal sqSz = svSquareSize();
        m_sat = std::clamp((pos.x() - sqTL.x()) / sqSz, 0.0, 1.0);
        m_val = 1.0 - std::clamp((pos.y() - sqTL.y()) / sqSz, 0.0, 1.0);
        update();
        emit colorChanged(color());
    }
}

void ColorWheelWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_draggingRing = false;
        m_draggingSquare = false;
        handleMouse(event->pos());
    }
}

void ColorWheelWidget::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        handleMouse(event->pos());
    }
}

void ColorWheelWidget::resizeEvent(QResizeEvent*) {
    m_dirty = true;
}

// ==========================================================================
// ColorsDock
// ==========================================================================

ColorsDock::ColorsDock(QWidget* parent)
    : QDockWidget(tr("Colors"), parent) {
    setFeatures(QDockWidget::NoDockWidgetFeatures);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget;
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Primary/Secondary swatches + swap (above wheel)
    auto* swatchLayout = new QHBoxLayout;

    m_primarySwatch = new QLabel;
    m_primarySwatch->setFixedSize(32, 32);
    m_primarySwatch->setFrameStyle(QFrame::Box | QFrame::Sunken);
    m_primarySwatch->setCursor(Qt::PointingHandCursor);
    m_primarySwatch->setToolTip(tr("Primary Color"));
    m_primarySwatch->installEventFilter(this);

    m_secondarySwatch = new QLabel;
    m_secondarySwatch->setFixedSize(32, 32);
    m_secondarySwatch->setFrameStyle(QFrame::Box | QFrame::Sunken);
    m_secondarySwatch->setCursor(Qt::PointingHandCursor);
    m_secondarySwatch->setToolTip(tr("Secondary Color"));
    m_secondarySwatch->installEventFilter(this);

    swatchLayout->addWidget(m_primarySwatch);
    swatchLayout->addWidget(m_secondarySwatch);

    auto* swapBtn = new QPushButton(tr("Swap"));
    swapBtn->setFixedWidth(48);
    connect(swapBtn, &QPushButton::clicked, this, &ColorsDock::swapColors);
    swatchLayout->addWidget(swapBtn);
    swatchLayout->addStretch();
    mainLayout->addLayout(swatchLayout);

    // Color wheel
    m_wheel = new ColorWheelWidget;
    m_wheel->setColor(toQColor(m_primary));
    mainLayout->addWidget(m_wheel, 1);

    connect(m_wheel, &ColorWheelWidget::colorChanged, this, &ColorsDock::updateFromWheel);

    // Alpha slider (right below wheel) — created early, wired after helper defined
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        auto* lbl = new QLabel(tr("A:"));
        lbl->setFixedWidth(16);
        row->addWidget(lbl);
        m_alphaSlider = new QSlider(Qt::Horizontal);
        m_alphaSlider->setRange(0, 255);
        row->addWidget(m_alphaSlider, 1);
        m_alphaSpin = new QSpinBox;
        m_alphaSpin->setRange(0, 255);
        m_alphaSpin->setFixedWidth(50);
        row->addWidget(m_alphaSpin);
        mainLayout->addLayout(row);
        connect(m_alphaSlider, &QSlider::valueChanged, m_alphaSpin, &QSpinBox::setValue);
        connect(m_alphaSpin, QOverload<int>::of(&QSpinBox::valueChanged), m_alphaSlider, &QSlider::setValue);
        connect(m_alphaSlider, &QSlider::valueChanged, this, &ColorsDock::updateFromAlpha);
    }

    // Helper to create a labeled slider + spin row inside a given layout
    auto makeSliderRow = [this](QVBoxLayout* group, const QString& label, int min, int max,
                             QSlider*& slider, QSpinBox*& spin) {
        auto* row = new QHBoxLayout;
        row->setSpacing(2);
        auto* lbl = new QLabel(label);
        lbl->setFixedWidth(16);
        row->addWidget(lbl);
        slider = new QSlider(Qt::Horizontal);
        slider->setRange(min, max);
        row->addWidget(slider, 1);
        spin = new QSpinBox;
        spin->setRange(min, max);
        spin->setFixedWidth(50);
        row->addWidget(spin);
        group->addLayout(row);

        // Keep slider and spin in sync
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
    };

    // RGB sliders (tight spacing within group)
    auto* rgbGroup = new QVBoxLayout;
    rgbGroup->setSpacing(1);
    rgbGroup->setContentsMargins(0, 0, 0, 0);
    makeSliderRow(rgbGroup, tr("R:"), 0, 255, m_redSlider, m_redSpin);
    makeSliderRow(rgbGroup, tr("G:"), 0, 255, m_greenSlider, m_greenSpin);
    makeSliderRow(rgbGroup, tr("B:"), 0, 255, m_blueSlider, m_blueSpin);
    mainLayout->addLayout(rgbGroup);

    // HSV sliders (tight spacing within group)
    auto* hsvGroup = new QVBoxLayout;
    hsvGroup->setSpacing(1);
    hsvGroup->setContentsMargins(0, 0, 0, 0);
    makeSliderRow(hsvGroup, tr("H:"), 0, 359, m_hueSlider, m_hueSpin);
    makeSliderRow(hsvGroup, tr("S:"), 0, 255, m_satSlider, m_satSpin);
    makeSliderRow(hsvGroup, tr("V:"), 0, 255, m_valSlider, m_valSpin);
    mainLayout->addLayout(hsvGroup);

    // Wire HSV slider changes
    auto hsvChanged = [this](int) { updateFromHsv(); };
    connect(m_hueSlider, &QSlider::valueChanged, this, hsvChanged);
    connect(m_satSlider, &QSlider::valueChanged, this, hsvChanged);
    connect(m_valSlider, &QSlider::valueChanged, this, hsvChanged);

    // Wire RGB slider changes
    auto rgbChanged = [this](int) { updateFromRgb(); };
    connect(m_redSlider, &QSlider::valueChanged, this, rgbChanged);
    connect(m_greenSlider, &QSlider::valueChanged, this, rgbChanged);
    connect(m_blueSlider, &QSlider::valueChanged, this, rgbChanged);

    // Hex input row
    auto* hexLayout = new QHBoxLayout;
    hexLayout->addWidget(new QLabel(tr("#")));
    m_hexEdit = new QLineEdit;
    m_hexEdit->setMaxLength(8);
    auto* hexValidator = new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Fa-f]{0,8}"), m_hexEdit);
    m_hexEdit->setValidator(hexValidator);
    m_hexEdit->setFixedWidth(80);
    hexLayout->addWidget(m_hexEdit);
    hexLayout->addStretch();
    mainLayout->addLayout(hexLayout);

    connect(m_hexEdit, &QLineEdit::editingFinished, this, &ColorsDock::updateFromHex);

    setWidget(content);
    updateSwatches();
    syncControlsToColor(m_primary);
}

void ColorsDock::setPrimaryColor(ColorBgra color) {
    m_primary = color;
    updateSwatches();
    if (m_editingPrimary)
        syncControlsToColor(color);
    emit primaryColorChanged(color);
}

void ColorsDock::setSecondaryColor(ColorBgra color) {
    m_secondary = color;
    updateSwatches();
    if (!m_editingPrimary)
        syncControlsToColor(color);
    emit secondaryColorChanged(color);
}

void ColorsDock::syncControlsToColor(ColorBgra color) {
    m_updating = true;

    QColor qc = toQColor(color);
    m_wheel->setColor(qc);
    m_hexEdit->setText(qc.name(QColor::HexRgb).mid(1).toUpper());

    m_alphaSlider->setValue(color.a);

    // RGB
    m_redSlider->setValue(color.r);
    m_greenSlider->setValue(color.g);
    m_blueSlider->setValue(color.b);

    // HSV
    int h, s, v;
    qc.getHsv(&h, &s, &v);
    if (h < 0) h = 0;
    m_hueSlider->setValue(h);
    m_satSlider->setValue(s);
    m_valSlider->setValue(v);

    m_updating = false;
}

void ColorsDock::updateSwatches() {
    auto setSwatchColor = [](QLabel* label, ColorBgra c) {
        QColor qc(c.r, c.g, c.b, c.a);
        label->setStyleSheet(
            QStringLiteral("background-color: %1; border: 2px solid gray;").arg(qc.name()));
    };
    setSwatchColor(m_primarySwatch, m_primary);
    setSwatchColor(m_secondarySwatch, m_secondary);

    // Highlight active swatch
    m_primarySwatch->setFrameStyle(m_editingPrimary
        ? (QFrame::Box | QFrame::Plain) : (QFrame::Box | QFrame::Sunken));
    m_secondarySwatch->setFrameStyle(!m_editingPrimary
        ? (QFrame::Box | QFrame::Plain) : (QFrame::Box | QFrame::Sunken));
}

void ColorsDock::updateFromWheel(const QColor& color) {
    if (m_updating) return;
    m_updating = true;

    QColor c = color;
    c.setAlpha(m_alphaSlider->value());
    ColorBgra bgra = fromQColor(c);

    m_hexEdit->setText(c.name(QColor::HexRgb).mid(1).toUpper());

    // Sync RGB sliders
    m_redSlider->setValue(c.red());
    m_greenSlider->setValue(c.green());
    m_blueSlider->setValue(c.blue());

    // Sync HSV sliders
    int h, s, v;
    c.getHsv(&h, &s, &v);
    if (h < 0) h = 0;
    m_hueSlider->setValue(h);
    m_satSlider->setValue(s);
    m_valSlider->setValue(v);

    if (m_editingPrimary) {
        m_primary = bgra;
        updateSwatches();
        emit primaryColorChanged(m_primary);
    } else {
        m_secondary = bgra;
        updateSwatches();
        emit secondaryColorChanged(m_secondary);
    }
    m_updating = false;
}

void ColorsDock::updateFromHex() {
    if (m_updating) return;

    QString hex = m_hexEdit->text();
    if (hex.length() < 6) return;

    QColor c("#" + hex.left(6));
    if (!c.isValid()) return;
    c.setAlpha(m_alphaSlider->value());

    ColorBgra bgra = fromQColor(c);
    syncControlsToColor(bgra);

    if (m_editingPrimary) {
        m_primary = bgra;
        updateSwatches();
        emit primaryColorChanged(m_primary);
    } else {
        m_secondary = bgra;
        updateSwatches();
        emit secondaryColorChanged(m_secondary);
    }
}

void ColorsDock::updateFromAlpha(int alpha) {
    if (m_updating) return;
    if (m_editingPrimary) {
        m_primary.a = static_cast<uint8_t>(alpha);
        emit primaryColorChanged(m_primary);
    } else {
        m_secondary.a = static_cast<uint8_t>(alpha);
        emit secondaryColorChanged(m_secondary);
    }
}

void ColorsDock::updateFromRgb() {
    if (m_updating) return;
    m_updating = true;

    int r = m_redSlider->value();
    int g = m_greenSlider->value();
    int b = m_blueSlider->value();
    int a = m_alphaSlider->value();

    QColor c(r, g, b, a);
    ColorBgra bgra = fromQColor(c);

    m_wheel->setColor(c);
    m_hexEdit->setText(c.name(QColor::HexRgb).mid(1).toUpper());

    // Sync HSV sliders
    int h, s, v;
    c.getHsv(&h, &s, &v);
    if (h < 0) h = 0;
    m_hueSlider->setValue(h);
    m_satSlider->setValue(s);
    m_valSlider->setValue(v);

    if (m_editingPrimary) {
        m_primary = bgra;
        updateSwatches();
        emit primaryColorChanged(m_primary);
    } else {
        m_secondary = bgra;
        updateSwatches();
        emit secondaryColorChanged(m_secondary);
    }
    m_updating = false;
}

void ColorsDock::updateFromHsv() {
    if (m_updating) return;
    m_updating = true;

    int h = m_hueSlider->value();
    int s = m_satSlider->value();
    int v = m_valSlider->value();
    int a = m_alphaSlider->value();

    QColor c = QColor::fromHsv(h, s, v, a);
    ColorBgra bgra = fromQColor(c);

    m_wheel->setColor(c);
    m_hexEdit->setText(c.name(QColor::HexRgb).mid(1).toUpper());

    // Sync RGB sliders
    m_redSlider->setValue(c.red());
    m_greenSlider->setValue(c.green());
    m_blueSlider->setValue(c.blue());

    if (m_editingPrimary) {
        m_primary = bgra;
        updateSwatches();
        emit primaryColorChanged(m_primary);
    } else {
        m_secondary = bgra;
        updateSwatches();
        emit secondaryColorChanged(m_secondary);
    }
    m_updating = false;
}

void ColorsDock::swapColors() {
    std::swap(m_primary, m_secondary);
    updateSwatches();

    ColorBgra active = m_editingPrimary ? m_primary : m_secondary;
    syncControlsToColor(active);

    emit primaryColorChanged(m_primary);
    emit secondaryColorChanged(m_secondary);
}

bool ColorsDock::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj == m_primarySwatch) {
            m_editingPrimary = true;
            syncControlsToColor(m_primary);
            updateSwatches();
            return true;
        }
        if (obj == m_secondarySwatch) {
            m_editingPrimary = false;
            syncControlsToColor(m_secondary);
            updateSwatches();
            return true;
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

} // namespace paintnux
