#pragma once

#include "history/historystack.h"
#include "data/document.h"
#include "data/bitmaplayer.h"
#include "core/surface.h"

#include <QVariant>
#include <functional>

namespace paintnux {

using SetActiveIndexFn = std::function<void(int)>;

/// Memento for layer property changes (name, visibility, opacity).
class LayerPropertyMemento : public HistoryMemento {
public:
    enum Property { Name, Visible, Opacity, BlendMode };

    LayerPropertyMemento(const QString& mementoName,
                         Document* document,
                         int layerIndex,
                         Property prop,
                         QVariant savedValue);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_layerIndex;
    Property m_property;
    QVariant m_savedValue;
};

/// Memento for adding a layer (undo = remove it).
class AddLayerMemento : public HistoryMemento {
public:
    AddLayerMemento(const QString& mementoName,
                    Document* document,
                    int insertIndex,
                    SetActiveIndexFn setActiveIndex,
                    int savedActiveIndex);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_insertIndex;
    SetActiveIndexFn m_setActiveIndex;
    int m_savedActiveIndex;
    std::unique_ptr<Layer> m_savedLayer; // non-null after undo (holds removed layer)
};

/// Memento for deleting a layer (undo = re-insert it).
class DeleteLayerMemento : public HistoryMemento {
public:
    DeleteLayerMemento(const QString& mementoName,
                       Document* document,
                       int removedIndex,
                       std::unique_ptr<Layer> savedLayer,
                       SetActiveIndexFn setActiveIndex,
                       int savedActiveIndex);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_removedIndex;
    std::unique_ptr<Layer> m_savedLayer;
    SetActiveIndexFn m_setActiveIndex;
    int m_savedActiveIndex;
};

/// Memento for moving a layer (undo = move back).
class MoveLayerMemento : public HistoryMemento {
public:
    MoveLayerMemento(const QString& mementoName,
                     Document* document,
                     int fromIndex,
                     int toIndex,
                     SetActiveIndexFn setActiveIndex,
                     int savedActiveIndex);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_fromIndex;
    int m_toIndex;
    SetActiveIndexFn m_setActiveIndex;
    int m_savedActiveIndex;
};

/// Memento for merging a layer down (undo = restore both layers).
class MergeLayerDownMemento : public HistoryMemento {
public:
    MergeLayerDownMemento(const QString& mementoName,
                          Document* document,
                          int mergeIndex,
                          std::unique_ptr<Layer> savedTopLayer,
                          Surface savedBottomSurface,
                          SetActiveIndexFn setActiveIndex,
                          int savedActiveIndex);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    Document* m_document;
    int m_mergeIndex;
    std::unique_ptr<Layer> m_savedTopLayer;
    Surface m_savedBottomSurface;
    SetActiveIndexFn m_setActiveIndex;
    int m_savedActiveIndex;
};

} // namespace paintnux
