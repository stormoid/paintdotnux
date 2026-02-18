#pragma once

#include "core/surface.h"

#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <QScrollBar>
#include <QPainterPath>
#include <QTimer>

#include <functional>
#include <vector>

namespace paintnux {

enum class ChannelView { All = 0, Red, Green, Blue, Alpha };

/// Custom canvas widget that renders a composited document surface
/// with zoom, pan, checkerboard transparency background, marching ants, and overlay.
class CanvasWidget : public QWidget {
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget* parent = nullptr);

    /// Set the surface to render (typically the composited document).
    void setRenderSource(const Surface* surface);

    // --- Zoom ---

    [[nodiscard]] double zoomFactor() const { return m_zoom; }

    /// Set zoom level (clamped to [MinZoom, MaxZoom]).
    void setZoomFactor(double zoom);

    /// Zoom in/out by a fine step (mouse wheel / keyboard).
    void zoomIn();
    void zoomOut();

    /// Zoom in/out by a coarse step (zoom tool clicks).
    void zoomInCoarse();
    void zoomOutCoarse();

    /// Zoom to fit the document in the viewport.
    void zoomToFit();

    /// Zoom to 100%.
    void zoomToActualSize();

    /// Zoom to fit the selection bounding rect in the viewport.
    void zoomToSelection(const QPainterPath& path);

    // --- Pixel grid ---

    [[nodiscard]] bool pixelGrid() const { return m_showPixelGrid; }
    void setPixelGrid(bool show);

    // --- Channel view ---

    [[nodiscard]] ChannelView channelView() const { return m_channelView; }
    void setChannelView(ChannelView view);

    static constexpr double MinZoom = 0.01;   // 1%
    static constexpr double MaxZoom = 32.0;    // 3200%

    // --- Pan ---

    /// Get/set the scroll position (document coordinates of the top-left visible corner).
    [[nodiscard]] QPointF scrollPosition() const { return m_scrollPos; }
    void setScrollPosition(QPointF pos);

    /// Center the view on the given document point.
    void centerOn(QPointF docPoint);

    // --- Coordinate transforms ---

    /// Convert widget coordinates to document (image pixel) coordinates.
    [[nodiscard]] QPointF widgetToDocument(QPointF widgetPt) const;

    /// Convert document coordinates to widget coordinates.
    [[nodiscard]] QPointF documentToWidget(QPointF docPt) const;

    /// Get the visible document rectangle (in document coordinates).
    [[nodiscard]] QRectF visibleDocumentRect() const;

    // --- Spacebar pan mode ---

    void setSpacebarHeld(bool held);
    [[nodiscard]] bool isSpacebarHeld() const { return m_spaceHeld; }

    // --- External scrollbars ---

    void setScrollBars(QScrollBar* hbar, QScrollBar* vbar);

    // --- Selection display ---

    /// Set the selection path for marching ants rendering (in document coordinates).
    void setSelectionPath(const QPainterPath& path);

    // --- Overlay ---

    /// Set a floating overlay surface to render on top of the document.
    void setOverlay(const Surface* surface, QPoint offset);

    /// Set an override display size for the overlay (for resize preview).
    /// Pass QSizeF(0,0) to use the surface's native size.
    void setOverlayDisplaySize(QSizeF size);

    // --- Brush circle guides ---

    /// Show brush circle guides at the given document positions with the given diameter.
    /// Pass empty vector to clear.
    void setBrushCircles(const std::vector<QPointF>& docPositions, int diameter);

    /// Clear all brush circle guides.
    void clearBrushCircles();

    // --- Tool nubs (control points) ---

    /// Set control point positions to draw as draggable nubs (document coords).
    /// Pass empty vector to clear.
    void setToolNubs(const std::vector<QPointF>& nubs);

    // --- Text cursor overlay ---

    /// Set the text cursor position and height (document coords) for blinking cursor display.
    void setTextCursor(QPointF docPos, qreal height);

    /// Clear the text cursor overlay.
    void clearTextCursor();

signals:
    void zoomChanged(double zoom);
    void scrollPositionChanged(QPointF pos);
    void cursorDocumentPosition(QPointF docPos);
    void channelViewChanged(ChannelView view);

    // Tool event signals (document coordinates)
    void toolMouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void toolMouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
    void toolMouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void toolKeyDown(QKeyEvent* event);
    void toolKeyUp(QKeyEvent* event);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void clampScrollPosition();
    void updateScrollBars();
    void drawCheckerboard(QPainter& painter, const QRect& widgetRect);
    void drawPixelGrid(QPainter& painter, const QRectF& docRect);
    void drawMarchingAnts(QPainter& painter);
    void drawSelectionHandles(QPainter& painter);
    void drawOverlay(QPainter& painter);
    void drawToolNubs(QPainter& painter);
    void drawBrushCircle(QPainter& painter);
    void drawTextCursor(QPainter& painter);
    QSizeF scaledDocSize() const;

    const Surface* m_source = nullptr;
    double m_zoom = 1.0;
    QPointF m_scrollPos{0, 0};

    // Pan state
    bool m_spaceHeld = false;
    bool m_panning = false;
    QPoint m_panStartWidget;
    QPointF m_panStartScroll;

    // External scrollbars
    QScrollBar* m_hbar = nullptr;
    QScrollBar* m_vbar = nullptr;
    bool m_updatingScrollBars = false;

    // Pixel grid
    bool m_showPixelGrid = false;

    // Channel view
    ChannelView m_channelView = ChannelView::All;
    QImage m_channelFilteredImage;

    // Checkerboard tile
    static constexpr int CheckerSize = 8;
    QPixmap m_checkerTile;
    void ensureCheckerTile();

    // Marching ants
    QPainterPath m_selectionPath;
    QTimer m_antTimer;
    qreal m_antOffset = 0.0;

    // Floating overlay
    const Surface* m_overlaySurface = nullptr;
    QPoint m_overlayOffset{0, 0};
    QSizeF m_overlayDisplaySize{0, 0};  // (0,0) = use native surface size

    // Brush circle guides
    std::vector<QPointF> m_brushCirclePositions;
    int m_brushCircleDiameter = 0;

    // Tool nubs (control point handles)
    std::vector<QPointF> m_toolNubs;

    // Text cursor overlay
    bool m_textCursorVisible = false;
    QPointF m_textCursorDocPos;
    qreal m_textCursorHeight = 0.0;
    int m_textCursorBlinkCounter = 0;
    bool m_textCursorBlinkOn = true;
};

} // namespace paintnux
