#pragma once

#include <QObject>
#include <QString>

#include <deque>
#include <memory>

namespace paintnux {

class Document;

/// Abstract base for history mementos (undo/redo entries).
/// Implements the symmetric swap pattern: PerformUndo() returns a memento
/// that, when itself undone, restores the original state.
class HistoryMemento {
public:
    HistoryMemento(const QString& name) : m_name(name) {}
    virtual ~HistoryMemento() = default;

    [[nodiscard]] const QString& name() const { return m_name; }

    /// Execute the undo/redo. Returns the reverse memento.
    [[nodiscard]] std::unique_ptr<HistoryMemento> performUndo() {
        return onUndo();
    }

protected:
    /// Subclass implements the actual undo logic, returning the reverse memento.
    [[nodiscard]] virtual std::unique_ptr<HistoryMemento> onUndo() = 0;

private:
    QString m_name;
};

/// Undo/redo history stack.
class HistoryStack : public QObject {
    Q_OBJECT

public:
    explicit HistoryStack(Document* document, QObject* parent = nullptr);

    /// Push a new action onto the undo stack (clears redo stack).
    void pushNewMemento(std::unique_ptr<HistoryMemento> memento);

    /// Undo the most recent action.
    void stepBackward();

    /// Redo the most recently undone action.
    void stepForward();

    [[nodiscard]] bool canUndo() const { return !m_undoStack.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redoStack.empty(); }

    [[nodiscard]] int undoCount() const { return static_cast<int>(m_undoStack.size()); }
    [[nodiscard]] int redoCount() const { return static_cast<int>(m_redoStack.size()); }

    /// Get the name of the entry at the given undo index (0 = most recent).
    [[nodiscard]] QString undoName(int index) const;

    /// Get all undo entry names (most recent first).
    [[nodiscard]] QStringList undoNames() const;

    /// Clear all history.
    void clearAll();

    /// Replace the document pointer (used by ReplaceDocumentMemento).
    void setDocument(Document* doc) { m_document = doc; }

signals:
    void changed();
    void historyFlushed();
    void undoPushed(const QString& name);
    void steppedBackward();
    void steppedForward();

private:
    Document* m_document;
    std::deque<std::unique_ptr<HistoryMemento>> m_undoStack;
    std::deque<std::unique_ptr<HistoryMemento>> m_redoStack;
    bool m_isExecuting = false;
};

} // namespace paintnux
