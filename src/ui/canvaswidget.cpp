#include "ui/canvaswidget.h"

#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace paintnux {

CanvasWidget::CanvasWidget(QWidget* parent)
    : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(100, 100);

    // Marching ants timer (also drives text cursor blink)
    m_antTimer.setInterval(100);
    connect(&m_antTimer, &QTimer::timeout, this, [this]() {
        m_antOffset += 1.0;
        if (m_antOffset >= 12.0) m_antOffset = 0.0;

        // Text cursor blink: toggle every 500ms (5 ticks at 100ms)
        if (m_textCursorVisible) {
            m_textCursorBlinkCounter++;
            if (m_textCursorBlinkCounter >= 5) {
                m_textCursorBlinkCounter = 0;
                m_textCursorBlinkOn = !m_textCursorBlinkOn;
            }
        }

        if (!m_selectionPath.isEmpty() || m_textCursorVisible) {
            update();
        }
    });
    m_antTimer.start();
}

void CanvasWidget::setRenderSource(const Surface* surface) {
    m_source = surface;
    m_channelFilteredImage = QImage(); // invalidate channel view cache
    update();
}

// --- Zoom ---

void CanvasWidget::setZoomFactor(double zoom) {
    zoom = std::clamp(zoom, MinZoom, MaxZoom);
    if (std::abs(zoom - m_zoom) < 1e-9) return;

    // Preserve the document center point across zoom changes
    QPointF center = widgetToDocument(QPointF(width() / 2.0, height() / 2.0));

    m_zoom = zoom;

    // Re-center on the same document point
    centerOn(center);

    updateScrollBars();
    emit zoomChanged(m_zoom);
    update();
}

static const double kZoomLevels[] = {
    // Under 0.20: steps of 0.02
    0.02, 0.04, 0.06, 0.08, 0.10, 0.12, 0.14, 0.16, 0.18,
    // 0.20 to 0.60: steps of 0.05
    0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.55, 0.60,
    // 0.60 to 1.0: steps of 0.10
    0.70, 0.80, 0.90,
    // 1.0 and above
    1.0, 1.12, 1.25, 1.50, 1.75,
    2.0, 2.25, 2.50, 3.0, 3.5, 4.0, 4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0,
    12.0, 16.0, 24.0, 32.0
};
static const int kNumZoomLevels = sizeof(kZoomLevels) / sizeof(kZoomLevels[0]);

void CanvasWidget::zoomIn() {
    for (int i = 0; i < kNumZoomLevels; ++i) {
        if (kZoomLevels[i] > m_zoom + 1e-9) {
            setZoomFactor(kZoomLevels[i]);
            return;
        }
    }
}

void CanvasWidget::zoomOut() {
    for (int i = kNumZoomLevels - 1; i >= 0; --i) {
        if (kZoomLevels[i] < m_zoom - 1e-9) {
            setZoomFactor(kZoomLevels[i]);
            return;
        }
    }
}

// Coarse zoom levels for the zoom tool (bigger jumps)
static const double kCoarseZoomLevels[] = {
    0.01, 0.02, 0.03, 0.05, 0.08, 0.12, 0.16, 0.25, 0.33, 0.50, 0.66,
    1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
    12.0, 16.0, 24.0, 32.0
};
static const int kNumCoarseZoomLevels = sizeof(kCoarseZoomLevels) / sizeof(kCoarseZoomLevels[0]);

void CanvasWidget::zoomInCoarse() {
    for (int i = 0; i < kNumCoarseZoomLevels; ++i) {
        if (kCoarseZoomLevels[i] > m_zoom + 1e-9) {
            setZoomFactor(kCoarseZoomLevels[i]);
            return;
        }
    }
}

void CanvasWidget::zoomOutCoarse() {
    for (int i = kNumCoarseZoomLevels - 1; i >= 0; --i) {
        if (kCoarseZoomLevels[i] < m_zoom - 1e-9) {
            setZoomFactor(kCoarseZoomLevels[i]);
            return;
        }
    }
}

