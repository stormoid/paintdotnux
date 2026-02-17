#include "ui/historydock.h"
#include "history/historystack.h"

namespace paintnux {

HistoryDock::HistoryDock(QWidget* parent)
    : QDockWidget(tr("History"), parent) {
    setFeatures(QDockWidget::NoDockWidgetFeatures);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    setWidget(m_list);
    setMinimumHeight(120);
}

void HistoryDock::setHistoryStack(HistoryStack* stack) {
    if (m_stack) {
        m_stack->disconnect(this);
    }
    m_stack = stack;
    if (m_stack) {
        connect(m_stack, &HistoryStack::changed, this, &HistoryDock::refresh);
    }
    refresh();
}

void HistoryDock::refresh() {
    m_list->clear();
    if (!m_stack) return;

    QStringList names = m_stack->undoNames();
    for (const QString& name : names) {
        m_list->addItem(name);
    }

    if (m_list->count() == 0) {
        m_list->addItem(tr("(No history)"));
    }
}

} // namespace paintnux
