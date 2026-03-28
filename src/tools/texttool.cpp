#include "tools/texttool.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "history/bitmaphistorymemento.h"

#include <QPainter>
#include <QFontMetricsF>
#include <cstring>

namespace paintnux {

QFont TextTool::buildFont() const {
    QFont font(settings().fontFamily, settings().fontSize);
    font.setBold(settings().bold);
    font.setItalic(settings().italic);
    font.setUnderline(settings().underline);
    font.setStrikeOut(settings().strikeout);
    return font;
}

qreal TextTool::lineXOffset(const QFontMetricsF& fm, const QString& lineText) const {
    qreal lineWidth = fm.horizontalAdvance(lineText);
    switch (settings().textAlign) {
        case TextAlign::Center: return -lineWidth / 2.0;
        case TextAlign::Right:  return -lineWidth;
        default:                return 0.0;
    }
}

QPointF TextTool::cursorDocPos() const {
    if (m_state != State::Editing) return {};

    QFont font = buildFont();
    QFontMetricsF fm(font);

    // Calculate cursor x position based on text before cursor on current line
    qreal lineHeight = fm.height();
    QString lineText = m_lines.value(m_cursorLine);
    QString beforeCursor = lineText.left(m_cursorCol);
    qreal xOff = lineXOffset(fm, lineText);
    qreal x = m_origin.x() + xOff + fm.horizontalAdvance(beforeCursor);

    // Y centered on click point, like Paint.NET
    qreal y = m_origin.y() - 0.5 * lineHeight + m_cursorLine * lineHeight;

    return QPointF(x, y);
}

qreal TextTool::cursorHeight() const {
    QFont font = buildFont();
    QFontMetricsF fm(font);
    return fm.height();
}

void TextTool::renderText() {
    auto* layer = activeLayer();
    if (!layer) return;

    // Restore saved surface first
    Surface& surf = layer->surface();
    const int w = surf.width();
    const int h = surf.height();
    for (int y = 0; y < h; ++y) {
        std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
    }

    // If there's no text yet, just invalidate and return
    bool hasText = false;
    for (const auto& line : m_lines) {
        if (!line.isEmpty()) { hasText = true; break; }
    }
    if (!hasText && m_lines.size() <= 1) {
        invalidateCanvas();
        emit editingChanged(true);
        return;
    }

    // Build font and set up painter
    QFont font = buildFont();
    QFontMetricsF fm(font);
    qreal lineHeight = fm.height();

    QPainter painter(&surf.qimage());
    if (settings().antialiased)
        painter.setRenderHint(QPainter::TextAntialiasing);
    if (settings().blendMode == ToolBlendMode::Overwrite)
        painter.setCompositionMode(QPainter::CompositionMode_Source);

    // Clip to selection if active
    auto* sel = workspace()->selection();
    if (sel && !sel->isEmpty())
        painter.setClipRegion(sel->region());

    painter.setFont(font);

    // Set color based on which button was used
    ColorBgra color = (m_button == Qt::RightButton)
        ? settings().secondaryColor
        : settings().primaryColor;
    painter.setPen(QColor(color.r, color.g, color.b, color.a));

    // Draw each line (Y centered on click point, like Paint.NET)
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].isEmpty()) continue;
        qreal xOff = lineXOffset(fm, m_lines[i]);
        qreal baselineY = m_origin.y() - 0.5 * lineHeight + i * lineHeight + fm.ascent();
        painter.drawText(QPointF(m_origin.x() + xOff, baselineY), m_lines[i]);
    }

    painter.end();
    invalidateCanvas();
    emit editingChanged(true);
}

void TextTool::commitText() {
    if (m_state != State::Editing) return;

    // Check if there's actual text to commit
    bool hasText = false;
    for (const auto& line : m_lines) {
        if (!line.isEmpty()) { hasText = true; break; }
    }

    if (hasText && m_hasSaved) {
        // Text was already rendered onto the surface. Push undo.
        auto* layer = activeLayer();
        if (layer && history()) {
            QRegion fullRegion(QRect(0, 0, layer->surface().width(), layer->surface().height()));
            auto memento = std::make_unique<BitmapHistoryMemento>(
                name(), document(), workspace()->activeLayerIndex(),
                fullRegion, std::move(m_savedSurface));
            history()->pushNewMemento(std::move(memento));
        }
    } else if (m_hasSaved) {
        // No text was typed — restore the saved surface
        auto* layer = activeLayer();
        if (layer) {
            Surface& surf = layer->surface();
            const int w = surf.width();
            const int h = surf.height();
            for (int y = 0; y < h; ++y) {
                std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
            }
            invalidateCanvas();
        }
    }

    m_state = State::Idle;
    m_hasSaved = false;
    m_lines.clear();
    m_cursorLine = 0;
    m_cursorCol = 0;

    emit editingChanged(false);
}