void CanvasWidget::zoomToFit() {
    if (!m_source) return;
    double zx = static_cast<double>(width() - 10) / m_source->width();
    double zy = static_cast<double>(height() - 10) / m_source->height();
    double z = std::min(zx, zy);
    z = std::clamp(z, MinZoom, 1.0);
    m_zoom = z;
    centerOn(QPointF(m_source->width() / 2.0, m_source->height() / 2.0));
    updateScrollBars();
    emit zoomChanged(m_zoom);
    update();
}

void CanvasWidget::zoomToActualSize() {
    setZoomFactor(1.0);
}

void CanvasWidget::zoomToSelection(const QPainterPath& path) {
    if (path.isEmpty()) return;

    QRectF bounds = path.boundingRect();
    if (bounds.isEmpty()) return;

    // Add 10% padding
    double pad = std::max(bounds.width(), bounds.height()) * 0.1;
    bounds.adjust(-pad, -pad, pad, pad);

    double zx = static_cast<double>(width()) / bounds.width();
    double zy = static_cast<double>(height()) / bounds.height();
    double z = std::min(zx, zy);
    z = std::clamp(z, MinZoom, MaxZoom);

    m_zoom = z;
    centerOn(bounds.center());
    updateScrollBars();
    emit zoomChanged(m_zoom);
    update();
}

// --- Pixel grid ---

void CanvasWidget::setPixelGrid(bool show) {
    if (m_showPixelGrid == show) return;
    m_showPixelGrid = show;
    update();
}

void CanvasWidget::setChannelView(ChannelView view) {
    if (m_channelView == view) return;
    m_channelView = view;
    m_channelFilteredImage = QImage(); // invalidate cache
    update();
    emit channelViewChanged(view);
}

void CanvasWidget::drawPixelGrid(QPainter& painter, const QRectF& docRect) {
    if (!m_showPixelGrid || m_zoom < 4.0) return;

    painter.save();

    QPen gridPen(QColor(160, 160, 160, 128), 1.0);
    gridPen.setCosmetic(true);
    painter.setPen(gridPen);

    // Visible document area in integer pixel coords
    QRectF visDoc = visibleDocumentRect();
    int x0 = std::max(0, static_cast<int>(std::floor(visDoc.left())));
    int y0 = std::max(0, static_cast<int>(std::floor(visDoc.top())));
    int x1 = std::min(static_cast<int>(std::ceil(visDoc.right())), m_source ? m_source->width() : 0);
    int y1 = std::min(static_cast<int>(std::ceil(visDoc.bottom())), m_source ? m_source->height() : 0);

    // Vertical lines
    for (int x = x0; x <= x1; ++x) {
        QPointF top = documentToWidget(QPointF(x, y0));
        QPointF bot = documentToWidget(QPointF(x, y1));
        painter.drawLine(top, bot);
    }

    // Horizontal lines
    for (int y = y0; y <= y1; ++y) {
        QPointF left = documentToWidget(QPointF(x0, y));
        QPointF right = documentToWidget(QPointF(x1, y));
        painter.drawLine(left, right);
    }

    painter.restore();
}

// --- Pan ---

void CanvasWidget::setScrollPosition(QPointF pos) {
    m_scrollPos = pos;
    clampScrollPosition();
    updateScrollBars();
    emit scrollPositionChanged(m_scrollPos);
    update();
}

void CanvasWidget::centerOn(QPointF docPoint) {
    m_scrollPos.setX(docPoint.x() - (width() / 2.0) / m_zoom);
    m_scrollPos.setY(docPoint.y() - (height() / 2.0) / m_zoom);
    clampScrollPosition();
    updateScrollBars();
    emit scrollPositionChanged(m_scrollPos);
}

void CanvasWidget::setSpacebarHeld(bool held) {
    m_spaceHeld = held;
    if (held) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
        m_panning = false;
    }
}

// --- Coordinate transforms ---

QPointF CanvasWidget::widgetToDocument(QPointF widgetPt) const {
    return QPointF(
        widgetPt.x() / m_zoom + m_scrollPos.x(),
        widgetPt.y() / m_zoom + m_scrollPos.y());
}

QPointF CanvasWidget::documentToWidget(QPointF docPt) const {
    return QPointF(
        (docPt.x() - m_scrollPos.x()) * m_zoom,
        (docPt.y() - m_scrollPos.y()) * m_zoom);
}

