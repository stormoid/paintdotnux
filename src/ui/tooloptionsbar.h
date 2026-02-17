#pragma once

#include <QToolBar>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>

#include <vector>

namespace paintnux {

struct ToolSettings;
class Tool;

/// Toolbar showing tool-specific options (brush size, tolerance, etc.).
class ToolOptionsBar : public QToolBar {
    Q_OBJECT

public:
    explicit ToolOptionsBar(QWidget* parent = nullptr);

    void applyTo(ToolSettings& settings) const;
    void loadFrom(const ToolSettings& settings);

    /// Show/hide controls based on the active tool type.
    void showForTool(Tool* tool);

signals:
    void settingsChanged();

private:
    // Selection combine mode (selection tools only)
    QComboBox* m_selectionMode;
    std::vector<QAction*> m_selectionModeActions;

    // Selection draw mode (rect/ellipse select only)
    QComboBox* m_selectionDrawMode;
    QDoubleSpinBox* m_selDrawWidth;
    QDoubleSpinBox* m_selDrawHeight;
    std::vector<QAction*> m_selDrawModeActions;
    std::vector<QAction*> m_selDrawSizeActions;  // width/height (hidden in Normal mode)

    // Resampling (move tool only)
    QComboBox* m_resampling;
    std::vector<QAction*> m_resamplingActions;

    QSpinBox* m_brushSize;
    std::vector<QAction*> m_brushSizeActions;

    // Flood mode (bucket, magic wand)
    QComboBox* m_floodMode;
    std::vector<QAction*> m_floodModeActions;

    QSlider* m_toleranceSlider;
    QLabel* m_toleranceLabel;
    std::vector<QAction*> m_toleranceActions;
    QAction* m_toleranceSeparator = nullptr;

    QCheckBox* m_antialiased;
    QAction* m_antialiasedAction = nullptr;
    QComboBox* m_shapeDrawType;
    std::vector<QAction*> m_shapeActions;
    QAction* m_shapeSeparator = nullptr;

    // Gradient tool controls
    QComboBox* m_gradientType;
    std::vector<QAction*> m_gradientActions;
    QAction* m_gradientSeparator = nullptr;

    // Text tool controls (and their separators/labels for show/hide)
    QFontComboBox* m_fontFamily;
    QSpinBox* m_fontSize;
    QCheckBox* m_bold;
    QCheckBox* m_italic;
    QCheckBox* m_underline;
    QCheckBox* m_strikeout;
    QComboBox* m_textAlign;

    // Actions that belong to the font section (for show/hide)
    std::vector<QAction*> m_fontActions;
    QAction* m_fontSeparator = nullptr;

    // Blend mode (Normal / Overwrite)
    QComboBox* m_blendMode;
    std::vector<QAction*> m_blendModeActions;
    QAction* m_blendModeSeparator = nullptr;

    // Color picker after-click behavior
    QComboBox* m_pickerBehavior;
    std::vector<QAction*> m_pickerBehaviorActions;
};

} // namespace paintnux
