#pragma once

#include <QDockWidget>
#include <QListWidget>
#include <QPointer>

namespace paintnux {

class HistoryStack;

/// Dock showing the undo history list.
class HistoryDock : public QDockWidget {
    Q_OBJECT

public:
    explicit HistoryDock(QWidget* parent = nullptr);

    void setHistoryStack(HistoryStack* stack);

private:
    void refresh();

    QListWidget* m_list;
    QPointer<HistoryStack> m_stack;
};

} // namespace paintnux