QRectF CanvasWidget::visibleDocumentRect() const {
    QPointF topLeft = widgetToDocument(QPointF(0, 0));
    QPointF bottomRight = widgetToDocument(QPointF(width(), height()));
    return QRectF(topLeft, bottomRight);
}

// --- Selection display ---

void CanvasWidget::setSelectionPath(const QPainterPath& path) {
    m_selectionPath = path;
    update();
}

// --- Overlay ---

void CanvasWidget::setOverlay(const Surface* surface, QPoint offset) {
    m_overlaySurface = surface;
    m_overlayOffset = offset;
    m_overlayDisplaySize = QSizeF(0, 0); // reset to native size
    update();
}

void CanvasWidget::setOverlayDisplaySize(QSizeF size) {
    m_overlayDisplaySize = size;
    update();
}

// --- Tool nubs ---

void CanvasWidget::setToolNubs(const std::vector<QPointF>& nubs) {
    m_toolNubs = nubs;
    update();
}

// --- Brush circle guides ---

void CanvasWidget::setBrushCircles(const std::vector<QPointF>& docPositions, int diameter) {
    m_brushCirclePositions = docPositions;
    m_brushCircleDiameter = diameter;
    update();
}

void CanvasWidget::clearBrushCircles() {
    m_brushCirclePositions.clear();
    update();
}

// --- Text cursor ---

void CanvasWidget::setTextCursor(QPointF docPos, qreal height) {
    m_textCursorVisible = true;
    m_textCursorDocPos = docPos;
    m_textCursorHeight = height;
    m_textCursorBlinkCounter = 0;
    m_textCursorBlinkOn = true;
    update();
}

void CanvasWidget::clearTextCursor() {
    m_textCursorVisible = false;
    update();
}

void CanvasWidget::drawTextCursor(QPainter& painter) {
    if (!m_textCursorVisible || !m_textCursorBlinkOn) return;

    painter.save();

    QPointF topWidget = documentToWidget(m_textCursorDocPos);
    QPointF bottomWidget = documentToWidget(
        QPointF(m_textCursorDocPos.x(), m_textCursorDocPos.y() + m_textCursorHeight));

    QPen cursorPen(Qt::black, 1.0);
    cursorPen.setCosmetic(true);
    painter.setPen(cursorPen);
    painter.drawLine(topWidget, bottomWidget);

    painter.restore();
}

// --- Scrollbars ---

void CanvasWidget::setScrollBars(QScrollBar* hbar, QScrollBar* vbar) {
    m_hbar = hbar;
    m_vbar = vbar;

    if (m_hbar) {
        connect(m_hbar, &QScrollBar::valueChanged, this, [this](int val) {
            if (m_updatingScrollBars) return;
            m_scrollPos.setX(val / m_zoom);
            emit scrollPositionChanged(m_scrollPos);
            update();
        });
    }
    if (m_vbar) {
        connect(m_vbar, &QScrollBar::valueChanged, this, [this](int val) {
            if (m_updatingScrollBars) return;
            m_scrollPos.setY(val / m_zoom);
            emit scrollPositionChanged(m_scrollPos);
            update();
        });
    }
    updateScrollBars();
}

void CanvasWidget::updateScrollBars() {
    if (!m_source) return;
    m_updatingScrollBars = true;

    // Scroll range in pixels: allow document to scroll half a viewport past each edge
    int halfW = width() / 2;
    int halfH = height() / 2;
    int docW = static_cast<int>(m_source->width() * m_zoom);
    int docH = static_cast<int>(m_source->height() * m_zoom);

    if (m_hbar) {
        m_hbar->setRange(-halfW, docW - halfW);
        m_hbar->setPageStep(width());
        m_hbar->setSingleStep(std::max(1, width() / 20));
        m_hbar->setValue(static_cast<int>(m_scrollPos.x() * m_zoom));
    }
    if (m_vbar) {
        m_vbar->setRange(-halfH, docH - halfH);
        m_vbar->setPageStep(height());
        m_vbar->setSingleStep(std::max(1, height() / 20));
        m_vbar->setValue(static_cast<int>(m_scrollPos.y() * m_zoom));
    }

    m_updatingScrollBars = false;
}

