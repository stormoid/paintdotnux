#include "ui/tooloptionsbar.h"
#include "tools/tool.h"
#include "tools/texttool.h"
#include "tools/gradienttool.h"
#include "tools/shapetools.h"
#include "tools/paintbuckettool.h"
#include "tools/magicwandtool.h"
#include "tools/recolortool.h"
#include "tools/brushtool.h"
#include "tools/clonestamptool.h"
#include "tools/selecttools.h"
#include "tools/movetools.h"
#include "tools/colorpickertool.h"

namespace paintnux {

ToolOptionsBar::ToolOptionsBar(QWidget* parent)
    : QToolBar(tr("Tool Options"), parent) {
    setMovable(false);
    setFixedHeight(32);

    // Selection combine mode (selection tools only)
    m_selectionMode = new QComboBox;
    m_selectionMode->addItem(tr("Replace"));
    m_selectionMode->addItem(tr("Add (Union)"));
    m_selectionMode->addItem(tr("Subtract"));
    m_selectionMode->addItem(tr("Invert (Xor)"));
    m_selectionMode->addItem(tr("Intersect"));
    m_selectionMode->setCurrentIndex(0);
    connect(m_selectionMode, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    m_selectionModeActions.push_back(addWidget(new QLabel(tr(" Selection Mode: "))));
    m_selectionModeActions.push_back(addWidget(m_selectionMode));

    // Hide selection mode by default
    for (auto* a : m_selectionModeActions) a->setVisible(false);

    // Selection draw mode (rect/ellipse select only)
    m_selectionDrawMode = new QComboBox;
    m_selectionDrawMode->addItem(tr("Normal"));
    m_selectionDrawMode->addItem(tr("Fixed Ratio"));
    m_selectionDrawMode->addItem(tr("Fixed Size"));
    m_selectionDrawMode->setCurrentIndex(0);
    connect(m_selectionDrawMode, &QComboBox::currentIndexChanged, this, [this](int idx) {
        bool showSize = (idx != 0);
        for (auto* a : m_selDrawSizeActions) a->setVisible(showSize);
        // Update suffix and defaults based on mode
        if (idx == 2) {
            // Fixed Size: pixel values
            m_selDrawWidth->setSuffix(tr(" px"));
            m_selDrawHeight->setSuffix(tr(" px"));
            m_selDrawWidth->setDecimals(0);
            m_selDrawHeight->setDecimals(0);
            if (m_selDrawWidth->value() < 10) m_selDrawWidth->setValue(100);
            if (m_selDrawHeight->value() < 10) m_selDrawHeight->setValue(100);
        } else {
            // Fixed Ratio: no suffix
            m_selDrawWidth->setSuffix(QString());
            m_selDrawHeight->setSuffix(QString());
            m_selDrawWidth->setDecimals(1);
            m_selDrawHeight->setDecimals(1);
        }
        emit settingsChanged();
    });

    m_selDrawWidth = new QDoubleSpinBox;
    m_selDrawWidth->setRange(0.1, 10000.0);
    m_selDrawWidth->setValue(4.0);
    m_selDrawWidth->setDecimals(1);
    connect(m_selDrawWidth, &QDoubleSpinBox::valueChanged, this, &ToolOptionsBar::settingsChanged);

    m_selDrawHeight = new QDoubleSpinBox;
    m_selDrawHeight->setRange(0.1, 10000.0);
    m_selDrawHeight->setValue(3.0);
    m_selDrawHeight->setDecimals(1);
    connect(m_selDrawHeight, &QDoubleSpinBox::valueChanged, this, &ToolOptionsBar::settingsChanged);

    m_selDrawModeActions.push_back(addWidget(m_selectionDrawMode));
    m_selDrawSizeActions.push_back(addWidget(m_selDrawWidth));
    m_selDrawSizeActions.push_back(addWidget(new QLabel(tr(" x "))));
    m_selDrawSizeActions.push_back(addWidget(m_selDrawHeight));

    // Hide by default
    for (auto* a : m_selDrawModeActions) a->setVisible(false);
    for (auto* a : m_selDrawSizeActions) a->setVisible(false);

    // Resampling (move tool only)
    m_resampling = new QComboBox;
    m_resampling->addItem(tr("Bilinear"));
    m_resampling->addItem(tr("Nearest Neighbor"));
    m_resampling->setCurrentIndex(0);
    connect(m_resampling, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    m_resamplingActions.push_back(addWidget(new QLabel(tr(" Resampling: "))));
    m_resamplingActions.push_back(addWidget(m_resampling));

    // Hide by default
    for (auto* a : m_resamplingActions) a->setVisible(false);

    // Flood mode (bucket, magic wand)
    m_floodMode = new QComboBox;
    m_floodMode->addItem(tr("Contiguous"));
    m_floodMode->addItem(tr("Global"));
    m_floodMode->setCurrentIndex(0);
    connect(m_floodMode, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    m_floodModeActions.push_back(addWidget(new QLabel(tr(" Flood Mode: "))));
    m_floodModeActions.push_back(addWidget(m_floodMode));

    // Hide by default
    for (auto* a : m_floodModeActions) a->setVisible(false);

    // Brush size
    m_brushSize = new QSpinBox;
    m_brushSize->setRange(1, 200);
    m_brushSize->setValue(5);
    m_brushSize->setSuffix(tr(" px"));
    connect(m_brushSize, &QSpinBox::valueChanged, this, &ToolOptionsBar::settingsChanged);

    m_brushSizeActions.push_back(addWidget(new QLabel(tr(" Size: "))));
    m_brushSizeActions.push_back(addWidget(m_brushSize));

    // Hide brush size by default
    for (auto* a : m_brushSizeActions) a->setVisible(false);

    // Tolerance (bucket, magic wand, recolor only)
    m_toleranceSlider = new QSlider(Qt::Horizontal);
    m_toleranceSlider->setRange(0, 100);
    m_toleranceSlider->setValue(50);
    m_toleranceSlider->setFixedWidth(100);

    m_toleranceLabel = new QLabel("50%");
    m_toleranceLabel->setFixedWidth(35);
    connect(m_toleranceSlider, &QSlider::valueChanged, this, [this](int val) {
        m_toleranceLabel->setText(QStringLiteral("%1%").arg(val));
        emit settingsChanged();
    });

    m_toleranceSeparator = addSeparator();
    m_toleranceActions.push_back(addWidget(new QLabel(tr(" Tolerance: "))));
    m_toleranceActions.push_back(addWidget(m_toleranceSlider));
    m_toleranceActions.push_back(addWidget(m_toleranceLabel));

    // Hide tolerance section by default
    m_toleranceSeparator->setVisible(false);
    for (auto* a : m_toleranceActions) a->setVisible(false);

    // Antialiased
    m_antialiased = new QCheckBox(tr("Antialiased"));
    m_antialiased->setChecked(true);
    m_antialiasedAction = addWidget(m_antialiased);
    connect(m_antialiased, &QCheckBox::toggled, this, &ToolOptionsBar::settingsChanged);

    // Hide antialiased by default
    m_antialiasedAction->setVisible(false);

    // Shape draw type (shape tools only)
    m_shapeDrawType = new QComboBox;
    m_shapeDrawType->addItem(tr("Outline"));
    m_shapeDrawType->addItem(tr("Fill"));
    m_shapeDrawType->addItem(tr("Both"));
    m_shapeDrawType->setCurrentIndex(2); // Both
    connect(m_shapeDrawType, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    m_shapeSeparator = addSeparator();
    m_shapeActions.push_back(addWidget(new QLabel(tr(" Shape: "))));
    m_shapeActions.push_back(addWidget(m_shapeDrawType));

    // Hide shape section by default
    m_shapeSeparator->setVisible(false);
    for (auto* a : m_shapeActions) a->setVisible(false);

    // Gradient type (gradient tool only)
    m_gradientType = new QComboBox;
    m_gradientType->addItem(tr("Linear (Clamped)"));
    m_gradientType->addItem(tr("Linear (Reflected)"));
    m_gradientType->addItem(tr("Linear (Diamond)"));
    m_gradientType->addItem(tr("Radial"));
    m_gradientType->addItem(tr("Conical"));
    m_gradientType->setCurrentIndex(0);
    connect(m_gradientType, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    m_gradientSeparator = addSeparator();
    m_gradientActions.push_back(addWidget(new QLabel(tr(" Gradient: "))));
    m_gradientActions.push_back(addWidget(m_gradientType));

    // Hide gradient section by default
    m_gradientSeparator->setVisible(false);
    for (auto* a : m_gradientActions) a->setVisible(false);

    // --- Font section (text tool only) ---
    m_fontSeparator = addSeparator();

    m_fontFamily = new QFontComboBox;
    m_fontFamily->setCurrentFont(QFont("Sans Serif"));
    connect(m_fontFamily, &QFontComboBox::currentFontChanged, this, &ToolOptionsBar::settingsChanged);

    m_fontSize = new QSpinBox;
    m_fontSize->setRange(8, 200);
    m_fontSize->setValue(24);
    m_fontSize->setSuffix(tr(" pt"));
    connect(m_fontSize, &QSpinBox::valueChanged, this, &ToolOptionsBar::settingsChanged);

    m_bold = new QCheckBox(tr("B"));
    m_bold->setStyleSheet("font-weight: bold;");
    connect(m_bold, &QCheckBox::toggled, this, &ToolOptionsBar::settingsChanged);

    m_italic = new QCheckBox(tr("I"));
    m_italic->setStyleSheet("font-style: italic;");
    connect(m_italic, &QCheckBox::toggled, this, &ToolOptionsBar::settingsChanged);

    m_underline = new QCheckBox(tr("U"));
    m_underline->setStyleSheet("text-decoration: underline;");
    connect(m_underline, &QCheckBox::toggled, this, &ToolOptionsBar::settingsChanged);

    m_strikeout = new QCheckBox(tr("S"));
    m_strikeout->setStyleSheet("text-decoration: line-through;");
    connect(m_strikeout, &QCheckBox::toggled, this, &ToolOptionsBar::settingsChanged);

    m_textAlign = new QComboBox;
    m_textAlign->addItem(tr("Left"));
    m_textAlign->addItem(tr("Center"));
    m_textAlign->addItem(tr("Right"));
    m_textAlign->setCurrentIndex(0);
    connect(m_textAlign, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);

    // Add font widgets to toolbar and store their QActions for show/hide
    m_fontActions.push_back(addWidget(new QLabel(tr(" Font: "))));
    m_fontActions.push_back(addWidget(m_fontFamily));
    m_fontActions.push_back(addWidget(m_fontSize));
    m_fontActions.push_back(addWidget(m_bold));
    m_fontActions.push_back(addWidget(m_italic));
    m_fontActions.push_back(addWidget(m_underline));
    m_fontActions.push_back(addWidget(m_strikeout));
    m_fontActions.push_back(addWidget(new QLabel(tr(" Align: "))));
    m_fontActions.push_back(addWidget(m_textAlign));

    // Hide font section by default
    m_fontSeparator->setVisible(false);
    for (auto* a : m_fontActions) a->setVisible(false);

    // Blend mode (Normal / Overwrite)
    m_blendModeSeparator = addSeparator();
    m_blendMode = new QComboBox;
    m_blendMode->addItem(tr("Normal Blending"));
    m_blendMode->addItem(tr("Overwrite"));
    m_blendMode->setCurrentIndex(0);
    connect(m_blendMode, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);
    m_blendModeActions.push_back(addWidget(new QLabel(tr(" Blending: "))));
    m_blendModeActions.push_back(addWidget(m_blendMode));

    // Hide blend mode by default
    m_blendModeSeparator->setVisible(false);
    for (auto* a : m_blendModeActions) a->setVisible(false);

    // Color picker after-click behavior
    m_pickerBehavior = new QComboBox;
    m_pickerBehavior->addItem(tr("Do not switch"));
    m_pickerBehavior->addItem(tr("Switch to previous tool"));
    m_pickerBehavior->addItem(tr("Switch to Pencil"));
    m_pickerBehavior->setCurrentIndex(1);
    connect(m_pickerBehavior, &QComboBox::currentIndexChanged, this, &ToolOptionsBar::settingsChanged);
    m_pickerBehaviorActions.push_back(addWidget(new QLabel(tr(" After sampling: "))));
    m_pickerBehaviorActions.push_back(addWidget(m_pickerBehavior));

    // Hide by default
    for (auto* a : m_pickerBehaviorActions) a->setVisible(false);
}

void ToolOptionsBar::applyTo(ToolSettings& settings) const {
    // Combo index order: Replace=0, Union=1, Exclude=2, Xor=3, Intersect=4
    static constexpr SelectionCombineMode modeMap[] = {
        SelectionCombineMode::Replace, SelectionCombineMode::Union,
        SelectionCombineMode::Exclude, SelectionCombineMode::Xor,
        SelectionCombineMode::Intersect
    };
    int idx = m_selectionMode->currentIndex();
    settings.selectionCombineMode = (idx >= 0 && idx < 5) ? modeMap[idx] : SelectionCombineMode::Replace;
    settings.selectionDrawMode = static_cast<SelectionDrawMode>(m_selectionDrawMode->currentIndex());
    settings.selectionDrawWidth = m_selDrawWidth->value();
    settings.selectionDrawHeight = m_selDrawHeight->value();
    // Combo: 0=Bilinear, 1=NearestNeighbor
    settings.resamplingAlgorithm = (m_resampling->currentIndex() == 0)
        ? ResamplingAlgorithm::Bilinear : ResamplingAlgorithm::NearestNeighbor;
    settings.floodMode = static_cast<FloodMode>(m_floodMode->currentIndex());
    settings.brushSize = m_brushSize->value();
    settings.tolerance = m_toleranceSlider->value();
    settings.antialiased = m_antialiased->isChecked();
    settings.shapeDrawType = static_cast<ShapeDrawType>(m_shapeDrawType->currentIndex());
    settings.gradientType = static_cast<GradientType>(m_gradientType->currentIndex());
    settings.fontFamily = m_fontFamily->currentFont().family();
    settings.fontSize = m_fontSize->value();
    settings.bold = m_bold->isChecked();
    settings.italic = m_italic->isChecked();
    settings.underline = m_underline->isChecked();
    settings.strikeout = m_strikeout->isChecked();
    settings.textAlign = static_cast<TextAlign>(m_textAlign->currentIndex());
    settings.blendMode = static_cast<ToolBlendMode>(m_blendMode->currentIndex());
    settings.colorPickerBehavior = static_cast<ColorPickerBehavior>(m_pickerBehavior->currentIndex());
}

void ToolOptionsBar::loadFrom(const ToolSettings& settings) {
    // Reverse map: enum -> combo index
    static constexpr int indexMap[] = { 0, 1, 2, 3, 4 };
    m_selectionMode->setCurrentIndex(indexMap[static_cast<int>(settings.selectionCombineMode)]);
    m_selectionDrawMode->setCurrentIndex(static_cast<int>(settings.selectionDrawMode));
    m_selDrawWidth->setValue(settings.selectionDrawWidth);
    m_selDrawHeight->setValue(settings.selectionDrawHeight);
    m_resampling->setCurrentIndex(
        settings.resamplingAlgorithm == ResamplingAlgorithm::Bilinear ? 0 : 1);
    m_floodMode->setCurrentIndex(static_cast<int>(settings.floodMode));
    m_brushSize->setValue(settings.brushSize);
    m_toleranceSlider->setValue(settings.tolerance);
    m_antialiased->setChecked(settings.antialiased);
    m_shapeDrawType->setCurrentIndex(static_cast<int>(settings.shapeDrawType));
    m_gradientType->setCurrentIndex(static_cast<int>(settings.gradientType));
    m_fontFamily->setCurrentFont(QFont(settings.fontFamily));
    m_fontSize->setValue(settings.fontSize);
    m_bold->setChecked(settings.bold);
    m_italic->setChecked(settings.italic);
    m_underline->setChecked(settings.underline);
    m_strikeout->setChecked(settings.strikeout);
    m_textAlign->setCurrentIndex(static_cast<int>(settings.textAlign));
    m_blendMode->setCurrentIndex(static_cast<int>(settings.blendMode));
    m_pickerBehavior->setCurrentIndex(static_cast<int>(settings.colorPickerBehavior));
}

void ToolOptionsBar::showForTool(Tool* tool) {
    // Selection mode: selection tools + magic wand
    bool isSelBase = dynamic_cast<SelectionToolBase*>(tool) != nullptr;
    bool isLasso = dynamic_cast<LassoSelectTool*>(tool) != nullptr;
    bool isWand = dynamic_cast<MagicWandTool*>(tool) != nullptr;
    bool isSelection = isSelBase || isLasso || isWand;
    for (auto* a : m_selectionModeActions) a->setVisible(isSelection);

    // Selection draw mode: rectangle select only
    bool isRectSel = dynamic_cast<RectangleSelectTool*>(tool) != nullptr;
    for (auto* a : m_selDrawModeActions) a->setVisible(isRectSel);
    bool showDrawSize = isRectSel && m_selectionDrawMode->currentIndex() != 0;
    for (auto* a : m_selDrawSizeActions) a->setVisible(showDrawSize);

    // Resampling: move tool only
    bool isMove = dynamic_cast<MoveTool*>(tool) != nullptr;
    for (auto* a : m_resamplingActions) a->setVisible(isMove);

    bool isBucket = dynamic_cast<PaintBucketTool*>(tool) != nullptr;

    // Flood mode: bucket and magic wand
    bool hasFloodMode = isBucket || isWand;
    for (auto* a : m_floodModeActions) a->setVisible(hasFloodMode);

    bool isBrush = dynamic_cast<BrushToolBase*>(tool) != nullptr;
    bool isClone = dynamic_cast<CloneStampTool*>(tool) != nullptr;
    bool isRecolor = dynamic_cast<RecolorTool*>(tool) != nullptr;
    bool isShapeBase = dynamic_cast<ShapeToolBase*>(tool) != nullptr;
    bool isLine = dynamic_cast<LineTool*>(tool) != nullptr;
    bool isFreeform = dynamic_cast<FreeformShapeTool*>(tool) != nullptr;
    bool isText = dynamic_cast<TextTool*>(tool) != nullptr;
    bool isGradient = dynamic_cast<GradientTool*>(tool) != nullptr;

    // Brush size: brush tools, clone stamp, recolor, shape tools
    bool hasBrushSize = isBrush || isClone || isRecolor || isShapeBase || isLine || isFreeform;
    for (auto* a : m_brushSizeActions) a->setVisible(hasBrushSize);

    // Antialiased: same as brush size + text tool
    bool hasAA = hasBrushSize || isText;
    m_antialiasedAction->setVisible(hasAA);

    // Tolerance: bucket, magic wand, recolor
    bool hasTolerance = isBucket || isWand || isRecolor;
    m_toleranceSeparator->setVisible(hasTolerance);
    for (auto* a : m_toleranceActions) a->setVisible(hasTolerance);

    // Shape draw type: shape base tools + freeform
    bool isShape = isShapeBase || isFreeform;
    m_shapeSeparator->setVisible(isShape);
    for (auto* a : m_shapeActions) a->setVisible(isShape);

    // Gradient type
    m_gradientSeparator->setVisible(isGradient);
    for (auto* a : m_gradientActions) a->setVisible(isGradient);

    // Font section
    m_fontSeparator->setVisible(isText);
    for (auto* a : m_fontActions) a->setVisible(isText);

    // Blend mode: brush, pencil, bucket, gradient, shape tools, text
    bool hasBlendMode = isBrush || isBucket || isGradient || isShapeBase || isLine
                        || isFreeform || isText;
    m_blendModeSeparator->setVisible(hasBlendMode);
    for (auto* a : m_blendModeActions) a->setVisible(hasBlendMode);

    // Color picker after-click behavior
    bool isPicker = dynamic_cast<ColorPickerTool*>(tool) != nullptr;
    for (auto* a : m_pickerBehaviorActions) a->setVisible(isPicker);
}

} // namespace paintnux
