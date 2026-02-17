#include "data/selection.h"


namespace paintnux {

SelectionCombineMode combineModeFromModifiers(Qt::KeyboardModifiers mods,
                                               SelectionCombineMode defaultMode) {
    bool ctrl = mods & Qt::ControlModifier;
    bool shift = mods & Qt::ShiftModifier;
    bool alt = mods & Qt::AltModifier;

    if (ctrl && shift) return SelectionCombineMode::Intersect;
    if (ctrl)          return SelectionCombineMode::Xor;
    if (shift)         return SelectionCombineMode::Union;
    if (alt)           return SelectionCombineMode::Exclude;
    return defaultMode;
}

Selection::Selection(QObject* parent)
    : QObject(parent) {
}

bool Selection::isEmpty() const {
    return m_path.isEmpty();
}

const QRegion& Selection::region() const {
    if (m_regionDirty) {
        if (m_path.isEmpty()) {
            m_cachedRegion = QRegion();
        } else {
            // Pass the path's fill rule to QRegion so holes (e.g. from invert) work correctly
            m_cachedRegion = QRegion(m_path.toFillPolygon().toPolygon(),
                                     m_path.fillRule());
        }
        m_regionDirty = false;
    }
    return m_cachedRegion;
}

QPainterPath Selection::displayPath() const {
    if (!m_hasContinuation) return m_path;
    return combinePaths(m_path, m_continuationPath, m_continuationMode);
}

void Selection::setContinuation(const QPainterPath& contPath, SelectionCombineMode mode) {
    m_hasContinuation = true;
    m_continuationPath = contPath;
    m_continuationMode = mode;
    emit changed();
}

void Selection::commitContinuation() {
    if (!m_hasContinuation) return;
    m_path = combinePaths(m_path, m_continuationPath, m_continuationMode);
    m_hasContinuation = false;
    m_continuationPath = QPainterPath();
    invalidateCache();
    emit changed();
}

void Selection::clearContinuation() {
    if (!m_hasContinuation) return;
    m_hasContinuation = false;
    m_continuationPath = QPainterPath();
    emit changed();
}

void Selection::setPath(const QPainterPath& path) {
    m_path = path;
    m_hasContinuation = false;
    m_continuationPath = QPainterPath();
    invalidateCache();
    emit changed();
}

void Selection::reset() {
    m_path = QPainterPath();
    m_hasContinuation = false;
    m_continuationPath = QPainterPath();
    invalidateCache();
    emit changed();
}

void Selection::selectAll(const QRect& bounds) {
    m_path = QPainterPath();
    m_path.addRect(bounds);
    m_hasContinuation = false;
    m_continuationPath = QPainterPath();
    invalidateCache();
    emit changed();
}

void Selection::invert(const QRect& bounds) {
    QPainterPath full;
    full.addRect(bounds);
    if (m_path.isEmpty()) {
        m_path = full;
    } else {
        m_path = full.subtracted(m_path);
    }
    invalidateCache();
    emit changed();
}

void Selection::invalidateCache() {
    m_regionDirty = true;
}

QPainterPath Selection::combinePaths(const QPainterPath& base,
                                     const QPainterPath& addition,
                                     SelectionCombineMode mode) {
    switch (mode) {
    case SelectionCombineMode::Replace:
        return addition;
    case SelectionCombineMode::Union:
        return base.united(addition);
    case SelectionCombineMode::Exclude:
        return base.subtracted(addition);
    case SelectionCombineMode::Xor: {
        // XOR = (A union B) - (A intersect B)
        QPainterPath united = base.united(addition);
        QPainterPath intersected = base.intersected(addition);
        return united.subtracted(intersected);
    }
    case SelectionCombineMode::Intersect:
        return base.intersected(addition);
    }
    return addition;
}

} // namespace paintnux
