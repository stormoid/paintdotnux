#pragma once

#include "tools/tool.h"
#include "core/surface.h"

#include <QPointF>
#include <QStringList>
#include <QFont>
#include <QFontMetricsF>

namespace paintnux {

/// Text tool — click to place, type live on canvas, Escape to commit.
/// Text is rasterized onto the active layer on commit.
class TextTool : public Tool {
    Q_OBJECT

public:
    using Tool::Tool;

    [[nodiscard]] QString name() const override { return tr("Text"); }
    [[nodiscard]] QCursor cursor() const override { return Qt::IBeamCursor; }

    void mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void mouseMove(QPointF docPos, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods) override;
    void mouseUp(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers mods) override;
    void keyDown(QKeyEvent* event) override;
    void deactivate() override;

    /// Whether the tool is currently editing text (for cursor overlay).
    [[nodiscard]] bool isEditing() const { return m_state == State::Editing; }

    /// Position and height of the text cursor in document coordinates.
    [[nodiscard]] QPointF cursorDocPos() const;
    [[nodiscard]] qreal cursorHeight() const;

    /// Discard in-progress text without committing (for undo during editing).
    void cancelText();

signals:
    /// Emitted when editing state changes (for cursor overlay updates).
    void editingChanged(bool editing);

private:
    enum class State { Idle, Editing };
    State m_state = State::Idle;

    QPointF m_origin;               // top-left of text block in doc coords
    QStringList m_lines;            // text lines (Enter creates new line)
    int m_cursorLine = 0;           // current line index
    int m_cursorCol = 0;            // current column index
    Qt::MouseButton m_button = Qt::LeftButton; // which button started (for color)

    Surface m_savedSurface{1, 1};
    bool m_hasSaved = false;

    void renderText();              // restore surface + draw text via QPainter
    void commitText();              // push BitmapHistoryMemento
    QFont buildFont() const;        // build QFont from ToolSettings
    qreal lineXOffset(const QFontMetricsF& fm, const QString& lineText) const;
};

} // namespace paintnux