void CanvasWidget::clampScrollPosition() {
    if (!m_source) return;

    double viewW = width() / m_zoom;
    double viewH = height() / m_zoom;
    double docW = m_source->width();
    double docH = m_source->height();

    // When the document fits entirely in the viewport, center it
    if (docW * m_zoom <= width()) {
        m_scrollPos.setX(docW / 2.0 - viewW / 2.0);
    } else {
        double minX = -viewW / 2.0;
        double maxX = docW - viewW / 2.0;
        m_scrollPos.setX(std::clamp(m_scrollPos.x(), minX, maxX));
    }

    if (docH * m_zoom <= height()) {
        m_scrollPos.setY(docH / 2.0 - viewH / 2.0);
    } else {
        double minY = -viewH / 2.0;
        double maxY = docH - viewH / 2.0;
        m_scrollPos.setY(std::clamp(m_scrollPos.y(), minY, maxY));
    }
}

QSizeF CanvasWidget::scaledDocSize() const {
    if (!m_source) return QSizeF(0, 0);
    return QSizeF(m_source->width() * m_zoom, m_source->height() * m_zoom);
}

// --- Checkerboard ---

void CanvasWidget::ensureCheckerTile() {
    if (!m_checkerTile.isNull()) return;
    int sz = CheckerSize * 2;
    m_checkerTile = QPixmap(sz, sz);
    QPainter p(&m_checkerTile);
    QColor light(255, 255, 255);
    QColor dark(204, 204, 204);
    p.fillRect(0, 0, CheckerSize, CheckerSize, light);
    p.fillRect(CheckerSize, 0, CheckerSize, CheckerSize, dark);
    p.fillRect(0, CheckerSize, CheckerSize, CheckerSize, dark);
    p.fillRect(CheckerSize, CheckerSize, CheckerSize, CheckerSize, light);
}

void CanvasWidget::drawCheckerboard(QPainter& painter, const QRect& area) {
    ensureCheckerTile();
    // Fixed checkerboard — anchored to viewport, not document
    painter.drawTiledPixmap(area, m_checkerTile);
}

// --- Marching ants ---

