#pragma once

#include <QObject>
#include <QPainterPath>
#include <QRegion>
#include <QRect>

namespace paintnux {

/// Selection combine modes, determined by keyboard modifiers.
enum class SelectionCombineMode {
    Replace,    // No modifier
    Union,      // Shift
    Exclude,    // Alt
    Xor,        // Ctrl
    Intersect   // Ctrl+Shift
};

/// Determine combine mode from keyboard modifiers.
/// If no modifier keys are pressed, returns defaultMode.
SelectionCombineMode combineModeFromModifiers(Qt::KeyboardModifiers mods,
    SelectionCombineMode defaultMode = SelectionCombineMode::Replace);

/// Selection data model.
/// Stores a QPainterPath as the selection shape with a cached QRegion for fast pixel tests.
/// Supports a "continuation" pattern: during drag, a temporary path previews the result.
/// On mouseUp, the continuation is committed into the base path via the combine mode.
class Selection : public QObject {
    Q_OBJECT

public:
    explicit Selection(QObject* parent = nullptr);

    /// Is the selection empty?
    [[nodiscard]] bool isEmpty() const;

    /// The committed selection path.
    [[nodiscard]] const QPainterPath& path() const { return m_path; }

    /// Cached QRegion for fast per-pixel containment checks.
    [[nodiscard]] const QRegion& region() const;

    /// Display path = base path combined with continuation preview (if any).
    [[nodiscard]] QPainterPath displayPath() const;

    // --- Continuation pattern (drag preview) ---

    /// Set a temporary continuation path that previews the result during drag.
    void setContinuation(const QPainterPath& contPath, SelectionCombineMode mode);

    /// Commit the continuation into the base path.
    void commitContinuation();

    /// Discard the continuation without committing.
    void clearContinuation();

    [[nodiscard]] bool hasContinuation() const { return m_hasContinuation; }

    // --- Direct path manipulation ---

    /// Set the selection path directly (e.g., for undo/redo).
    void setPath(const QPainterPath& path);

    /// Clear the selection entirely.
    void reset();

    /// Select the entire document bounds.
    void selectAll(const QRect& bounds);

    /// Invert the selection within the given bounds.
    void invert(const QRect& bounds);

signals:
    void changed();

private:
    void invalidateCache();

    /// Combine two paths according to the given mode.
    static QPainterPath combinePaths(const QPainterPath& base,
                                     const QPainterPath& addition,
                                     SelectionCombineMode mode);

    QPainterPath m_path;

    // Continuation (drag preview)
    bool m_hasContinuation = false;
    QPainterPath m_continuationPath;
    SelectionCombineMode m_continuationMode = SelectionCombineMode::Replace;

    // Cached region
    mutable bool m_regionDirty = true;
    mutable QRegion m_cachedRegion;
};

} // namespace paintnux
