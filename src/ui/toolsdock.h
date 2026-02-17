#pragma once

#include <QDockWidget>
#include <QToolButton>
#include <QButtonGroup>

#include <map>
#include <vector>

namespace paintnux {

class Tool;

/// Dock widget showing a grid of tool buttons.
class ToolsDock : public QDockWidget {
    Q_OBJECT

public:
    explicit ToolsDock(QWidget* parent = nullptr);

    /// Add a tool button with an optional shortcut key (0 = none).
    void addTool(Tool* tool, const QString& iconText, char shortcutKey = 0);

    /// Select a tool by index.
    void selectTool(int index);

    /// Try to activate a tool by shortcut key. Returns true if handled.
    /// Repeated calls with the same key cycle through tools sharing that key.
    bool activateShortcut(char key);

signals:
    void toolSelected(Tool* tool);

private:
    QButtonGroup* m_buttonGroup;
    std::vector<Tool*> m_tools;

    // Shortcut cycling: key -> list of tool indices
    std::map<char, std::vector<int>> m_shortcutGroups;
};

} // namespace paintnux