void CanvasWidget::drawMarchingAnts(QPainter& painter) {
    if (m_selectionPath.isEmpty()) return;

    painter.save();

    // Transform from document coordinates to widget coordinates
    QTransform xform;
    xform.scale(m_zoom, m_zoom);
    xform.translate(-m_scrollPos.x(), -m_scrollPos.y());

    QPainterPath widgetPath = xform.map(m_selectionPath);

    // Black solid outline
    QPen blackPen(Qt::black, 1.0);
    blackPen.setCosmetic(true);
    painter.setPen(blackPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(widgetPath);

    // White dashed overlay with marching animation
    QPen whitePen(Qt::white, 1.0, Qt::DashLine);
    whitePen.setCosmetic(true);
    whitePen.setDashPattern({4.0, 4.0});
    whitePen.setDashOffset(m_antOffset);
    painter.setPen(whitePen);
    painter.drawPath(widgetPath);

    painter.restore();
}

// --- Selection handles ---

void CanvasWidget::drawSelectionHandles(QPainter& painter) {
    if (m_selectionPath.isEmpty()) return;

    QRectF bounds = m_selectionPath.boundingRect();
    if (bounds.isEmpty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);

    // 8 handle positions: TL, T, TR, R, BR, B, BL, L
    qreal cx = bounds.center().x();
    qreal cy = bounds.center().y();
    QPointF handlesDoc[] = {
        bounds.topLeft(),
        QPointF(cx, bounds.top()),
        bounds.topRight(),
        QPointF(bounds.right(), cy),
        bounds.bottomRight(),
        QPointF(cx, bounds.bottom()),
        bounds.bottomLeft(),
        QPointF(bounds.left(), cy)
    };

    constexpr int handleSize = 6; // pixels on screen
    int half = handleSize / 2;

    QPen outlinePen(Qt::black, 1.0);
    outlinePen.setCosmetic(true);

    for (const auto& docPt : handlesDoc) {
        QPointF widgetPt = documentToWidget(docPt);
        QRectF handleRect(widgetPt.x() - half, widgetPt.y() - half, handleSize, handleSize);

        painter.setPen(outlinePen);
        painter.setBrush(Qt::white);
        painter.drawRect(handleRect);
    }

    painter.restore();
}

void CanvasWidget::drawToolNubs(QPainter& painter) {
    if (m_toolNubs.empty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    constexpr int nubRadius = 5;  // pixels on screen

    QPen outlinePen(Qt::black, 1.0);
    outlinePen.setCosmetic(true);

    for (const auto& docPt : m_toolNubs) {
        QPointF widgetPt = documentToWidget(docPt);
        QRectF nubRect(widgetPt.x() - nubRadius, widgetPt.y() - nubRadius,
                       nubRadius * 2, nubRadius * 2);

        painter.setPen(outlinePen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(nubRect);
    }

    painter.restore();
}

void CanvasWidget::drawBrushCircle(QPainter& painter) {
    if (m_brushCirclePositions.empty()) return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);

    double widgetRadius = m_brushCircleDiameter * m_zoom / 2.0;

    QPen outerPen(Qt::black, 1.5);
    outerPen.setCosmetic(true);
    QPen innerPen(Qt::white, 0.75);
    innerPen.setCosmetic(true);

    for (const auto& docPos : m_brushCirclePositions) {
        QPointF widgetCenter = documentToWidget(docPos);

        painter.setPen(outerPen);
        painter.drawEllipse(widgetCenter, widgetRadius, widgetRadius);

        painter.setPen(innerPen);
        painter.drawEllipse(widgetCenter, widgetRadius, widgetRadius);
    }

    painter.restore();
}

// --- Overlay ---

void CanvasWidget::drawOverlay(QPainter& painter) {
    if (!m_overlaySurface) return;

    QPointF overlayOrigin = documentToWidget(QPointF(m_overlayOffset.x(), m_overlayOffset.y()));

    // Use display size override if set, otherwise native surface size
    QSizeF docSize = (m_overlayDisplaySize.width() > 0 && m_overlayDisplaySize.height() > 0)
        ? m_overlayDisplaySize
        : QSizeF(m_overlaySurface->width(), m_overlaySurface->height());
    QSizeF overlaySize(docSize.width() * m_zoom, docSize.height() * m_zoom);
    QRectF overlayRect(overlayOrigin, overlaySize);
    QRectF srcRect(QPointF(0, 0), QSizeF(m_overlaySurface->width(), m_overlaySurface->height()));

    bool needsSmooth = (m_overlayDisplaySize.width() > 0) || (m_zoom < 1.0);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, needsSmooth);

    // Clip overlay to canvas bounds
    if (m_source) {
        QRectF canvasWidget(documentToWidget(QPointF(0, 0)),
                            QSizeF(m_source->width() * m_zoom, m_source->height() * m_zoom));
        painter.save();
        painter.setClipRect(canvasWidget);
        painter.drawImage(overlayRect, m_overlaySurface->qimage(), srcRect);
        painter.restore();
        return;
    }

    painter.drawImage(overlayRect, m_overlaySurface->qimage(), srcRect);
}

// --- Events ---

void CanvasWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setClipRegion(event->region());

    // Fill background with dark gray (outside document area)
    painter.fillRect(rect(), QColor(128, 128, 128));

    if (!m_source) return;

    // Calculate where the document lands in widget coordinates
    QPointF docOrigin = documentToWidget(QPointF(0, 0));
    QSizeF docSize = scaledDocSize();
    QRectF docRect(docOrigin, docSize);

    // Clip to the visible intersection
    QRectF visibleDoc = docRect.intersected(QRectF(rect()));
    if (visibleDoc.isEmpty()) return;

    QRect pixelRect = visibleDoc.toAlignedRect();

    // Clip checkerboard to exact document rect to avoid 1px border artifacts
    painter.save();
    painter.setClipRect(docRect);
    drawCheckerboard(painter, pixelRect);
    painter.restore();

    // Draw the document surface, scaled (with optional channel view filter)
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_zoom < 1.0);
    const QImage& srcImg = m_source->qimage();
    QRectF srcRect(QPointF(0, 0), QSizeF(m_source->width(), m_source->height()));

    if (m_channelView == ChannelView::All) {
        painter.drawImage(docRect, srcImg, srcRect);
    } else {
        // Rebuild filtered image if source changed or cache is invalid
        if (m_channelFilteredImage.size() != srcImg.size()) {
            m_channelFilteredImage = QImage(srcImg.size(), QImage::Format_RGB32);
        }
        // Channel byte offsets in ARGB32 (native QRgb): A=24, R=16, G=8, B=0
        int shift = 0;
        switch (m_channelView) {
        case ChannelView::Red:   shift = 16; break;
        case ChannelView::Green: shift = 8;  break;
        case ChannelView::Blue:  shift = 0;  break;
        case ChannelView::Alpha: shift = 24; break;
        default: break;
        }
        for (int y = 0; y < srcImg.height(); ++y) {
            const QRgb* src = reinterpret_cast<const QRgb*>(srcImg.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(m_channelFilteredImage.scanLine(y));
            for (int x = 0; x < srcImg.width(); ++x) {
                uint8_t v = static_cast<uint8_t>((src[x] >> shift) & 0xFF);
                dst[x] = qRgb(v, v, v);
            }
        }
        painter.drawImage(docRect, m_channelFilteredImage, srcRect);
    }

    // Draw pixel grid (between pixels when zoomed in enough)
    drawPixelGrid(painter, docRect);

    // Draw floating overlay (moved pixels)
    drawOverlay(painter);

    // Draw marching ants selection outline
    drawMarchingAnts(painter);

    // Draw selection resize handles
    drawSelectionHandles(painter);

    // Draw tool control point nubs
    drawToolNubs(painter);

    // Draw brush circle guide
    drawBrushCircle(painter);

    // Draw text cursor overlay
    drawTextCursor(painter);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    // Zoom centered on mouse position
    QPointF docPt = widgetToDocument(event->position());
    int delta = event->angleDelta().y();

    if (delta > 0) {
        zoomIn();
    } else if (delta < 0) {
        zoomOut();
    }

    // After zoom, adjust scroll so docPt stays under the mouse
    QPointF newDocPt = widgetToDocument(event->position());
    m_scrollPos += docPt - newDocPt;
    clampScrollPosition();
    updateScrollBars();
    emit scrollPositionChanged(m_scrollPos);
    update();

    event->accept();
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (m_spaceHeld && event->button() == Qt::LeftButton) {
        m_panning = true;
        m_panStartWidget = event->pos();
        m_panStartScroll = m_scrollPos;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStartWidget = event->pos();
        m_panStartScroll = m_scrollPos;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    // Forward to tool
    QPointF docPos = widgetToDocument(event->position());
    emit toolMouseDown(docPos, event->button(), event->modifiers());
    event->accept();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    // Emit cursor position in document coords
    QPointF docPos = widgetToDocument(event->position());
    emit cursorDocumentPosition(docPos);

    if (m_panning) {
        QPoint delta = event->pos() - m_panStartWidget;
        m_scrollPos = m_panStartScroll - QPointF(delta.x() / m_zoom, delta.y() / m_zoom);
        clampScrollPosition();
        updateScrollBars();
        emit scrollPositionChanged(m_scrollPos);
        update();
        event->accept();
        return;
    }

    emit toolMouseMove(docPos, event->buttons(), event->modifiers());
    event->accept();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_panning && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_panning = false;
        setCursor(m_spaceHeld ? Qt::OpenHandCursor : Qt::ArrowCursor);
        event->accept();
        return;
    }

    QPointF docPos = widgetToDocument(event->position());
    emit toolMouseUp(docPos, event->button(), event->modifiers());
    event->accept();
}

void CanvasWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!m_source) return;
    double zx = static_cast<double>(width() - 10) / m_source->width();
    double zy = static_cast<double>(height() - 10) / m_source->height();
    double z = std::min({zx, zy, 1.0});
    z = std::max(z, MinZoom);
    m_zoom = z;
    centerOn(QPointF(m_source->width() / 2.0, m_source->height() / 2.0));
    updateScrollBars();
    emit zoomChanged(m_zoom);
    update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && !m_textCursorVisible) {
        setSpacebarHeld(true);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
            zoomIn();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Minus) {
            zoomOut();
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_0) {
            zoomToActualSize();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_F && event->modifiers() == Qt::ControlModifier) {
        zoomToFit();
        event->accept();
        return;
    }
    emit toolKeyDown(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && !m_textCursorVisible) {
        setSpacebarHeld(false);
        event->accept();
        return;
    }
    emit toolKeyUp(event);
}

} // namespace paintnux
