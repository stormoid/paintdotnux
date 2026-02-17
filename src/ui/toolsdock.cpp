#include "ui/toolsdock.h"
#include "tools/tool.h"

#include <QGridLayout>
#include <QWidget>

#include <algorithm>

namespace paintnux {

ToolsDock::ToolsDock(QWidget* parent)
    : QDockWidget(tr("Tools"), parent)
    , m_buttonGroup(new QButtonGroup(this)) {
    setFeatures(QDockWidget::NoDockWidgetFeatures);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto* content = new QWidget;
    auto* layout = new QGridLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    content->setLayout(layout);
    setWidget(content);

    m_buttonGroup->setExclusive(true);
    connect(m_buttonGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (id >= 0 && id < static_cast<int>(m_tools.size())) {
            emit toolSelected(m_tools[id]);
        }
    });
}

void ToolsDock::addTool(Tool* tool, const QString& iconText, char shortcutKey) {
    int idx = static_cast<int>(m_tools.size());
    m_tools.push_back(tool);

    auto* btn = new QToolButton;
    btn->setText(iconText);

    // Build tooltip with shortcut hint
    QString tip = tool->name();
    if (shortcutKey) {
        tip += QStringLiteral(" (%1)").arg(QChar(shortcutKey).toUpper());
        m_shortcutGroups[shortcutKey].push_back(idx);
    }
    btn->setToolTip(tip);

    btn->setCheckable(true);
    btn->setFixedSize(43, 43);
    btn->setFont(QFont("monospace", 18));

    m_buttonGroup->addButton(btn, idx);

    auto* layout = static_cast<QGridLayout*>(widget()->layout());
    int row = idx / 3;
    int col = idx % 3;
    layout->addWidget(btn, row, col);

    if (idx == 0) btn->setChecked(true);
}

void ToolsDock::selectTool(int index) {
    auto* btn = m_buttonGroup->button(index);
    if (btn) {
        btn->setChecked(true);
        emit toolSelected(m_tools[index]);
    }
}

bool ToolsDock::activateShortcut(char key) {
    auto it = m_shortcutGroups.find(key);
    if (it == m_shortcutGroups.end())
        return false;

    const auto& group = it->second;
    if (group.empty()) return false;

    // Find current tool index in this group
    int checkedId = m_buttonGroup->checkedId();
    auto pos = std::find(group.begin(), group.end(), checkedId);

    int nextIdx;
    if (pos != group.end()) {
        // Current tool is in this group — cycle to next
        auto nextPos = pos + 1;
        nextIdx = (nextPos == group.end()) ? group.front() : *nextPos;
    } else {
        // Not in this group — select first
        nextIdx = group.front();
    }

    selectTool(nextIdx);
    return true;
}

} // namespace paintnux