void TextTool::cancelText() {
    if (m_state != State::Editing) return;

    // Restore saved surface, discarding any rendered text
    if (m_hasSaved) {
        auto* layer = activeLayer();
        if (layer) {
            Surface& surf = layer->surface();
            const int w = surf.width();
            const int h = surf.height();
            for (int y = 0; y < h; ++y) {
                std::memcpy(surf.rowPtr(y), m_savedSurface.rowPtr(y), w * sizeof(ColorBgra));
            }
            invalidateCanvas();
        }
    }

    m_state = State::Idle;
    m_hasSaved = false;
    m_lines.clear();
    m_cursorLine = 0;
    m_cursorCol = 0;

    emit editingChanged(false);
}

void TextTool::mouseDown(QPointF docPos, Qt::MouseButton button, Qt::KeyboardModifiers) {
    if (button != Qt::LeftButton && button != Qt::RightButton) return;

    if (m_state == State::Editing) {
        // Commit current text, then start new
        commitText();
    }

    auto* layer = activeLayer();
    if (!layer) return;

    // Save surface for undo/restore
    m_savedSurface = layer->surface().clone();
    m_hasSaved = true;

    // Start editing
    m_state = State::Editing;
    m_origin = docPos;
    m_lines = QStringList{QString()};
    m_cursorLine = 0;
    m_cursorCol = 0;
    m_button = button;

    emit editingChanged(true);
}

void TextTool::mouseMove(QPointF, Qt::MouseButtons, Qt::KeyboardModifiers) {
    // No-op for text tool mouse move
}

void TextTool::mouseUp(QPointF, Qt::MouseButton, Qt::KeyboardModifiers) {
    // No-op for text tool mouse up
}

void TextTool::keyDown(QKeyEvent* event) {
    if (m_state != State::Editing) return;

    int key = event->key();

    if (key == Qt::Key_Escape) {
        commitText();
        return;
    }

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Insert newline
        QString currentLine = m_lines[m_cursorLine];
        QString before = currentLine.left(m_cursorCol);
        QString after = currentLine.mid(m_cursorCol);
        m_lines[m_cursorLine] = before;
        m_lines.insert(m_cursorLine + 1, after);
        m_cursorLine++;
        m_cursorCol = 0;
        renderText();
        return;
    }

    if (key == Qt::Key_Backspace) {
        if (m_cursorCol > 0) {
            QString& line = m_lines[m_cursorLine];
            line.remove(m_cursorCol - 1, 1);
            m_cursorCol--;
        } else if (m_cursorLine > 0) {
            // Join with previous line
            QString removed = m_lines.takeAt(m_cursorLine);
            m_cursorLine--;
            m_cursorCol = m_lines[m_cursorLine].length();
            m_lines[m_cursorLine].append(removed);
        }
        renderText();
        return;
    }

    if (key == Qt::Key_Delete) {
        QString& line = m_lines[m_cursorLine];
        if (m_cursorCol < line.length()) {
            line.remove(m_cursorCol, 1);
        } else if (m_cursorLine + 1 < m_lines.size()) {
            // Join with next line
            line.append(m_lines.takeAt(m_cursorLine + 1));
        }
        renderText();
        return;
    }

    // Arrow keys
    if (key == Qt::Key_Left) {
        if (m_cursorCol > 0) {
            m_cursorCol--;
        } else if (m_cursorLine > 0) {
            m_cursorLine--;
            m_cursorCol = m_lines[m_cursorLine].length();
        }
        // No re-render needed, just cursor moved
        emit editingChanged(true); // signal to update cursor display
        return;
    }
    if (key == Qt::Key_Right) {
        if (m_cursorCol < m_lines[m_cursorLine].length()) {
            m_cursorCol++;
        } else if (m_cursorLine + 1 < m_lines.size()) {
            m_cursorLine++;
            m_cursorCol = 0;
        }
        emit editingChanged(true);
        return;
    }
    if (key == Qt::Key_Up) {
        if (m_cursorLine > 0) {
            m_cursorLine--;
            m_cursorCol = std::min(m_cursorCol, static_cast<int>(m_lines[m_cursorLine].length()));
        }
        emit editingChanged(true);
        return;
    }
    if (key == Qt::Key_Down) {
        if (m_cursorLine + 1 < m_lines.size()) {
            m_cursorLine++;
            m_cursorCol = std::min(m_cursorCol, static_cast<int>(m_lines[m_cursorLine].length()));
        }
        emit editingChanged(true);
        return;
    }

    if (key == Qt::Key_Home) {
        m_cursorCol = 0;
        emit editingChanged(true);
        return;
    }
    if (key == Qt::Key_End) {
        m_cursorCol = m_lines[m_cursorLine].length();
        emit editingChanged(true);
        return;
    }

    // Printable text
    QString text = event->text();
    if (!text.isEmpty() && text[0].isPrint()) {
        m_lines[m_cursorLine].insert(m_cursorCol, text);
        m_cursorCol += text.length();
        renderText();
    }
}

void TextTool::deactivate() {
    if (m_state == State::Editing) {
        commitText();
    }
}

} // namespace paintnux
