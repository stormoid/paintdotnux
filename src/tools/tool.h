#pragma once

#include "core/colorbgra.h"
#include "core/surface.h"
#include "data/selection.h"

#include <QObject>
#include <QPointF>
#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QString>

namespace paintnux {

class DocumentWorkspace;
class Surface;
class BitmapLayer;
class HistoryStack;
class Document;

/// Shape draw mode for shape tools.
enum class ShapeDrawType { Outline, Fill, Both };

/// Gradient type for the gradient tool (matches Paint.NET).
enum class GradientType { LinearClamped, LinearReflected, LinearDiamond, Radial, Conical };

/// Text alignment for the text tool.
enum class TextAlign { Left, Center, Right };

/// Selection draw constraint for rect/ellipse select tools.
enum class SelectionDrawMode { Normal, FixedRatio, FixedSize };

/// Flood fill mode for bucket and magic wand.
enum class FloodMode { Contiguous, Global };

/// Alpha blending mode for drawing tools (Normal = SourceOver, Overwrite = SourceCopy).
enum class ToolBlendMode { Normal, Overwrite };

/// What happens after sampling with the color picker.
enum class ColorPickerBehavior { DoNotSwitch, SwitchToPrevious, SwitchToPencil };

/// Tool configuration passed to tools for rendering state.
struct ToolSettings {
    int brushSize = 5;
    int tolerance = 50;      // 0-100 for flood fill / magic wand
    bool antialiased = true;
    ShapeDrawType shapeDrawType = ShapeDrawType::Both;
    GradientType gradientType = GradientType::LinearClamped;
    FloodMode floodMode = FloodMode::Contiguous;
    SelectionCombineMode selectionCombineMode = SelectionCombineMode::Replace;
    SelectionDrawMode selectionDrawMode = SelectionDrawMode::Normal;
    double selectionDrawWidth = 4.0;   // ratio W or pixel W
    double selectionDrawHeight = 3.0;  // ratio H or pixel H
    ResamplingAlgorithm resamplingAlgorithm = ResamplingAlgorithm::Bilinear;
    ToolBlendMode blendMode = ToolBlendMode::Normal;
    ColorPickerBehavior colorPickerBehavior = ColorPickerBehavior::DoNotSwitch;
    ColorBgra primaryColor = ColorBgra::black();
    ColorBgra secondaryColor = ColorBgra::white();

    // Text tool settings
    QString fontFamily = QStringLiteral("Sans Serif");
    int fontSize = 24;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    TextAlign textAlign = TextAlign::Left;
};

/// Abstract base class for all tools.
class Tool : public QObject {
    Q_OBJECT

public:
    explicit Tool(DocumentWorkspace* workspace, QObject* parent = nullptr);
    ~Tool() override = default;

    [[nodiscard]] virtual QString name() const = 0;
    [[nodiscard]] virtual QCursor cursor() const { return Qt::CrossCursor; }

    /// Access the workspace and its components.
    [[nodiscard]] DocumentWorkspace* workspace() const { return m_workspace; }
    [[nodiscard]] Document* document() const;
    [[nodiscard]] HistoryStack* history() const;
    [[nodiscard]] BitmapLayer* activeLayer() const;

    /// Tool settings (shared across tools).
    [[nodiscard]] const ToolSettings& settings() const { return m_settings; }
    void setSettings(const ToolSettings& settings) { m_settings = settings; }
    ToolSettings& settingsRef() { return m_settings; }

    /// Lifecycle
    virtual void activate();
    virtual void deactivate();

    /// Event routing - called by CanvasWidget, coordinates in document space.
    virtual void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    virtual void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods);
    virtual void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    virtual void keyDown(QKeyEvent* event);
    virtual void keyUp(QKeyEvent* event);

signals:
    void cursorChanged(const QCursor& cursor);
    void statusChanged(const QString& text);

protected:
    /// Request canvas repaint.
    void invalidateCanvas();
    void invalidateCanvas(const QRect& rect);

private:
    DocumentWorkspace* m_workspace;
    ToolSettings m_settings;
};

} // namespace paintnux
