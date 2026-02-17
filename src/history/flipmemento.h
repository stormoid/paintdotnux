#pragma once

#include "history/historystack.h"
#include "data/document.h"

namespace paintnux {

enum class FlipDirection {
    Horizontal,
    Vertical
};

/// History memento that flips all layers in-place.
/// Self-inverse: flipping again restores the original.
class FlipMemento : public HistoryMemento {
public:
    FlipMemento(const QString& name, Document* document, FlipDirection direction);

protected:
    [[nodiscard]] std::unique_ptr<HistoryMemento> onUndo() override;

private:
    void applyFlip();

    Document* m_document;
    FlipDirection m_direction;
};

} // namespace paintnux
