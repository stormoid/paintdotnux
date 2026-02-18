#include "ui/mainwindow.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "data/fileio.h"
#include "tools/brushtool.h"
#include "tools/colorpickertool.h"
#include "tools/paintbuckettool.h"
#include "tools/navtools.h"
#include "tools/selecttools.h"
#include "tools/magicwandtool.h"
#include "tools/movetools.h"
#include "tools/shapetools.h"
#include "tools/gradienttool.h"
#include "tools/clonestamptool.h"
#include "tools/recolortool.h"
#include "tools/texttool.h"
#include "history/bitmaphistorymemento.h"
#include "history/layerhistorymemento.h"
#include "history/selectionhistorymemento.h"
#include "history/replacedocumentmemento.h"
#include "history/flipmemento.h"
#include "adjustments/adjustments.h"
#include "effects/effects.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QRegularExpression>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStatusBar>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRadioButton>
#include <QTimer>
#include <QTransform>

#include <QMouseEvent>
#include <QStyledItemDelegate>

#include <cstring>

namespace paintnux {

/// Delegate for document tab list: fixed-height items with a close button.
class TabItemDelegate : public QStyledItemDelegate {
public:
    static constexpr int ItemHeight = 40;
    static constexpr int CloseButtonSize = 16;
    static constexpr int CloseButtonMargin = 6;

    std::function<void(int)> onCloseRequested;

    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize sz = QStyledItemDelegate::sizeHint(option, index);
        sz.setHeight(ItemHeight);
        return sz;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);

        // Draw close button (X) on the right
        QRect closeRect = closeButtonRect(option.rect);
        painter->save();
        painter->setPen(QPen(option.state & QStyle::State_Selected
                             ? option.palette.highlightedText().color()
                             : option.palette.text().color(), 1.5));
        int m = 4;
        painter->drawLine(closeRect.left() + m, closeRect.top() + m,
                          closeRect.right() - m, closeRect.bottom() - m);
        painter->drawLine(closeRect.right() - m, closeRect.top() + m,
                          closeRect.left() + m, closeRect.bottom() - m);
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (closeButtonRect(option.rect).contains(me->pos())) {
                if (onCloseRequested) onCloseRequested(index.row());
                return true;
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    static QRect closeButtonRect(const QRect& itemRect) {
        int x = itemRect.right() - CloseButtonSize - CloseButtonMargin;
        int y = itemRect.top() + (itemRect.height() - CloseButtonSize) / 2;
        return QRect(x, y, CloseButtonSize, CloseButtonSize);
    }
};

/// Compound memento that undoes two mementos as one action.
class CompoundHistoryMemento : public HistoryMemento {
public:
    CompoundHistoryMemento(const QString& name,
                           std::unique_ptr<HistoryMemento> first,
                           std::unique_ptr<HistoryMemento> second)
        : HistoryMemento(name)
        , m_first(std::move(first))
        , m_second(std::move(second)) {}

protected:
    std::unique_ptr<HistoryMemento> onUndo() override {
        // Undo in reverse order
        auto redoSecond = m_second->performUndo();
        auto redoFirst = m_first->performUndo();
        return std::make_unique<CompoundHistoryMemento>(
            name(), std::move(redoFirst), std::move(redoSecond));
    }

private:
    std::unique_ptr<HistoryMemento> m_first;
    std::unique_ptr<HistoryMemento> m_second;
};

/// Undo memento for paste: saves/restores both the selection path and the floating overlay.
class PasteHistoryMemento : public HistoryMemento {
public:
    /// Construct BEFORE the paste is applied: captures the current selection path.
    /// After construction, caller should apply the paste (set selection + overlay).
    PasteHistoryMemento(const QString& name, Selection* selection, DocumentWorkspace* workspace)
        : HistoryMemento(name)
        , m_selection(selection)
        , m_workspace(workspace)
        , m_savedSelPath(selection->path())
        , m_hadOverlay(false) {
    }

    /// Construct with explicit saved state (for redo).
    PasteHistoryMemento(const QString& name, Selection* selection, DocumentWorkspace* workspace,
                        QPainterPath savedSelPath,
                        std::unique_ptr<Surface> savedOverlay, QPoint savedOffset)
        : HistoryMemento(name)
        , m_selection(selection)
        , m_workspace(workspace)
        , m_savedSelPath(std::move(savedSelPath))
        , m_hadOverlay(savedOverlay != nullptr)
        , m_savedOverlay(std::move(savedOverlay))
        , m_savedOverlayOffset(savedOffset) {
    }

protected:
    std::unique_ptr<HistoryMemento> onUndo() override {
        // Capture current state for redo (get offset before takeOverlay resets it)
        QPainterPath currentSelPath = m_selection->path();
        QPoint currentOffset = m_workspace->overlayOffset();
        auto currentOverlay = m_workspace->takeOverlay();

        // Restore saved state
        m_selection->setPath(m_savedSelPath);
        if (m_hadOverlay && m_savedOverlay) {
            m_workspace->setOverlay(std::move(m_savedOverlay), m_savedOverlayOffset);
        }
        // (If saved state had no overlay, workspace already cleared by takeOverlay)

        // Return redo memento
        return std::make_unique<PasteHistoryMemento>(
            name(), m_selection, m_workspace,
            std::move(currentSelPath),
            std::move(currentOverlay), currentOffset);
    }

private:
    Selection* m_selection;
    DocumentWorkspace* m_workspace;
    QPainterPath m_savedSelPath;
    bool m_hadOverlay;
    std::unique_ptr<Surface> m_savedOverlay;
    QPoint m_savedOverlayOffset;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_workspace(new DocumentWorkspace(this)) {
    setWindowTitle("Paint.nux");
    resize(1100, 768);
    setAcceptDrops(true);
    setStyleSheet("QMainWindow::separator { width: 8px; height: 8px; }");

    createTools();
    createDocks();
    createMenus();
    createStatusBar();
    createCentralArea();
    restoreSettings();
    createTestDocument();
}

void MainWindow::createTools() {
    // Paint.NET tool order (paired columns in 2-wide grid)
    m_tools.push_back(std::make_unique<RectangleSelectTool>(m_workspace));  // 0
    m_tools.push_back(std::make_unique<MoveTool>(m_workspace));             // 1
    m_tools.push_back(std::make_unique<LassoSelectTool>(m_workspace));      // 2
    m_tools.push_back(std::make_unique<MoveSelectionTool>(m_workspace));    // 3
    m_tools.push_back(std::make_unique<EllipseSelectTool>(m_workspace));    // 4
    m_tools.push_back(std::make_unique<ZoomTool>(m_workspace));             // 5
    m_tools.push_back(std::make_unique<MagicWandTool>(m_workspace));        // 6
    m_tools.push_back(std::make_unique<PanTool>(m_workspace));              // 7
    m_tools.push_back(std::make_unique<PaintBucketTool>(m_workspace));      // 8
    m_tools.push_back(std::make_unique<GradientTool>(m_workspace));         // 9
    m_tools.push_back(std::make_unique<PaintBrushTool>(m_workspace));       // 10
    m_tools.push_back(std::make_unique<EraserTool>(m_workspace));           // 11
    m_tools.push_back(std::make_unique<PencilTool>(m_workspace));           // 12
    m_tools.push_back(std::make_unique<ColorPickerTool>(m_workspace));      // 13
    m_tools.push_back(std::make_unique<CloneStampTool>(m_workspace));       // 14
    m_tools.push_back(std::make_unique<RecolorTool>(m_workspace));          // 15
    m_tools.push_back(std::make_unique<TextTool>(m_workspace));             // 16
    m_tools.push_back(std::make_unique<LineTool>(m_workspace));             // 17
    m_tools.push_back(std::make_unique<RectangleTool>(m_workspace));        // 18
    m_tools.push_back(std::make_unique<RoundedRectangleTool>(m_workspace)); // 19
    m_tools.push_back(std::make_unique<EllipseTool>(m_workspace));          // 20
    m_tools.push_back(std::make_unique<FreeformShapeTool>(m_workspace));    // 21

    // Connect canvas tool events to active tool
    auto* canvas = m_workspace->canvas();
    // Helper to sync line/curve nub display
    auto syncToolNubs = [this]() {
        auto* lt = dynamic_cast<LineTool*>(m_workspace->activeTool());
        if (lt && lt->showingNubs()) {
            auto& nubs = lt->nubPositions();
            m_workspace->canvas()->setToolNubs({nubs.begin(), nubs.end()});
        } else {
            m_workspace->canvas()->setToolNubs({});
        }
    };

    connect(canvas, &CanvasWidget::toolMouseDown, this, [this, syncToolNubs](QPointF pos, Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        if (auto* tool = m_workspace->activeTool()) {
            syncToolSettings();
            tool->mouseDown(pos, btn, mods);
        }
        syncToolNubs();
        // Sync text cursor after mouse events (click starts/commits text)
        auto* tt = dynamic_cast<TextTool*>(m_workspace->activeTool());
        if (tt && tt->isEditing()) {
            m_workspace->canvas()->setTextCursor(tt->cursorDocPos(), tt->cursorHeight());
        } else {
            m_workspace->canvas()->clearTextCursor();
        }
    });
    connect(canvas, &CanvasWidget::toolMouseMove, this, [this, syncToolNubs](QPointF pos, Qt::MouseButtons btns, Qt::KeyboardModifiers mods) {
        if (auto* tool = m_workspace->activeTool())
            tool->mouseMove(pos, btns, mods);
        syncToolNubs();

        // Update cursor for selection resize handles when hovering with no buttons
        if (btns == Qt::NoButton) {
            auto* tool = m_workspace->activeTool();
            bool isSelectionTool = dynamic_cast<SelectionToolBase*>(tool)
                || dynamic_cast<LassoSelectTool*>(tool)
                || dynamic_cast<MagicWandTool*>(tool)
                || dynamic_cast<MoveTool*>(tool)
                || dynamic_cast<MoveSelectionTool*>(tool);
            if (isSelectionTool) {
                auto* sel = m_workspace->selection();
                if (sel && !sel->isEmpty()) {
                    qreal threshold = 5.0 / m_workspace->canvas()->zoomFactor();
                    auto handle = SelectionResizeHelper::hitTest(
                        sel->path().boundingRect(), pos, threshold);
                    if (handle != SelectionHandle::None) {
                        m_workspace->canvas()->setCursor(
                            SelectionResizeHelper::cursorForHandle(handle));
                        return;
                    }
                }
                // Restore tool cursor when not hovering a handle
                m_workspace->canvas()->setCursor(tool->cursor());
            }
        }
    });
    connect(canvas, &CanvasWidget::toolMouseUp, this, [this, syncToolNubs](QPointF pos, Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        if (auto* tool = m_workspace->activeTool())
            tool->mouseUp(pos, btn, mods);
        syncToolNubs();
    });
    auto syncTextCursor = [this]() {
        auto* tt = dynamic_cast<TextTool*>(m_workspace->activeTool());
        if (tt && tt->isEditing()) {
            m_workspace->canvas()->setTextCursor(tt->cursorDocPos(), tt->cursorHeight());
        } else {
            m_workspace->canvas()->clearTextCursor();
        }
    };

    connect(canvas, &CanvasWidget::toolKeyDown, this, [this, syncToolNubs, syncTextCursor](QKeyEvent* event) {
        if (auto* tool = m_workspace->activeTool())
            tool->keyDown(event);
        syncToolNubs();
        syncTextCursor();
    });
    connect(canvas, &CanvasWidget::toolKeyUp, this, [this](QKeyEvent* event) {
        if (auto* tool = m_workspace->activeTool())
            tool->keyUp(event);
    });

    // Connect color picker signals
    for (auto& tool : m_tools) {
        auto* picker = dynamic_cast<ColorPickerTool*>(tool.get());
        if (picker) {
            connect(picker, &ColorPickerTool::colorPicked, this, [this](ColorBgra color, bool primary) {
                if (primary)
                    m_colorsDock->setPrimaryColor(color);
                else
                    m_colorsDock->setSecondaryColor(color);
            });
            connect(picker, &ColorPickerTool::requestToolSwitch, this, [this](Tool* target) {
                if (!target) {
                    // nullptr means switch to pencil
                    for (int i = 0; i < static_cast<int>(m_tools.size()); ++i) {
                        if (dynamic_cast<PencilTool*>(m_tools[i].get())) {
                            m_toolsDock->selectTool(i);
                            return;
                        }
                    }
                } else {
                    for (int i = 0; i < static_cast<int>(m_tools.size()); ++i) {
                        if (m_tools[i].get() == target) {
                            m_toolsDock->selectTool(i);
                            return;
                        }
                    }
                }
            });
            break;
        }
    }

    // Connect text tool signals for cursor overlay
    for (auto& tool : m_tools) {
        auto* textTool = dynamic_cast<TextTool*>(tool.get());
        if (textTool) {
            connect(textTool, &TextTool::editingChanged, this, [this, textTool](bool editing) {
                if (editing) {
                    syncToolSettings();
                    m_workspace->canvas()->setTextCursor(
                        textTool->cursorDocPos(), textTool->cursorHeight());
                } else {
                    m_workspace->canvas()->clearTextCursor();
                }
            });
            break;
        }
    }

    // Set initial tool to paintbrush
    for (int i = 0; i < static_cast<int>(m_tools.size()); ++i) {
        if (dynamic_cast<PaintBrushTool*>(m_tools[i].get())) {
            m_workspace->setActiveTool(m_tools[i].get());
            break;
        }
    }
}

void MainWindow::createMenus() {
    // --- File menu ---
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New..."), QKeySequence::New, this, &MainWindow::onFileNew);
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &MainWindow::onFileOpen);
    fileMenu->addSeparator();
    m_saveAction = fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::onFileSave);
    m_saveAction->setStatusTip(tr("Save using current format settings"));
    auto* saveAsAction = fileMenu->addAction(tr("Save &As..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, &MainWindow::onFileSaveAs);
    saveAsAction->setStatusTip(tr("Save to a new file using default format settings"));
    fileMenu->addSeparator();
    auto* exportAction = fileMenu->addAction(tr("E&xport (w/ Options)..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this, &MainWindow::onFileExport);
    exportAction->setStatusTip(tr("Export with format-specific quality and compression settings"));
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Print..."), QKeySequence::Print, this, &MainWindow::onPrint);
    fileMenu->addAction(tr("Print Pre&view..."), this, &MainWindow::onPrintPreview);
    fileMenu->addSeparator();
    m_recentFilesMenu = fileMenu->addMenu(tr("&Recent Files"));
    QSettings settings("paintnux", "paintnux");
    m_recentFiles = settings.value("recentFiles").toStringList();
    updateRecentFilesMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Close"), QKeySequence::Close, this, &MainWindow::onFileClose);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), QKeySequence::Quit, qApp, &QApplication::quit);

    // --- Edit menu ---
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    m_undoAction = editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, &MainWindow::onUndo);
    m_redoAction = editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, &MainWindow::onRedo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);

    editMenu->addSeparator();
    editMenu->addAction(tr("Cu&t"), QKeySequence::Cut, this, &MainWindow::onCut);
    editMenu->addAction(tr("&Copy"), QKeySequence::Copy, this, &MainWindow::onCopy);
    editMenu->addAction(tr("&Paste"), QKeySequence::Paste, this, &MainWindow::onPaste);
    editMenu->addAction(tr("&Delete"), QKeySequence::Delete, this, &MainWindow::onDelete);
    editMenu->addAction(tr("&Fill Selection"), QKeySequence(Qt::Key_Backspace), this, &MainWindow::onFillSelection);

    editMenu->addSeparator();
    editMenu->addAction(tr("Select &All"), QKeySequence::SelectAll, this, &MainWindow::onSelectAll);
    auto* deselectAction = editMenu->addAction(tr("&Deselect"), QKeySequence(Qt::CTRL | Qt::Key_D), this, &MainWindow::onDeselect);
    deselectAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_D), QKeySequence(Qt::Key_Escape)});
    editMenu->addAction(tr("&Invert Selection"), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::onInvertSelection);

    // --- View menu ---
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Zoom &In"), QKeySequence(Qt::CTRL | Qt::Key_Plus), m_workspace->canvas(), &CanvasWidget::zoomIn);
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), m_workspace->canvas(), &CanvasWidget::zoomOut);
    viewMenu->addAction(tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_0), m_workspace->canvas(), &CanvasWidget::zoomToActualSize);
    viewMenu->addAction(tr("&Fit to Window"), QKeySequence(Qt::CTRL | Qt::Key_F), m_workspace->canvas(), &CanvasWidget::zoomToFit);
    viewMenu->addSeparator();
    m_zoomToSelAction = viewMenu->addAction(tr("Zoom to &Selection"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), this, [this]() {
        auto* sel = m_workspace->selection();
        if (sel && !sel->isEmpty()) {
            m_workspace->canvas()->zoomToSelection(sel->path());
        }
    });
    m_zoomToSelAction->setEnabled(false);
    viewMenu->addSeparator();
    m_pixelGridAction = viewMenu->addAction(tr("&Pixel Grid"));
    m_pixelGridAction->setCheckable(true);
    m_pixelGridAction->setChecked(false);
    connect(m_pixelGridAction, &QAction::toggled, m_workspace->canvas(), &CanvasWidget::setPixelGrid);

    // Channel view submenu
    {
        auto* channelMenu = viewMenu->addMenu(tr("&Channel View"));
        auto* channelGroup = new QActionGroup(this);

        struct ChannelEntry { const char* label; ChannelView view; };
        const ChannelEntry entries[] = {
            {"&All Channels", ChannelView::All},
            {"&Red",          ChannelView::Red},
            {"&Green",        ChannelView::Green},
            {"&Blue",         ChannelView::Blue},
            {"Al&pha",        ChannelView::Alpha},
        };
        for (const auto& e : entries) {
            auto* action = channelMenu->addAction(tr(e.label));
            action->setCheckable(true);
            channelGroup->addAction(action);
            if (e.view == ChannelView::All) action->setChecked(true);
            connect(action, &QAction::triggered, this, [this, v = e.view]() {
                m_workspace->canvas()->setChannelView(v);
            });
        }

        connect(m_workspace->canvas(), &CanvasWidget::channelViewChanged, this, [this](ChannelView view) {
            if (view == ChannelView::All) {
                m_channelLabel->hide();
            } else {
                static const QStringList names = {QString(), tr("Red"), tr("Green"), tr("Blue"), tr("Alpha")};
                m_channelLabel->setText(tr("Channel: %1").arg(names[static_cast<int>(view)]));
                m_channelLabel->show();
            }
        });
    }

    viewMenu->addSeparator();
    m_rulersAction = viewMenu->addAction(tr("&Rulers"));
    m_rulersAction->setCheckable(true);
    m_rulersAction->setChecked(false);
    connect(m_rulersAction, &QAction::toggled, this, [this](bool show) {
        if (m_hRuler) m_hRuler->setVisible(show);
        if (m_vRuler) m_vRuler->setVisible(show);
        if (m_rulerCorner) m_rulerCorner->setVisible(show);
    });
    viewMenu->addSeparator();
    auto* unitsMenu = viewMenu->addMenu(tr("&Units"));
    auto* unitsGroup = new QActionGroup(this);
    m_unitsPixelsAction = unitsMenu->addAction(tr("&Pixels"));
    m_unitsPixelsAction->setCheckable(true);
    m_unitsPixelsAction->setChecked(true);
    unitsGroup->addAction(m_unitsPixelsAction);
    m_unitsInchesAction = unitsMenu->addAction(tr("&Inches"));
    m_unitsInchesAction->setCheckable(true);
    unitsGroup->addAction(m_unitsInchesAction);
    m_unitsCmAction = unitsMenu->addAction(tr("&Centimeters"));
    m_unitsCmAction->setCheckable(true);
    unitsGroup->addAction(m_unitsCmAction);
    connect(m_unitsPixelsAction, &QAction::triggered, this, [this]() {
        if (m_hRuler) m_hRuler->setUnits(RulerUnits::Pixels);
        if (m_vRuler) m_vRuler->setUnits(RulerUnits::Pixels);
    });
    connect(m_unitsInchesAction, &QAction::triggered, this, [this]() {
        if (m_hRuler) m_hRuler->setUnits(RulerUnits::Inches);
        if (m_vRuler) m_vRuler->setUnits(RulerUnits::Inches);
    });
    connect(m_unitsCmAction, &QAction::triggered, this, [this]() {
        if (m_hRuler) m_hRuler->setUnits(RulerUnits::Centimeters);
        if (m_vRuler) m_vRuler->setUnits(RulerUnits::Centimeters);
    });
    viewMenu->addSeparator();
    // Dock visibility toggles (uses QDockWidget::toggleViewAction for auto check state)
    viewMenu->addAction(m_toolsDock->toggleViewAction());
    viewMenu->addAction(m_historyDock->toggleViewAction());
    viewMenu->addAction(m_layersDock->toggleViewAction());
    viewMenu->addAction(m_colorsDock->toggleViewAction());

    // --- Image menu ---
    auto* imageMenu = menuBar()->addMenu(tr("&Image"));
    imageMenu->addAction(tr("&Resize..."), QKeySequence(Qt::CTRL | Qt::Key_R), this, &MainWindow::onResize);
    imageMenu->addAction(tr("Canvas Si&ze..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, &MainWindow::onCanvasSize);
    m_cropAction = imageMenu->addAction(tr("Crop to &Selection"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), this, &MainWindow::onCropToSelection);
    m_cropAction->setEnabled(false);
    imageMenu->addSeparator();
    imageMenu->addAction(tr("Flip &Horizontal"), this, &MainWindow::onFlipHorizontal);
    imageMenu->addAction(tr("Flip &Vertical"), this, &MainWindow::onFlipVertical);
    imageMenu->addSeparator();
    imageMenu->addAction(tr("Rotate 90\u00B0 C&W"), QKeySequence(Qt::CTRL | Qt::Key_H), this, &MainWindow::onRotate90CW);
    imageMenu->addAction(tr("Rotate 90\u00B0 CC&W"), QKeySequence(Qt::CTRL | Qt::Key_G), this, &MainWindow::onRotate90CCW);
    imageMenu->addAction(tr("Rotate 1&80\u00B0"), QKeySequence(Qt::CTRL | Qt::Key_J), this, &MainWindow::onRotate180);
    imageMenu->addSeparator();
    m_flattenAction = imageMenu->addAction(tr("&Flatten Image"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this, &MainWindow::onFlattenImage);
    m_flattenAction->setEnabled(false);

    // --- Layers menu ---
    auto* layersMenu = menuBar()->addMenu(tr("&Layers"));
    layersMenu->addAction(tr("Add &New Layer"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
                          this, [this]() { m_layersDock->addLayer(); });
    layersMenu->addAction(tr("&Delete Layer"), this, [this]() { m_layersDock->deleteLayer(); });
    layersMenu->addAction(tr("D&uplicate Layer"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
                          this, [this]() { m_layersDock->duplicateLayer(); });
    layersMenu->addAction(tr("&Merge Layer Down"), QKeySequence(Qt::CTRL | Qt::Key_M),
                          this, [this]() { m_layersDock->mergeLayerDown(); });
    layersMenu->addSeparator();
    layersMenu->addAction(tr("&Import From File..."), this, &MainWindow::onImportFromFile);
    layersMenu->addSeparator();
    layersMenu->addAction(tr("&Rotate / Zoom..."), this, &MainWindow::onRotateZoom);
    layersMenu->addAction(tr("Flip Layer &Horizontal"), this, &MainWindow::onLayerFlipHorizontal);
    layersMenu->addAction(tr("Flip Layer &Vertical"), this, &MainWindow::onLayerFlipVertical);
    // Remap Channels submenu
    {
        auto* remapMenu = layersMenu->addMenu(tr("&Remap Channels"));
        const QStringList channelNames = {tr("Red"), tr("Green"), tr("Blue"), tr("Alpha")};
        for (int src = 0; src < 4; ++src) {
            auto* srcMenu = remapMenu->addMenu(channelNames[src]);
            for (int dst = 0; dst < 4; ++dst) {
                if (dst == src) continue;
                srcMenu->addAction(tr("Copy to %1").arg(channelNames[dst]),
                    this, [this, src, dst]() { onRemapChannel(src, dst, false); });
            }
            srcMenu->addSeparator();
            for (int dst = 0; dst < 4; ++dst) {
                if (dst == src) continue;
                srcMenu->addAction(tr("Move to %1").arg(channelNames[dst]),
                    this, [this, src, dst]() { onRemapChannel(src, dst, true); });
            }
        }
    }
    layersMenu->addSeparator();
    layersMenu->addAction(tr("Layer &Properties..."))->setEnabled(false);

    // --- Adjustments menu ---
    auto* adjustMenu = menuBar()->addMenu(tr("&Adjustments"));
    adjustMenu->addAction(tr("&Invert Colors"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I),
                          this, &MainWindow::onInvertColors);
    adjustMenu->addAction(tr("&Grayscale"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U),
                          this, &MainWindow::onGrayscale);
    adjustMenu->addAction(tr("S&epia"), this, &MainWindow::onSepia);
    adjustMenu->addAction(tr("Auto-&Level"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L),
                          this, &MainWindow::onAutoLevel);
    adjustMenu->addSeparator();
    adjustMenu->addAction(tr("&Brightness/Contrast..."), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B),
                          this, &MainWindow::onBrightnessContrast);
    adjustMenu->addAction(tr("&Hue/Saturation..."), QKeySequence(Qt::CTRL | Qt::Key_U),
                          this, &MainWindow::onHueSaturation);
    adjustMenu->addAction(tr("&Posterize..."), this, &MainWindow::onPosterize);
    adjustMenu->addSeparator();
    adjustMenu->addAction(tr("Le&vels..."), QKeySequence(Qt::CTRL | Qt::Key_L),
                          this, &MainWindow::onLevels);

    // --- Effects menu ---
    auto* effectsMenu = menuBar()->addMenu(tr("Effe&cts"));

    auto* artisticMenu = effectsMenu->addMenu(tr("&Artistic"));
    artisticMenu->addAction(tr("&Ink Sketch..."), this, &MainWindow::onInkSketch);
    artisticMenu->addAction(tr("&Oil Painting..."), this, &MainWindow::onOilPainting);
    artisticMenu->addAction(tr("&Pencil Sketch..."), this, &MainWindow::onPencilSketch);

    auto* blursMenu = effectsMenu->addMenu(tr("&Blurs"));
    blursMenu->addAction(tr("&Gaussian Blur..."), this, &MainWindow::onGaussianBlur);
    blursMenu->addAction(tr("&Motion Blur..."), this, &MainWindow::onMotionBlur);
    blursMenu->addAction(tr("&Unfocus..."), this, &MainWindow::onUnfocus);

    auto* distortMenu = effectsMenu->addMenu(tr("&Distort"));
    distortMenu->addAction(tr("&Pixelate..."), this, &MainWindow::onPixelate);

    auto* noiseMenu = effectsMenu->addMenu(tr("&Noise"));
    noiseMenu->addAction(tr("&Add Noise..."), this, &MainWindow::onAddNoise);
    noiseMenu->addAction(tr("&Median..."), this, &MainWindow::onMedian);

    auto* photoMenu = effectsMenu->addMenu(tr("&Photo"));
    photoMenu->addAction(tr("&Glow..."), this, &MainWindow::onGlow);
    photoMenu->addAction(tr("&Sharpen..."), this, &MainWindow::onSharpen);

    auto* stylizeMenu = effectsMenu->addMenu(tr("&Stylize"));
    stylizeMenu->addAction(tr("&Edge Detect..."), this, &MainWindow::onEdgeDetect);
    stylizeMenu->addAction(tr("E&mboss..."), this, &MainWindow::onEmboss);
    stylizeMenu->addAction(tr("&Outline..."), this, &MainWindow::onOutline);
    stylizeMenu->addAction(tr("&Relief..."), this, &MainWindow::onRelief);

    // --- Help menu ---
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Paint.nux"), [this]() {
        QMessageBox::about(this, tr("About Paint.nux"),
            tr("<h3>Paint.nux 0.1.0</h3>"
               "<p>A reimplementation of Paint.NET 3.36, built with C++20 and Qt6.</p>"));
    });
}

void MainWindow::createStatusBar() {
    m_posLabel = new QLabel(tr("Pos: 0, 0"));
    m_posLabel->setMinimumWidth(140);

    m_sizeLabel = new QLabel;
    m_sizeLabel->setMinimumWidth(140);

    m_zoomLabel = new QLabel(tr("100%"));
    m_zoomLabel->setMinimumWidth(80);
    m_zoomLabel->setCursor(Qt::PointingHandCursor);
    m_zoomLabel->installEventFilter(this);

    m_channelLabel = new QLabel;
    m_channelLabel->setMinimumWidth(100);
    m_channelLabel->hide(); // hidden when viewing all channels

    statusBar()->addWidget(m_posLabel);
    statusBar()->addWidget(m_sizeLabel);
    statusBar()->addPermanentWidget(m_channelLabel);
    statusBar()->addPermanentWidget(m_zoomLabel);
}

void MainWindow::createCentralArea() {
    auto* centralWidget = new QWidget;
    auto* grid = new QGridLayout(centralWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);

    CanvasWidget* canvas = m_workspace->canvas();
    m_hScrollBar = new QScrollBar(Qt::Horizontal);
    m_vScrollBar = new QScrollBar(Qt::Vertical);

    m_hRuler = new RulerWidget(RulerWidget::Horizontal);
    m_vRuler = new RulerWidget(RulerWidget::Vertical);
    m_hRuler->setVisible(false);
    m_vRuler->setVisible(false);

    // Row 0: corner spacer + horizontal ruler
    m_rulerCorner = new QWidget;
    m_rulerCorner->setFixedSize(RulerWidget::RulerThickness, RulerWidget::RulerThickness);
    m_rulerCorner->setVisible(false);
    grid->addWidget(m_rulerCorner, 0, 0);
    grid->addWidget(m_hRuler, 0, 1);

    // Row 1: vertical ruler + canvas + vertical scrollbar
    grid->addWidget(m_vRuler, 1, 0);
    grid->addWidget(canvas, 1, 1);
    grid->addWidget(m_vScrollBar, 1, 2);

    // Row 2: horizontal scrollbar
    grid->addWidget(m_hScrollBar, 2, 1);

    canvas->setScrollBars(m_hScrollBar, m_vScrollBar);
    setCentralWidget(centralWidget);

    connect(canvas, &CanvasWidget::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(canvas, &CanvasWidget::cursorDocumentPosition, this, &MainWindow::onCursorPosition);

    // Sync rulers with canvas zoom/scroll/cursor
    connect(canvas, &CanvasWidget::zoomChanged, this, [this](double zoom) {
        m_hRuler->setZoom(zoom);
        m_vRuler->setZoom(zoom);
    });
    connect(canvas, &CanvasWidget::scrollPositionChanged, this, [this](QPointF pos) {
        m_hRuler->setOffset(pos.x());
        m_vRuler->setOffset(pos.y());
    });
    connect(canvas, &CanvasWidget::cursorDocumentPosition, this, [this](QPointF docPos) {
        m_hRuler->setCursorPos(docPos.x());
        m_vRuler->setCursorPos(docPos.y());
    });
}

void MainWindow::createDocks() {
    // Tool options bar (top)
    m_toolOptionsBar = new ToolOptionsBar(this);
    addToolBar(Qt::TopToolBarArea, m_toolOptionsBar);

    connect(m_toolOptionsBar, &ToolOptionsBar::settingsChanged, this, &MainWindow::syncToolSettings);

    // Tools dock (left)
    m_toolsDock = new ToolsDock(this);
    // Unicode symbols as temporary icons
    struct ToolEntry { QString icon; char shortcut; };
    const ToolEntry entries[] = {
        {QStringLiteral("\u2B1A"),     's'},  //  0 rect select
        {QStringLiteral("\u271C"),     'm'},  //  1 move pixels
        {QStringLiteral("\u27B0"),     's'},  //  2 lasso select
        {QStringLiteral("\u26F6"),     'm'},  //  3 move selection
        {QStringLiteral("\u25CC"),     's'},  //  4 ellipse select
        {QStringLiteral("\U0001F50D"), 'z'},  //  5 zoom
        {QStringLiteral("\u2728"),     's'},  //  6 magic wand
        {QStringLiteral("\u270B"),     'h'},  //  7 pan
        {QStringLiteral("\U0001FAA3"), 'f'},  //  8 paint bucket
        {QStringLiteral("\u25E7"),     'g'},  //  9 gradient
        {QStringLiteral("\U0001F58C"), 'b'},  // 10 paintbrush
        {QStringLiteral("\u232B"),     'e'},  // 11 eraser
        {QStringLiteral("\u270F"),     'p'},  // 12 pencil
        {QStringLiteral("\U0001F4A7"), 'k'},  // 13 color picker
        {QStringLiteral("\u29C9"),     'l'},  // 14 clone stamp
        {QStringLiteral("\U0001F504"), 'r'},  // 15 recolor
        {QStringLiteral("\U0001D54B"), 't'},  // 16 text
        {QStringLiteral("\u2312"),     'o'},  // 17 line / curve
        {QStringLiteral("\u25A1"),     'o'},  // 18 rectangle
        {QStringLiteral("\u25A2"),     'o'},  // 19 rounded rectangle
        {QStringLiteral("\u2B2D"),     'o'},  // 20 ellipse shape
        {QStringLiteral("\u51F9"),     'o'},  // 21 freeform shape
    };
    for (int i = 0; i < static_cast<int>(m_tools.size()); ++i) {
        m_toolsDock->addTool(m_tools[i].get(), entries[i].icon, entries[i].shortcut);
    }
    m_toolsDock->setMaximumWidth(180);
    addDockWidget(Qt::LeftDockWidgetArea, m_toolsDock);

    connect(m_toolsDock, &ToolsDock::toolSelected, this, &MainWindow::onToolSelected);

    // Colors dock (left, below tools)
    m_colorsDock = new ColorsDock(this);
    m_colorsDock->setMaximumWidth(180);
    addDockWidget(Qt::LeftDockWidgetArea, m_colorsDock);

    connect(m_colorsDock, &ColorsDock::primaryColorChanged, this, [this](ColorBgra) {
        syncToolSettings();
    });
    connect(m_colorsDock, &ColorsDock::secondaryColorChanged, this, [this](ColorBgra) {
        syncToolSettings();
    });

    // Tabs dock (right, above history)
    m_tabsDock = new QDockWidget(tr("Documents"), this);
    m_tabsDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_tabList = new QListWidget;
    m_tabList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tabList->setDragDropMode(QAbstractItemView::NoDragDrop);
    m_tabList->setIconSize(QSize(48, 32));
    auto* tabDelegate = new TabItemDelegate(m_tabList);
    tabDelegate->onCloseRequested = [this](int index) { onTabCloseRequested(index); };
    m_tabList->setItemDelegate(tabDelegate);
    m_tabsDock->setWidget(m_tabList);
    m_tabsDock->setMinimumHeight(200);
    m_tabsDock->setMinimumWidth(140);
    m_tabsDock->setMaximumWidth(350);
    addDockWidget(Qt::RightDockWidgetArea, m_tabsDock);

    connect(m_tabList, &QListWidget::currentRowChanged, this, &MainWindow::onTabChanged);

    // Debounced thumbnail refresh for active tab
    m_tabThumbTimer = new QTimer(this);
    m_tabThumbTimer->setSingleShot(true);
    m_tabThumbTimer->setInterval(250);
    connect(m_tabThumbTimer, &QTimer::timeout, this, &MainWindow::updateActiveTabThumbnail);

    connect(m_workspace, &DocumentWorkspace::compositionUpdated, this, [this]() {
        if (!m_tabThumbTimer->isActive())
            m_tabThumbTimer->start();
    });

    // History dock (right, below tabs)
    m_historyDock = new HistoryDock(this);
    m_historyDock->setMaximumWidth(350);
    addDockWidget(Qt::RightDockWidgetArea, m_historyDock);

    // Layers dock (right, below history)
    m_layersDock = new LayersDock(this);
    m_layersDock->setWorkspace(m_workspace);
    m_layersDock->setMaximumWidth(350);
    addDockWidget(Qt::RightDockWidgetArea, m_layersDock);

    // Distribute right dock area evenly
    resizeDocks({m_tabsDock, m_historyDock, m_layersDock},
                {200, 200, 200}, Qt::Vertical);
    resizeDocks({m_tabsDock}, {180}, Qt::Horizontal);

    // Update history dock when workspace document changes
    connect(m_workspace, &DocumentWorkspace::documentChanged, this, [this]() {
        auto* stack = m_workspace->historyStack();
        m_historyDock->setHistoryStack(stack);
        updateImageMenuState();
        if (stack) {
            connect(stack, &HistoryStack::changed, this, [this]() {
                m_undoAction->setEnabled(m_workspace->historyStack()->canUndo());
                m_redoAction->setEnabled(m_workspace->historyStack()->canRedo());
                setDocumentDirty();
                updateImageMenuState();
                // Update size label in case document dimensions changed
                if (auto* doc = m_workspace->document())
                    m_sizeLabel->setText(tr("Size: %1 x %2").arg(doc->width()).arg(doc->height()));
            });
        }
    });
}

void MainWindow::createTestDocument() {
    auto doc = std::make_unique<Document>(800, 600);
    doc->addLayer(BitmapLayer::createBackground(800, 600));
    setNewDocument(std::move(doc), QString());
}

void MainWindow::addRecentFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    m_recentFiles.removeAll(filePath);
    m_recentFiles.prepend(filePath);
    while (m_recentFiles.size() > MaxRecentFiles)
        m_recentFiles.removeLast();

    QSettings settings("paintnux", "paintnux");
    settings.setValue("recentFiles", m_recentFiles);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    m_recentFilesMenu->clear();
    if (m_recentFiles.isEmpty()) {
        m_recentFilesMenu->addAction(tr("(No recent files)"))->setEnabled(false);
        return;
    }
    for (int i = 0; i < m_recentFiles.size(); ++i) {
        QString path = m_recentFiles.at(i);
        QString label = tr("&%1  %2").arg(i + 1).arg(QFileInfo(path).fileName());
        m_recentFilesMenu->addAction(label, this, [this, path]() {
            openRecentFile(path);
        });
    }
    m_recentFilesMenu->addSeparator();
    m_recentFilesMenu->addAction(tr("Clear Recent Files"), this, [this]() {
        m_recentFiles.clear();
        QSettings settings("paintnux", "paintnux");
        settings.setValue("recentFiles", m_recentFiles);
        updateRecentFilesMenu();
    });
}

void MainWindow::openRecentFile(const QString& filePath) {
    if (!QFileInfo::exists(filePath)) {
        QMessageBox::warning(this, tr("File Not Found"),
                             tr("The file \"%1\" no longer exists.").arg(filePath));
        m_recentFiles.removeAll(filePath);
        QSettings settings("paintnux", "paintnux");
        settings.setValue("recentFiles", m_recentFiles);
        updateRecentFilesMenu();
        return;
    }

    QString error;
    auto doc = loadDocument(filePath, &error);
    if (!doc) {
        QMessageBox::critical(this, tr("Open Failed"), error);
        return;
    }
    setNewDocument(std::move(doc), filePath);
}

void MainWindow::setNewDocument(std::unique_ptr<Document> doc, const QString& filePath) {
    addNewTab(std::move(doc), filePath);
}

void MainWindow::addNewTab(std::unique_ptr<Document> doc, const QString& filePath) {
    int w = doc->width();
    int h = doc->height();

    // Save current tab state before switching
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        auto& cur = m_tabs[m_activeTab];
        cur->state = m_workspace->takeState();
        cur->filePath = m_currentFilePath;
        cur->savedHistoryIndex = m_savedHistoryIndex;
        cur->dirty = m_dirty;
        cur->zoom = m_workspace->canvas()->zoomFactor();
        cur->scrollPos = m_workspace->canvas()->scrollPosition();
    }

    // Set the new document into workspace
    m_workspace->setDocument(std::move(doc));
    m_workspace->setActiveLayerIndex(0);
    m_workspace->canvas()->zoomToFit();

    m_sizeLabel->setText(tr("Size: %1 x %2").arg(w).arg(h));

    m_currentFilePath = filePath;
    m_savedHistoryIndex = 0;
    m_dirty = false;

    // Create tab session (state is empty since workspace owns it now)
    auto session = std::make_unique<TabSession>();
    session->filePath = filePath;
    session->savedHistoryIndex = 0;
    session->dirty = false;

    // Add tab to list
    QString label = filePath.isEmpty() ? tr("Untitled") : QFileInfo(filePath).fileName();
    m_switchingTabs = true;
    m_tabList->addItem(label);
    int idx = m_tabList->count() - 1;
    m_tabs.push_back(std::move(session));
    m_activeTab = idx;
    m_tabList->setCurrentRow(idx);
    m_switchingTabs = false;

    updateWindowTitle();
    syncToolSettings();
    addRecentFile(filePath);
    updateActiveTabThumbnail();
}

void MainWindow::onTabChanged(int index) {
    if (m_switchingTabs) return;
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;
    if (index == m_activeTab) return;

    m_switchingTabs = true;

    // Save current tab state
    if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size())) {
        auto& cur = m_tabs[m_activeTab];
        cur->state = m_workspace->takeState();
        cur->filePath = m_currentFilePath;
        cur->savedHistoryIndex = m_savedHistoryIndex;
        cur->dirty = m_dirty;
        cur->zoom = m_workspace->canvas()->zoomFactor();
        cur->scrollPos = m_workspace->canvas()->scrollPosition();
    }

    // Restore new tab state
    auto& tab = m_tabs[index];
    m_currentFilePath = tab->filePath;
    m_savedHistoryIndex = tab->savedHistoryIndex;
    m_dirty = tab->dirty;

    m_workspace->restoreState(std::move(tab->state));

    // Restore canvas view
    m_workspace->canvas()->setZoomFactor(tab->zoom);
    m_workspace->canvas()->setScrollPosition(tab->scrollPos);

    if (auto* doc = m_workspace->document())
        m_sizeLabel->setText(tr("Size: %1 x %2").arg(doc->width()).arg(doc->height()));

    m_activeTab = index;
    m_switchingTabs = false;

    updateWindowTitle();
    syncToolSettings();
}

void MainWindow::onTabCloseRequested(int index) {
    if (m_tabs.size() <= 1) {
        // Last tab - behave like close window
        close();
        return;
    }

    // If closing current tab, check for unsaved changes
    if (index == m_activeTab) {
        if (!maybeSave()) return;

        m_switchingTabs = true;

        // Remove tab state
        m_tabs.erase(m_tabs.begin() + index);
        delete m_tabList->takeItem(index);

        // Switch to adjacent tab
        int newIdx = qMin(index, static_cast<int>(m_tabs.size()) - 1);
        m_activeTab = -1; // Reset so onTabChanged will process
        m_switchingTabs = false;

        m_tabList->setCurrentRow(newIdx);
        // If currentIndex didn't change (already was newIdx), trigger manually
        if (m_activeTab < 0) {
            onTabChanged(newIdx);
        }
    } else {
        // Closing a non-active tab - check if it's dirty
        auto& tab = m_tabs[index];
        if (tab->dirty) {
            QString name = tab->filePath.isEmpty() ? tr("Untitled") : QFileInfo(tab->filePath).fileName();
            auto btn = QMessageBox::question(this, tr("Unsaved Changes"),
                tr("Save changes to \"%1\"?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (btn == QMessageBox::Cancel) return;
            if (btn == QMessageBox::Save) {
                // Switch to that tab to save, then close it
                m_tabList->setCurrentRow(index);
                if (!maybeSave()) return;
            }
        }

        m_switchingTabs = true;
        m_tabs.erase(m_tabs.begin() + index);
        delete m_tabList->takeItem(index);

        // Adjust active tab index if needed
        if (m_activeTab > index) {
            m_activeTab--;
        }
        m_switchingTabs = false;
    }
}

void MainWindow::updateTabLabel(int index) {
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) return;

    QString filePath;
    bool dirty;

    if (index == m_activeTab) {
        filePath = m_currentFilePath;
        dirty = m_dirty;
    } else {
        filePath = m_tabs[index]->filePath;
        dirty = m_tabs[index]->dirty;
    }

    QString name = filePath.isEmpty() ? tr("Untitled") : QFileInfo(filePath).fileName();
    if (dirty) name += QStringLiteral("*");
    if (auto* item = m_tabList->item(index))
        item->setText(name);
}

QPixmap MainWindow::generateThumbnail(const Surface* surface) const {
    if (!surface) return {};
    static constexpr int ThumbH = 32;
    const QImage& src = surface->qimage();
    QImage scaled = src.scaled(src.width() * ThumbH / src.height(), ThumbH,
                               Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(scaled);
}

void MainWindow::updateActiveTabThumbnail() {
    if (m_activeTab < 0 || m_activeTab >= static_cast<int>(m_tabs.size())) return;

    QPixmap thumb = generateThumbnail(m_workspace->compositeSurface());
    if (auto* item = m_tabList->item(m_activeTab))
        item->setIcon(QIcon(thumb));
}

void MainWindow::onZoomChanged(double zoom) {
    m_zoomLabel->setText(QString("%1%").arg(zoom * 100.0, 0, 'f', zoom < 0.1 ? 1 : 0));
}

void MainWindow::onCursorPosition(QPointF docPos) {
    int x = static_cast<int>(docPos.x());
    int y = static_cast<int>(docPos.y());
    m_posLabel->setText(tr("Pos: %1, %2").arg(x).arg(y));
}

void MainWindow::onToolSelected(Tool* tool) {
    syncToolSettings();
    m_workspace->setActiveTool(tool);
    // Clear nubs when switching away from line/curve tool
    m_workspace->canvas()->setToolNubs({});
    // Show/hide font controls based on tool type
    m_toolOptionsBar->showForTool(tool);
}

void MainWindow::syncToolSettings() {
    auto* tool = m_workspace->activeTool();
    if (!tool) return;
    m_toolOptionsBar->applyTo(tool->settingsRef());
    tool->settingsRef().primaryColor = m_colorsDock->primaryColor();
    tool->settingsRef().secondaryColor = m_colorsDock->secondaryColor();
}

// --- Dirty tracking / title ---

void MainWindow::updateWindowTitle() {
    QString name = m_currentFilePath.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentFilePath).fileName();
    QString dirty = m_dirty ? QStringLiteral("*") : QString();
    setWindowTitle(QStringLiteral("%1%2 \u2014 Paint.nux").arg(name, dirty));
    updateTabLabel(m_activeTab);
}

void MainWindow::setDocumentDirty() {
    // Compare current history position to saved position
    auto* stack = m_workspace->historyStack();
    int currentIndex = stack ? stack->undoCount() : 0;
    bool wasDirty = m_dirty;
    m_dirty = (currentIndex != m_savedHistoryIndex);
    if (m_dirty != wasDirty)
        updateWindowTitle();
}

void MainWindow::clearDirty() {
    auto* stack = m_workspace->historyStack();
    m_savedHistoryIndex = stack ? stack->undoCount() : 0;
    m_dirty = false;
    updateWindowTitle();
}

bool MainWindow::maybeSave() {
    if (!m_dirty) return true;

    QString name = m_currentFilePath.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentFilePath).fileName();

    QMessageBox msgBox(QMessageBox::Warning, tr("Unsaved Changes"),
        tr("Do you want to save changes to \"%1\"?").arg(name), QMessageBox::NoButton, this);
    auto* saveBtn = msgBox.addButton(tr("&Save"), QMessageBox::AcceptRole);
    auto* discardBtn = msgBox.addButton(tr("Close &Without Saving"), QMessageBox::DestructiveRole);
    auto* cancelBtn = msgBox.addButton(tr("Cancel"), QMessageBox::RejectRole);
    msgBox.setDefaultButton(saveBtn);
    msgBox.setEscapeButton(cancelBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == saveBtn) {
        onFileSave();
        return !m_dirty; // false if save was cancelled
    }
    if (msgBox.clickedButton() == discardBtn) {
        return true;
    }
    return false; // Cancel
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Check current tab first
    if (m_dirty) {
        if (!maybeSave()) {
            event->ignore();
            return;
        }
    }

    // Check all other tabs for unsaved changes
    for (int i = 0; i < static_cast<int>(m_tabs.size()); ++i) {
        if (i == m_activeTab) continue;
        auto& tab = m_tabs[i];
        if (tab->dirty) {
            // Switch to the tab so the user can see which document is being asked about
            m_tabList->setCurrentRow(i);
            QApplication::processEvents();

            QString name = tab->filePath.isEmpty() ? tr("Untitled") : QFileInfo(tab->filePath).fileName();
            auto btn = QMessageBox::question(this, tr("Unsaved Changes"),
                tr("Save changes to \"%1\"?").arg(name),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (btn == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (btn == QMessageBox::Save) {
                onFileSave();
                if (m_dirty) {
                    event->ignore();
                    return;
                }
            }
        }
    }

    saveSettings();
    event->accept();
}

void MainWindow::saveSettings() {
    QSettings s("paintnux", "paintnux");
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    s.setValue("pixelGrid", m_pixelGridAction->isChecked());
    s.setValue("rulers", m_rulersAction->isChecked());
    int units = 0;
    if (m_unitsInchesAction->isChecked()) units = 1;
    else if (m_unitsCmAction->isChecked()) units = 2;
    s.setValue("rulerUnits", units);
}

void MainWindow::restoreSettings() {
    QSettings s("paintnux", "paintnux");
    if (s.contains("geometry"))
        restoreGeometry(s.value("geometry").toByteArray());
    if (s.contains("windowState"))
        restoreState(s.value("windowState").toByteArray());
    m_pixelGridAction->setChecked(s.value("pixelGrid", false).toBool());
    m_rulersAction->setChecked(s.value("rulers", false).toBool());
    int units = s.value("rulerUnits", 0).toInt();
    switch (units) {
        case 1: m_unitsInchesAction->trigger(); break;
        case 2: m_unitsCmAction->trigger(); break;
        default: m_unitsPixelsAction->trigger(); break;
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const auto* mime = event->mimeData();
    if (mime->hasImage() || mime->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const auto* mime = event->mimeData();

    // 1) Direct image data (some browsers provide this)
    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            promptAndImportImage(img, tr("Dropped Image"));
            return;
        }
    }

    // 2) URLs — local files or remote http(s)
    for (const auto& url : mime->urls()) {
        if (url.isLocalFile()) {
            QString filePath = url.toLocalFile();

            // Peek at the image size to decide whether to offer expand
            QString error;
            auto peekDoc = loadDocument(filePath, &error);
            if (!peekDoc) {
                QMessageBox::critical(this, tr("Open Failed"), error);
                continue;
            }

            auto* doc = m_workspace->document();
            bool needsExpand = doc && (peekDoc->width() > doc->width() || peekDoc->height() > doc->height());

            QDialog dlg(this);
            dlg.setWindowTitle(tr("Open Dropped File"));
            auto* layout = new QVBoxLayout(&dlg);
            layout->addWidget(new QLabel(tr("How would you like to open \"%1\"?").arg(QFileInfo(filePath).fileName())));
            auto* btnLayout = new QHBoxLayout;
            auto* openBtn = new QPushButton(tr("Open as New Document"));
            auto* layerBtn = new QPushButton(tr("Add as New Layer"));
            QPushButton* expandBtn = nullptr;
            auto* cancelBtn = new QPushButton(tr("Cancel"));
            btnLayout->addWidget(openBtn);
            btnLayout->addWidget(layerBtn);
            if (needsExpand) {
                expandBtn = new QPushButton(tr("Add as Layer && Expand Canvas"));
                btnLayout->addWidget(expandBtn);
                connect(expandBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
            }
            btnLayout->addWidget(cancelBtn);
            layout->addLayout(btnLayout);
            QPushButton* clicked = nullptr;
            connect(openBtn, &QPushButton::clicked, &dlg, [&]() { clicked = openBtn; dlg.accept(); });
            connect(layerBtn, &QPushButton::clicked, &dlg, [&]() { clicked = layerBtn; dlg.accept(); });
            if (expandBtn)
                connect(expandBtn, &QPushButton::clicked, &dlg, [&]() { clicked = expandBtn; dlg.accept(); });
            connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
            if (dlg.exec() == QDialog::Rejected) continue;

            if (clicked == openBtn) {
                setNewDocument(std::move(peekDoc), filePath);
            } else if (expandBtn && clicked == expandBtn) {
                // Flatten the loaded doc to get an image, then expand+import
                Surface flat = peekDoc->flatten();
                QString name = QFileInfo(filePath).completeBaseName();
                importFromImageExpand(flat.qimage(), name);
            } else if (clicked == layerBtn) {
                importFromPath(filePath);
            }
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            handleDroppedUrl(url);
        }
    }
}

void MainWindow::handleDroppedUrl(const QUrl& url) {
    if (!m_networkManager)
        m_networkManager = new QNetworkAccessManager(this);

    QNetworkReply* reply = m_networkManager->get(QNetworkRequest(url));
    setCursor(Qt::WaitCursor);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        reply->deleteLater();
        unsetCursor();

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Download Failed"),
                tr("Could not download image: %1").arg(reply->errorString()));
            return;
        }

        QImage img;
        if (!img.loadFromData(reply->readAll())) {
            QMessageBox::warning(this, tr("Open Failed"),
                tr("The downloaded file is not a supported image format."));
            return;
        }

        QString name = QFileInfo(url.path()).completeBaseName();
        if (name.isEmpty()) name = tr("Dropped Image");
        promptAndImportImage(img, name);
    });
}

void MainWindow::promptAndImportImage(const QImage& image, const QString& name) {
    auto* doc = m_workspace->document();
    bool needsExpand = doc && (image.width() > doc->width() || image.height() > doc->height());

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Open Dropped Image"));
    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("How would you like to open \"%1\"?").arg(name)));
    auto* btnLayout = new QHBoxLayout;
    auto* openBtn = new QPushButton(tr("Open as New Document"));
    auto* layerBtn = new QPushButton(tr("Add as New Layer"));
    QPushButton* expandBtn = nullptr;
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    btnLayout->addWidget(openBtn);
    btnLayout->addWidget(layerBtn);
    if (needsExpand) {
        expandBtn = new QPushButton(tr("Add as Layer && Expand Canvas"));
        btnLayout->addWidget(expandBtn);
    }
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
    QPushButton* clicked = nullptr;
    connect(openBtn, &QPushButton::clicked, &dlg, [&]() { clicked = openBtn; dlg.accept(); });
    connect(layerBtn, &QPushButton::clicked, &dlg, [&]() { clicked = layerBtn; dlg.accept(); });
    if (expandBtn)
        connect(expandBtn, &QPushButton::clicked, &dlg, [&]() { clicked = expandBtn; dlg.accept(); });
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Rejected) return;

    if (clicked == openBtn) {
        QImage img = image.format() == QImage::Format_ARGB32
            ? image : image.convertToFormat(QImage::Format_ARGB32);
        auto newDoc = std::make_unique<Document>(img.width(), img.height());
        auto layer = std::make_unique<BitmapLayer>(Surface(std::move(img)));
        layer->setName(name);
        newDoc->addLayer(std::move(layer));
        setNewDocument(std::move(newDoc), QString());
    } else if (expandBtn && clicked == expandBtn) {
        importFromImageExpand(image, name);
    } else if (clicked == layerBtn) {
        importFromImage(image, name);
    }
}

void MainWindow::importFromImageExpand(const QImage& image, const QString& name) {
    auto* doc = m_workspace->document();
    if (!doc) return;

    int newW = qMax(image.width(), doc->width());
    int newH = qMax(image.height(), doc->height());

    // Build expanded document with all existing layers
    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        Surface expanded(newW, newH);
        ColorBgra fill = (i == 0)
            ? ColorBgra::fromBgra(255, 255, 255, 255)
            : ColorBgra::fromBgra(0, 0, 0, 0);
        expanded.clear(fill);
        expanded.copySurface(layer->surface());

        auto newLayer = std::make_unique<BitmapLayer>(std::move(expanded));
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    // Add the imported image as a new layer
    QImage img = image.format() == QImage::Format_ARGB32
        ? image : image.convertToFormat(QImage::Format_ARGB32);
    Surface importSurf(newW, newH);
    importSurf.copySurface(Surface(std::move(img)));
    auto importLayer = std::make_unique<BitmapLayer>(std::move(importSurf));
    importLayer->setName(name);
    newDoc->addLayer(std::move(importLayer));

    int newActiveIdx = newDoc->layerCount() - 1;

    // Swap document with undo support
    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(
        std::move(newDoc), newActiveIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        auto result = m_workspace->replaceDocumentForHistory(std::move(d), idx);
        m_workspace->canvas()->zoomToFit();
        return result;
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Import & Expand Canvas"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    m_sizeLabel->setText(tr("Size: %1 x %2").arg(newW).arg(newH));
    m_workspace->canvas()->zoomToFit();
    m_workspace->invalidateAll();
}

void MainWindow::importFromImage(const QImage& image, const QString& name) {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QImage img = image;
    if (img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_ARGB32);

    Surface padded(doc->width(), doc->height());
    padded.copySurface(Surface(std::move(img)));

    int oldActive = m_workspace->activeLayerIndex();
    auto newLayer = std::make_unique<BitmapLayer>(std::move(padded));
    newLayer->setName(name);

    int insertIdx = oldActive + 1;
    doc->insertLayer(insertIdx, std::move(newLayer));
    m_workspace->setActiveLayerIndex(insertIdx);

    SetActiveIndexFn setActiveFn = [this](int idx) { m_workspace->setActiveLayerIndex(idx); };
    auto memento = std::make_unique<AddLayerMemento>(
        tr("Import Layer"), doc, insertIdx, setActiveFn, oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    m_workspace->invalidateAll();
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Only handle bare letter keys (no Ctrl/Alt/Meta modifiers)
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Don't intercept if a text input widget has focus
    auto* focused = focusWidget();
    if (qobject_cast<QLineEdit*>(focused) || qobject_cast<QSpinBox*>(focused)) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    QString text = event->text().toLower();
    if (text.length() == 1 && text[0].isLetter()) {
        if (m_toolsDock->activateShortcut(text[0].toLatin1()))
            return;
    }

    QMainWindow::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_zoomLabel && event->type() == QEvent::MouseButtonPress) {
        double current = m_workspace->canvas()->zoomFactor() * 100.0;
        bool ok = false;
        double val = QInputDialog::getDouble(
            this, tr("Zoom Level"), tr("Zoom (%):"),
            current, 1.0, 6400.0, 1, &ok);
        if (ok) {
            m_workspace->canvas()->setZoomFactor(val / 100.0);
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

// --- File menu actions ---

void MainWindow::onFileNew() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New Image"));
    auto* form = new QFormLayout(&dlg);

    bool updating = false;
    double resolution = 72.0; // DPI
    bool unitsInches = true;   // true=inches, false=cm

    // Default to clipboard image size if available, else 800x600
    int defaultW = 800, defaultH = 600;
    const QClipboard* clip = QApplication::clipboard();
    if (clip && clip->mimeData() && clip->mimeData()->hasImage()) {
        QImage img = qvariant_cast<QImage>(clip->mimeData()->imageData());
        if (!img.isNull() && img.width() > 0 && img.height() > 0) {
            defaultW = img.width();
            defaultH = img.height();
        }
    }

    // --- Pixel dimensions ---
    auto* pixelWidthSpin = new QSpinBox;
    pixelWidthSpin->setRange(1, 10000);
    pixelWidthSpin->setValue(defaultW);
    pixelWidthSpin->setSuffix(tr(" px"));
    form->addRow(tr("Width:"), pixelWidthSpin);

    auto* pixelHeightSpin = new QSpinBox;
    pixelHeightSpin->setRange(1, 10000);
    pixelHeightSpin->setValue(defaultH);
    pixelHeightSpin->setSuffix(tr(" px"));
    form->addRow(tr("Height:"), pixelHeightSpin);

    // --- Resolution ---
    auto* resSpin = new QDoubleSpinBox;
    resSpin->setRange(1.0, 10000.0);
    resSpin->setValue(72.0);
    resSpin->setDecimals(2);

    auto* resUnitsCombo = new QComboBox;
    resUnitsCombo->addItem(tr("pixels/inch"));
    resUnitsCombo->addItem(tr("pixels/cm"));

    auto* resRow = new QHBoxLayout;
    resRow->addWidget(resSpin);
    resRow->addWidget(resUnitsCombo);
    form->addRow(tr("Resolution:"), resRow);

    // --- Print size ---
    auto* printWidthSpin = new QDoubleSpinBox;
    printWidthSpin->setRange(0.01, 10000.0);
    printWidthSpin->setDecimals(3);

    auto* printHeightSpin = new QDoubleSpinBox;
    printHeightSpin->setRange(0.01, 10000.0);
    printHeightSpin->setDecimals(3);

    auto* printUnitsCombo = new QComboBox;
    printUnitsCombo->addItem(tr("inches"));
    printUnitsCombo->addItem(tr("cm"));

    auto* printWRow = new QHBoxLayout;
    printWRow->addWidget(printWidthSpin);
    printWRow->addWidget(printUnitsCombo);
    form->addRow(tr("Print width:"), printWRow);
    form->addRow(tr("Print height:"), printHeightSpin);

    // Helper: get effective DPI (always in pixels/inch internally)
    auto effectiveDpi = [&]() -> double {
        return unitsInches ? resolution : resolution * 2.54;
    };

    // Helper: recalc print size from pixels
    auto updatePrintFromPixels = [&]() {
        double dpi = effectiveDpi();
        double pw = pixelWidthSpin->value() / dpi;
        double ph = pixelHeightSpin->value() / dpi;
        if (!unitsInches) { pw *= 2.54; ph *= 2.54; }
        printWidthSpin->setValue(pw);
        printHeightSpin->setValue(ph);
    };

    // Helper: recalc pixels from print size
    auto updatePixelsFromPrint = [&]() {
        double dpi = effectiveDpi();
        double pw = printWidthSpin->value();
        double ph = printHeightSpin->value();
        if (!unitsInches) { pw /= 2.54; ph /= 2.54; }
        pixelWidthSpin->setValue(qMax(1, static_cast<int>(pw * dpi + 0.5)));
        pixelHeightSpin->setValue(qMax(1, static_cast<int>(ph * dpi + 0.5)));
    };

    // Initialize print size
    updatePrintFromPixels();

    // Pixel changes -> update print size
    connect(pixelWidthSpin, &QSpinBox::valueChanged, &dlg, [&]() {
        if (updating) return;
        updating = true;
        updatePrintFromPixels();
        updating = false;
    });
    connect(pixelHeightSpin, &QSpinBox::valueChanged, &dlg, [&]() {
        if (updating) return;
        updating = true;
        updatePrintFromPixels();
        updating = false;
    });

    // Print size changes -> update pixels
    connect(printWidthSpin, &QDoubleSpinBox::valueChanged, &dlg, [&]() {
        if (updating) return;
        updating = true;
        updatePixelsFromPrint();
        updating = false;
    });
    connect(printHeightSpin, &QDoubleSpinBox::valueChanged, &dlg, [&]() {
        if (updating) return;
        updating = true;
        updatePixelsFromPrint();
        updating = false;
    });

    // Resolution changes -> keep pixels, update print size
    connect(resSpin, &QDoubleSpinBox::valueChanged, &dlg, [&](double val) {
        if (updating) return;
        updating = true;
        resolution = val;
        updatePrintFromPixels();
        updating = false;
    });

    // Resolution units change -> convert resolution value
    connect(resUnitsCombo, &QComboBox::currentIndexChanged, &dlg, [&](int idx) {
        if (updating) return;
        updating = true;
        bool nowInches = (idx == 0);
        if (nowInches && !unitsInches) {
            // cm -> inches
            resolution = resolution * 2.54;
        } else if (!nowInches && unitsInches) {
            // inches -> cm
            resolution = resolution / 2.54;
        }
        unitsInches = nowInches;
        resSpin->setValue(resolution);
        updatePrintFromPixels();
        updating = false;
    });

    // Print units change -> convert print values and sync res units
    connect(printUnitsCombo, &QComboBox::currentIndexChanged, &dlg, [&](int idx) {
        if (updating) return;
        updating = true;
        bool nowInches = (idx == 0);
        if (nowInches && !unitsInches) {
            printWidthSpin->setValue(printWidthSpin->value() / 2.54);
            printHeightSpin->setValue(printHeightSpin->value() / 2.54);
            resolution = resolution * 2.54;
        } else if (!nowInches && unitsInches) {
            printWidthSpin->setValue(printWidthSpin->value() * 2.54);
            printHeightSpin->setValue(printHeightSpin->value() * 2.54);
            resolution = resolution / 2.54;
        }
        unitsInches = nowInches;
        resSpin->setValue(resolution);
        resUnitsCombo->setCurrentIndex(idx);
        updating = false;
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    int w = pixelWidthSpin->value();
    int h = pixelHeightSpin->value();
    auto doc = std::make_unique<Document>(w, h);
    doc->addLayer(BitmapLayer::createBackground(w, h));
    setNewDocument(std::move(doc), QString());

    // Set DPI metadata on the canvas surface
    double dpi = effectiveDpi();
    int dpm = static_cast<int>(dpi * 39.3701 + 0.5); // dots per meter
    auto* layer = dynamic_cast<BitmapLayer*>(m_workspace->document()->layerAt(0));
    if (layer) {
        layer->surface().qimage().setDotsPerMeterX(dpm);
        layer->surface().qimage().setDotsPerMeterY(dpm);
    }
}

void MainWindow::onFileOpen() {
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open Image"), QString(), openFileFilter(),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (filePath.isEmpty()) return;

    QString error;
    auto doc = loadDocument(filePath, &error);
    if (!doc) {
        QMessageBox::critical(this, tr("Open Failed"), error);
        return;
    }

    setNewDocument(std::move(doc), filePath);
}

void MainWindow::commitFloatingOverlayForSave() {
    if (!m_workspace->overlaySurface()) return;

    auto* doc = m_workspace->document();
    if (!doc) return;

    auto* layer = dynamic_cast<BitmapLayer*>(
        doc->layerAt(m_workspace->activeLayerIndex()));
    if (!layer) {
        m_workspace->clearOverlay();
        return;
    }

    // Save undo state, commit overlay pixels, push memento
    QPoint offset = m_workspace->overlayOffset();
    const Surface* ovl = m_workspace->overlaySurface();
    QRect overlayBounds(offset, QSize(ovl->width(), ovl->height()));
    QRegion region(overlayBounds.intersected(layer->surface().bounds()));

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Commit Paste"), doc, m_workspace->activeLayerIndex(), region);
    m_workspace->commitOverlay();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    // Sync MoveTool state — overlay is gone now
    auto* moveTool = dynamic_cast<MoveTool*>(m_workspace->activeTool());
    if (moveTool)
        moveTool->adoptOverlay();
}

void MainWindow::onFileSave() {
    commitFloatingOverlayForSave();

    if (m_currentFilePath.isEmpty()) {
        onFileSaveAs();
        return;
    }

    auto result = saveDocument(*m_workspace->document(), m_currentFilePath);
    if (!result.success) {
        QMessageBox::critical(this, tr("Save Failed"), result.errorMessage);
        return;
    }

    clearDirty();
}

void MainWindow::onFileSaveAs() {
    commitFloatingOverlayForSave();

    QFileDialog dlg(this, tr("Save As"), m_currentFilePath, saveFileFilter());
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setDefaultSuffix(QStringLiteral("pnx"));

    // Update default suffix when the user switches filter
    connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString& filter) {
        static const QRegularExpression extRe(QStringLiteral(R"(\*\.(\w+))"));
        auto match = extRe.match(filter);
        if (match.hasMatch())
            dlg.setDefaultSuffix(match.captured(1));
    });

    if (dlg.exec() != QDialog::Accepted) return;
    QString filePath = dlg.selectedFiles().first();

    auto result = saveDocument(*m_workspace->document(), filePath);
    if (!result.success) {
        QMessageBox::critical(this, tr("Save Failed"), result.errorMessage);
        return;
    }

    m_currentFilePath = filePath;
    clearDirty();
    addRecentFile(filePath);
}

void MainWindow::onFileExport() {
    commitFloatingOverlayForSave();

    auto* doc = m_workspace->document();
    if (!doc) return;

    // File dialog for flat formats only
    QFileDialog dlg(this, tr("Export"), m_currentFilePath, exportFileFilter());
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setDefaultSuffix(QStringLiteral("png"));

    connect(&dlg, &QFileDialog::filterSelected, &dlg, [&dlg](const QString& filter) {
        static const QRegularExpression extRe(QStringLiteral(R"(\*\.(\w+))"));
        auto match = extRe.match(filter);
        if (match.hasMatch())
            dlg.setDefaultSuffix(match.captured(1));
    });

    if (dlg.exec() != QDialog::Accepted) return;
    QString filePath = dlg.selectedFiles().first();
    QString ext = QFileInfo(filePath).suffix().toLower();

    ExportOptions opts;

    // Show format-specific settings dialog for formats that have options
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("JPEG Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        // Quality
        auto* qualLabel = new QLabel(tr("Quality:"));
        layout->addWidget(qualLabel);
        auto* qualRow = new QHBoxLayout;
        auto* qualSlider = new QSlider(Qt::Horizontal);
        qualSlider->setRange(0, 100);
        qualSlider->setValue(opts.jpegQuality);
        auto* qualSpin = new QSpinBox;
        qualSpin->setRange(0, 100);
        qualSpin->setValue(opts.jpegQuality);
        connect(qualSlider, &QSlider::valueChanged, qualSpin, &QSpinBox::setValue);
        connect(qualSpin, qOverload<int>(&QSpinBox::valueChanged), qualSlider, &QSlider::setValue);
        qualRow->addWidget(qualSlider);
        qualRow->addWidget(qualSpin);
        layout->addLayout(qualRow);

        // Progressive
        auto* progCheck = new QCheckBox(tr("Progressive"));
        progCheck->setChecked(opts.jpegProgressive);
        layout->addWidget(progCheck);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;
        opts.jpegQuality = qualSpin->value();
        opts.jpegProgressive = progCheck->isChecked();

    } else if (ext == QStringLiteral("png")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("PNG Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        // Bit depth radio buttons
        auto* depthGroup = new QGroupBox(tr("Color Depth"));
        auto* depthLayout = new QVBoxLayout(depthGroup);
        auto* radioAuto = new QRadioButton(tr("Auto-detect"));
        auto* radio32   = new QRadioButton(tr("32-bit (RGBA)"));
        auto* radio24   = new QRadioButton(tr("24-bit (RGB, no transparency)"));
        auto* radio8    = new QRadioButton(tr("8-bit (256 colors)"));
        radioAuto->setChecked(true);
        depthLayout->addWidget(radioAuto);
        depthLayout->addWidget(radio32);
        depthLayout->addWidget(radio24);
        depthLayout->addWidget(radio8);
        layout->addWidget(depthGroup);

        // Dither level
        auto* ditherLabel = new QLabel(tr("Dithering level:"));
        layout->addWidget(ditherLabel);
        auto* ditherRow = new QHBoxLayout;
        auto* ditherSlider = new QSlider(Qt::Horizontal);
        ditherSlider->setRange(0, 8);
        ditherSlider->setValue(opts.pngDitherLevel);
        auto* ditherSpin = new QSpinBox;
        ditherSpin->setRange(0, 8);
        ditherSpin->setValue(opts.pngDitherLevel);
        connect(ditherSlider, &QSlider::valueChanged, ditherSpin, &QSpinBox::setValue);
        connect(ditherSpin, qOverload<int>(&QSpinBox::valueChanged), ditherSlider, &QSlider::setValue);
        ditherRow->addWidget(ditherSlider);
        ditherRow->addWidget(ditherSpin);
        layout->addLayout(ditherRow);

        // Transparency threshold
        auto* threshLabel = new QLabel(tr("Transparency threshold:"));
        layout->addWidget(threshLabel);
        auto* threshRow = new QHBoxLayout;
        auto* threshSlider = new QSlider(Qt::Horizontal);
        threshSlider->setRange(0, 255);
        threshSlider->setValue(opts.pngThreshold);
        auto* threshSpin = new QSpinBox;
        threshSpin->setRange(0, 255);
        threshSpin->setValue(opts.pngThreshold);
        connect(threshSlider, &QSlider::valueChanged, threshSpin, &QSpinBox::setValue);
        connect(threshSpin, qOverload<int>(&QSpinBox::valueChanged), threshSlider, &QSlider::setValue);
        threshRow->addWidget(threshSlider);
        threshRow->addWidget(threshSpin);
        layout->addLayout(threshRow);

        // Enable dither/threshold only for 8-bit
        auto updateEnabled = [=]() {
            bool is8bit = radio8->isChecked();
            ditherSlider->setEnabled(is8bit);
            ditherSpin->setEnabled(is8bit);
            threshSlider->setEnabled(is8bit);
            threshSpin->setEnabled(is8bit);
            ditherLabel->setEnabled(is8bit);
            threshLabel->setEnabled(is8bit);
        };
        connect(radioAuto, &QRadioButton::toggled, this, updateEnabled);
        connect(radio32, &QRadioButton::toggled, this, updateEnabled);
        connect(radio24, &QRadioButton::toggled, this, updateEnabled);
        connect(radio8, &QRadioButton::toggled, this, updateEnabled);
        updateEnabled();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;

        if (radio32->isChecked()) opts.pngBitDepth = PngBitDepth::Bpp32;
        else if (radio24->isChecked()) opts.pngBitDepth = PngBitDepth::Bpp24;
        else if (radio8->isChecked()) opts.pngBitDepth = PngBitDepth::Bpp8;
        else opts.pngBitDepth = PngBitDepth::AutoDetect;

        opts.pngDitherLevel = ditherSpin->value();
        opts.pngThreshold = threshSpin->value();

    } else if (ext == QStringLiteral("webp")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("WebP Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        auto* qualLabel = new QLabel(tr("Quality:"));
        layout->addWidget(qualLabel);
        auto* qualRow = new QHBoxLayout;
        auto* qualSlider = new QSlider(Qt::Horizontal);
        qualSlider->setRange(0, 100);
        qualSlider->setValue(opts.webpQuality);
        auto* qualSpin = new QSpinBox;
        qualSpin->setRange(0, 100);
        qualSpin->setValue(opts.webpQuality);
        connect(qualSlider, &QSlider::valueChanged, qualSpin, &QSpinBox::setValue);
        connect(qualSpin, qOverload<int>(&QSpinBox::valueChanged), qualSlider, &QSlider::setValue);
        qualRow->addWidget(qualSlider);
        qualRow->addWidget(qualSpin);
        layout->addLayout(qualRow);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;
        opts.webpQuality = qualSpin->value();

    } else if (ext == QStringLiteral("tif") || ext == QStringLiteral("tiff")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("TIFF Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        auto* compLabel = new QLabel(tr("Compression:"));
        layout->addWidget(compLabel);
        auto* compCombo = new QComboBox;
        compCombo->addItem(tr("None"), 0);
        compCombo->addItem(tr("LZW"), 1);
        compCombo->setCurrentIndex(opts.tiffCompression);
        layout->addWidget(compCombo);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;
        opts.tiffCompression = compCombo->currentData().toInt();

    } else if (ext == QStringLiteral("bmp")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("BMP Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        // Bit depth radio buttons
        auto* depthGroup = new QGroupBox(tr("Color Depth"));
        auto* depthLayout = new QVBoxLayout(depthGroup);
        auto* radioAuto = new QRadioButton(tr("Auto-detect"));
        auto* radio24   = new QRadioButton(tr("24-bit (RGB)"));
        auto* radio8    = new QRadioButton(tr("8-bit (256 colors)"));
        radioAuto->setChecked(true);
        depthLayout->addWidget(radioAuto);
        depthLayout->addWidget(radio24);
        depthLayout->addWidget(radio8);
        layout->addWidget(depthGroup);

        // Dither level
        auto* ditherLabel = new QLabel(tr("Dithering level:"));
        layout->addWidget(ditherLabel);
        auto* ditherRow = new QHBoxLayout;
        auto* ditherSlider = new QSlider(Qt::Horizontal);
        ditherSlider->setRange(0, 8);
        ditherSlider->setValue(opts.bmpDitherLevel);
        auto* ditherSpin = new QSpinBox;
        ditherSpin->setRange(0, 8);
        ditherSpin->setValue(opts.bmpDitherLevel);
        connect(ditherSlider, &QSlider::valueChanged, ditherSpin, &QSpinBox::setValue);
        connect(ditherSpin, qOverload<int>(&QSpinBox::valueChanged), ditherSlider, &QSlider::setValue);
        ditherRow->addWidget(ditherSlider);
        ditherRow->addWidget(ditherSpin);
        layout->addLayout(ditherRow);

        // Enable dither only for 8-bit
        auto updateEnabled = [=]() {
            bool is8bit = radio8->isChecked();
            ditherSlider->setEnabled(is8bit);
            ditherSpin->setEnabled(is8bit);
            ditherLabel->setEnabled(is8bit);
        };
        connect(radioAuto, &QRadioButton::toggled, this, updateEnabled);
        connect(radio24, &QRadioButton::toggled, this, updateEnabled);
        connect(radio8, &QRadioButton::toggled, this, updateEnabled);
        updateEnabled();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;

        if (radio24->isChecked()) opts.bmpBitDepth = BmpBitDepth::Bpp24;
        else if (radio8->isChecked()) opts.bmpBitDepth = BmpBitDepth::Bpp8;
        else opts.bmpBitDepth = BmpBitDepth::AutoDetect;
        opts.bmpDitherLevel = ditherSpin->value();

    } else if (ext == QStringLiteral("gif")) {
        QDialog settingsDlg(this);
        settingsDlg.setWindowTitle(tr("GIF Export Settings"));
        auto* layout = new QVBoxLayout(&settingsDlg);

        // Dither level
        auto* ditherLabel = new QLabel(tr("Dithering level:"));
        layout->addWidget(ditherLabel);
        auto* ditherRow = new QHBoxLayout;
        auto* ditherSlider = new QSlider(Qt::Horizontal);
        ditherSlider->setRange(0, 8);
        ditherSlider->setValue(opts.gifDitherLevel);
        auto* ditherSpin = new QSpinBox;
        ditherSpin->setRange(0, 8);
        ditherSpin->setValue(opts.gifDitherLevel);
        connect(ditherSlider, &QSlider::valueChanged, ditherSpin, &QSpinBox::setValue);
        connect(ditherSpin, qOverload<int>(&QSpinBox::valueChanged), ditherSlider, &QSlider::setValue);
        ditherRow->addWidget(ditherSlider);
        ditherRow->addWidget(ditherSpin);
        layout->addLayout(ditherRow);

        // Transparency threshold
        auto* threshLabel = new QLabel(tr("Transparency threshold:"));
        layout->addWidget(threshLabel);
        auto* threshRow = new QHBoxLayout;
        auto* threshSlider = new QSlider(Qt::Horizontal);
        threshSlider->setRange(0, 255);
        threshSlider->setValue(opts.gifThreshold);
        auto* threshSpin = new QSpinBox;
        threshSpin->setRange(0, 255);
        threshSpin->setValue(opts.gifThreshold);
        connect(threshSlider, &QSlider::valueChanged, threshSpin, &QSpinBox::setValue);
        connect(threshSpin, qOverload<int>(&QSpinBox::valueChanged), threshSlider, &QSlider::setValue);
        threshRow->addWidget(threshSlider);
        threshRow->addWidget(threshSpin);
        layout->addLayout(threshRow);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &settingsDlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &settingsDlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (settingsDlg.exec() != QDialog::Accepted) return;
        opts.gifDitherLevel = ditherSpin->value();
        opts.gifThreshold = threshSpin->value();
    }

    auto result = exportDocument(*doc, filePath, opts);
    if (!result.success) {
        QMessageBox::critical(this, tr("Export Failed"), result.errorMessage);
        return;
    }

    statusBar()->showMessage(tr("Exported to %1").arg(QFileInfo(filePath).fileName()), 5000);
}

void MainWindow::onPrint() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    dlg.setWindowTitle(tr("Print"));
    if (dlg.exec() != QDialog::Accepted) return;

    renderForPrint(&printer);
}

void MainWindow::onPrintPreview() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QPrinter printer(QPrinter::HighResolution);
    QPrintPreviewDialog preview(&printer, this);
    preview.setWindowTitle(tr("Print Preview"));
    connect(&preview, &QPrintPreviewDialog::paintRequested,
            this, &MainWindow::renderForPrint);
    preview.exec();
}

void MainWindow::renderForPrint(QPrinter* printer) {
    auto* doc = m_workspace->document();
    if (!doc) return;

    const Surface* surface = m_workspace->compositeSurface();
    if (!surface) return;

    QPainter painter(printer);

    // Get printable area in device pixels
    QRectF pageRect = printer->pageLayout().paintRectPixels(printer->resolution());

    // Image size in device pixels at the printer's DPI
    // Use the image's logical DPI (default 96) to compute physical size,
    // then scale to printer resolution
    double imageDpiX = surface->qimage().dotsPerMeterX() > 0
        ? surface->qimage().dotsPerMeterX() / 39.3701 : 96.0;
    double imageDpiY = surface->qimage().dotsPerMeterY() > 0
        ? surface->qimage().dotsPerMeterY() / 39.3701 : 96.0;

    // Physical size of image in inches
    double imageWidthInches = surface->width() / imageDpiX;
    double imageHeightInches = surface->height() / imageDpiY;

    // Size in printer pixels
    double printW = imageWidthInches * printer->resolution();
    double printH = imageHeightInches * printer->resolution();

    // Scale to fit page if image is larger than printable area
    double scaleX = pageRect.width() / printW;
    double scaleY = pageRect.height() / printH;
    double scale = std::min({scaleX, scaleY, 1.0});

    double finalW = printW * scale;
    double finalH = printH * scale;

    // Center on page
    double x = pageRect.left() + (pageRect.width() - finalW) / 2.0;
    double y = pageRect.top() + (pageRect.height() - finalH) / 2.0;

    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(x, y, finalW, finalH), surface->qimage());
}

void MainWindow::onFileClose() {
    onTabCloseRequested(m_activeTab);
}

void MainWindow::onUndo() {
    // If a tool has in-progress work, cancel it instead of undoing history
    auto* lineTool = dynamic_cast<LineTool*>(m_workspace->activeTool());
    if (lineTool && lineTool->showingNubs()) {
        // Cancel in-progress curve (restore saved surface, don't commit)
        QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        lineTool->keyDown(&escEvent);
        m_workspace->canvas()->setToolNubs({});
        m_workspace->invalidateAll();
        return;
    }
    auto* textTool = dynamic_cast<TextTool*>(m_workspace->activeTool());
    if (textTool && textTool->isEditing()) {
        textTool->cancelText();
        m_workspace->canvas()->clearTextCursor();
        m_workspace->invalidateAll();
        return;
    }

    // If there's a floating overlay (from move/paste), discard it before undoing.
    // The undo memento will restore the erased pixels.
    if (m_workspace->overlaySurface()) {
        m_workspace->clearOverlay();
        auto* moveTool = dynamic_cast<MoveTool*>(m_workspace->activeTool());
        if (moveTool) {
            // Restore the selection path from before the drag started
            auto* sel = m_workspace->selection();
            if (sel) {
                sel->setPath(moveTool->savedSelectionPath());
            }
            moveTool->adoptOverlay(); // overlay is null → resets m_lifted
        }
    }

    if (auto* stack = m_workspace->historyStack()) {
        stack->stepBackward();
        m_workspace->invalidateAll();
    }
}

void MainWindow::onRedo() {
    if (auto* stack = m_workspace->historyStack()) {
        stack->stepForward();
        m_workspace->invalidateAll();
    }
}

// --- Selection edit menu actions ---

void MainWindow::onSelectAll() {
    auto* sel = m_workspace->selection();
    auto* doc = m_workspace->document();
    if (!sel || !doc) return;

    auto memento = std::make_unique<SelectionHistoryMemento>(tr("Select All"), sel);
    sel->selectAll(doc->bounds());
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onDeselect() {
    // If the text tool is editing, forward Escape/Enter to it instead
    auto* textTool = dynamic_cast<TextTool*>(m_workspace->activeTool());
    if (textTool && textTool->isEditing()) {
        QKeyEvent escEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        textTool->keyDown(&escEvent);
        m_workspace->canvas()->clearTextCursor();
        return;
    }

    // If the line/curve tool is in curve editing, forward Enter/Escape to it
    auto* lineTool = dynamic_cast<LineTool*>(m_workspace->activeTool());
    if (lineTool && lineTool->showingNubs()) {
        QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        lineTool->keyDown(&keyEvent);
        m_workspace->canvas()->setToolNubs({});
        return;
    }

    // Commit any floating overlay first (e.g. from paste/move)
    if (m_workspace->overlaySurface()) {
        auto* tool = dynamic_cast<MoveTool*>(m_workspace->activeTool());
        if (tool) {
            tool->deactivate();
            tool->activate();
        } else {
            m_workspace->commitOverlay();
        }
    }

    auto* sel = m_workspace->selection();
    if (!sel || sel->isEmpty()) return;

    auto memento = std::make_unique<SelectionHistoryMemento>(tr("Deselect"), sel);
    sel->reset();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onInvertSelection() {
    // Commit any floating overlay first (e.g. from paste/move)
    if (m_workspace->overlaySurface()) {
        auto* tool = dynamic_cast<MoveTool*>(m_workspace->activeTool());
        if (tool) {
            tool->deactivate();
            tool->activate();
        } else {
            m_workspace->commitOverlay();
        }
    }

    auto* sel = m_workspace->selection();
    auto* doc = m_workspace->document();
    if (!sel || !doc) return;

    auto memento = std::make_unique<SelectionHistoryMemento>(tr("Invert Selection"), sel);
    sel->invert(doc->bounds());
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onCopy() {
    auto* sel = m_workspace->selection();
    auto* layer = dynamic_cast<BitmapLayer*>(
        m_workspace->document()->layerAt(m_workspace->activeLayerIndex()));
    if (!sel || sel->isEmpty() || !layer) return;

    const Surface& src = layer->surface();
    QRect bounds = sel->region().boundingRect().intersected(src.bounds());
    if (bounds.isEmpty()) return;

    // Remember the origin, size and selection shape so paste restores them
    m_copyOrigin = bounds.topLeft();
    m_copySize = bounds.size();
    m_copySelectionPath = sel->path();

    // Create a QImage with the selected pixels (transparent elsewhere)
    QImage img(bounds.width(), bounds.height(), QImage::Format_ARGB32);
    img.fill(0);

    const QRegion& region = sel->region();
    for (const QRect& r : region) {
        QRect clipped = r.intersected(src.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            const ColorBgra* srcRow = src.rowPtr(y);
            auto* dstRow = reinterpret_cast<ColorBgra*>(img.scanLine(y - bounds.top()));
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                dstRow[x - bounds.left()] = srcRow[x];
            }
        }
    }

    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setImage(img);
}

void MainWindow::onCut() {
    auto* sel = m_workspace->selection();
    auto* layer = dynamic_cast<BitmapLayer*>(
        m_workspace->document()->layerAt(m_workspace->activeLayerIndex()));
    if (!sel || sel->isEmpty() || !layer) return;

    // Copy first
    onCopy();

    // Then erase selected pixels (with undo)
    const QRegion& region = sel->region();
    Surface& surf = layer->surface();

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Cut"), m_workspace->document(), m_workspace->activeLayerIndex(), region);

    for (const QRect& r : region) {
        QRect clipped = r.intersected(surf.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            ColorBgra* row = surf.rowPtr(y);
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                row[x] = ColorBgra::transparent();
            }
        }
    }

    m_workspace->historyStack()->pushNewMemento(std::move(memento));
    m_workspace->invalidateAll();
}

void MainWindow::onDelete() {
    // If there's a floating overlay (active paste), discard it
    if (m_workspace->overlaySurface()) {
        auto* sel = m_workspace->selection();
        auto memento = std::make_unique<PasteHistoryMemento>(
            tr("Delete Paste"), sel, m_workspace);

        m_workspace->clearOverlay();
        sel->reset();

        m_workspace->historyStack()->pushNewMemento(std::move(memento));

        // Sync MoveTool state
        auto* moveTool = dynamic_cast<MoveTool*>(m_workspace->activeTool());
        if (moveTool) moveTool->adoptOverlay();
        return;
    }

    auto* sel = m_workspace->selection();
    auto* layer = dynamic_cast<BitmapLayer*>(
        m_workspace->document()->layerAt(m_workspace->activeLayerIndex()));
    if (!sel || sel->isEmpty() || !layer) return;

    const QRegion& region = sel->region();
    Surface& surf = layer->surface();

    // Check if there are any non-transparent pixels to delete
    bool hasContent = false;
    for (const QRect& r : region) {
        QRect clipped = r.intersected(surf.bounds());
        for (int y = clipped.top(); !hasContent && y <= clipped.bottom(); ++y) {
            const ColorBgra* row = surf.rowPtr(y);
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                if (row[x].a != 0) { hasContent = true; break; }
            }
        }
        if (hasContent) break;
    }
    if (!hasContent) return;

    auto bmpMemento = std::make_unique<BitmapHistoryMemento>(
        tr("Delete"), m_workspace->document(), m_workspace->activeLayerIndex(), region);

    for (const QRect& r : region) {
        QRect clipped = r.intersected(surf.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            ColorBgra* row = surf.rowPtr(y);
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                row[x] = ColorBgra::transparent();
            }
        }
    }

    // Clear selection after delete, compound both into one undo step
    auto selMemento = std::make_unique<SelectionHistoryMemento>(tr("Delete"), sel);
    sel->reset();

    auto compound = std::make_unique<CompoundHistoryMemento>(
        tr("Delete"), std::move(bmpMemento), std::move(selMemento));
    m_workspace->historyStack()->pushNewMemento(std::move(compound));
    m_workspace->invalidateAll();
}

void MainWindow::onFillSelection() {
    auto* sel = m_workspace->selection();
    auto* layer = dynamic_cast<BitmapLayer*>(
        m_workspace->document()->layerAt(m_workspace->activeLayerIndex()));
    if (!sel || sel->isEmpty() || !layer) return;

    ColorBgra fillColor = m_colorsDock->primaryColor();
    const QRegion& region = sel->region();
    Surface& surf = layer->surface();

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Fill Selection"), m_workspace->document(), m_workspace->activeLayerIndex(), region);

    for (const QRect& r : region) {
        QRect clipped = r.intersected(surf.bounds());
        for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
            ColorBgra* row = surf.rowPtr(y);
            for (int x = clipped.left(); x <= clipped.right(); ++x) {
                row[x] = fillColor;
            }
        }
    }

    m_workspace->historyStack()->pushNewMemento(std::move(memento));
    m_workspace->invalidateAll();
}

void MainWindow::onPaste() {
    QClipboard* clipboard = QApplication::clipboard();
    QImage img = clipboard->image();
    if (img.isNull()) return;

    // Convert to ARGB32 for our Surface
    if (img.format() != QImage::Format_ARGB32) {
        img = img.convertToFormat(QImage::Format_ARGB32);
    }

    auto* doc = m_workspace->document();
    if (!doc) return;

    // If the pasted image is larger than the canvas, ask the user what to do
    if (img.width() > doc->width() || img.height() > doc->height()) {
        QMessageBox msgBox(this);
        msgBox.setWindowTitle(tr("Paste"));
        msgBox.setText(tr("The image being pasted is larger than the canvas size.\n"
                          "What do you want to do?"));
        auto* expandBtn = msgBox.addButton(tr("Expand canvas"), QMessageBox::YesRole);
        msgBox.addButton(tr("Keep canvas size"), QMessageBox::NoRole);
        auto* cancelBtn = msgBox.addButton(QMessageBox::Cancel);
        msgBox.setDefaultButton(expandBtn);
        msgBox.exec();

        if (msgBox.clickedButton() == cancelBtn)
            return;

        if (msgBox.clickedButton() == expandBtn) {
            int newW = qMax(img.width(), doc->width());
            int newH = qMax(img.height(), doc->height());

            // Build expanded document (top-left anchor)
            auto newDoc = std::make_unique<Document>(newW, newH);
            for (int i = 0; i < doc->layerCount(); ++i) {
                auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
                if (!layer) continue;

                Surface expanded(newW, newH);
                ColorBgra fill = (i == 0)
                    ? ColorBgra::fromBgra(255, 255, 255, 255)
                    : ColorBgra::fromBgra(0, 0, 0, 0);
                int oldW = layer->surface().width();
                int oldH = layer->surface().height();
                // Copy existing rows and fill only the new right-side pixels
                for (int y = 0; y < oldH; ++y) {
                    std::memcpy(expanded.rowPtr(y), layer->surface().rowPtr(y),
                                oldW * sizeof(ColorBgra));
                    ColorBgra* row = expanded.rowPtr(y);
                    for (int x = oldW; x < newW; ++x) row[x] = fill;
                }
                // Fill entirely new rows below
                for (int y = oldH; y < newH; ++y) {
                    ColorBgra* row = expanded.rowPtr(y);
                    for (int x = 0; x < newW; ++x) row[x] = fill;
                }
                auto newLayer = std::make_unique<BitmapLayer>(std::move(expanded));
                newLayer->setName(layer->name());
                newLayer->setVisible(layer->isVisible());
                newLayer->setOpacity(layer->opacity());
                newDoc->addLayer(std::move(newLayer));
            }

            int activeIdx = m_workspace->activeLayerIndex();
            auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(
                std::move(newDoc), activeIdx);

            using ReplaceDocFn = std::function<std::pair<std::unique_ptr<Document>, int>(
                std::unique_ptr<Document>, int)>;
            ReplaceDocFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
                auto result = m_workspace->replaceDocumentForHistory(std::move(d), idx);
                m_workspace->canvas()->zoomToFit();
                return result;
            };
            auto canvasMemento = std::make_unique<ReplaceDocumentMemento>(
                tr("Expand Canvas"), replaceFn, std::move(oldDoc), oldActiveIdx);
            m_workspace->historyStack()->pushNewMemento(std::move(canvasMemento));

            m_sizeLabel->setText(tr("Size: %1 x %2").arg(newW).arg(newH));
            doc = m_workspace->document();
            m_workspace->canvas()->zoomToFit();
        }
    }

    // Commit any existing floating overlay first (stamp the previous paste)
    if (m_workspace->overlaySurface()) {
        // Tell MoveTool to forget its overlay (don't let deactivate re-commit)
        auto* moveTool = dynamic_cast<MoveTool*>(m_workspace->activeTool());

        auto* layer = dynamic_cast<BitmapLayer*>(
            doc->layerAt(m_workspace->activeLayerIndex()));
        if (layer) {
            QPoint offset = m_workspace->overlayOffset();
            const Surface* ovl = m_workspace->overlaySurface();
            QRect overlayBounds(offset, QSize(ovl->width(), ovl->height()));
            QRegion region(overlayBounds.intersected(layer->surface().bounds()));

            auto bmpMemento = std::make_unique<BitmapHistoryMemento>(
                tr("Commit Paste"), doc, m_workspace->activeLayerIndex(), region);
            m_workspace->commitOverlay();
            m_workspace->historyStack()->pushNewMemento(std::move(bmpMemento));
        } else {
            m_workspace->clearOverlay();
        }

        // Sync MoveTool state — overlay is gone now
        if (moveTool) {
            moveTool->adoptOverlay(); // overlay is null → m_lifted = false
        }
    }

    auto* sel = m_workspace->selection();

    // Create undo memento BEFORE applying paste (captures current selection + overlay state)
    auto memento = std::make_unique<PasteHistoryMemento>(tr("Paste"), sel, m_workspace);

    // Create a floating overlay surface from the clipboard image (move to avoid copy)
    auto overlay = std::make_unique<Surface>(std::move(img));

    // If clipboard image doesn't match our saved copy size, it's from an external source
    if (overlay->width() != m_copySize.width() || overlay->height() != m_copySize.height()) {
        m_copyOrigin = QPoint(0, 0);
        m_copySelectionPath = QPainterPath();
    }

    // Use copy origin if it fits within the canvas, otherwise center the paste
    int offsetX = m_copyOrigin.x();
    int offsetY = m_copyOrigin.y();
    if (offsetX + overlay->width() > doc->width() ||
        offsetY + overlay->height() > doc->height() ||
        offsetX < 0 || offsetY < 0) {
        offsetX = (doc->width() - overlay->width()) / 2;
        offsetY = (doc->height() - overlay->height()) / 2;
    }

    // Restore the original selection shape, translated to match the paste offset
    if (m_copySelectionPath.isEmpty()) {
        QPainterPath pastePath;
        pastePath.addRect(QRect(offsetX, offsetY, overlay->width(), overlay->height()));
        sel->setPath(pastePath);
    } else {
        // Translate the saved path from original coords to the new paste offset
        int dx = offsetX - m_copyOrigin.x();
        int dy = offsetY - m_copyOrigin.y();
        sel->setPath(m_copySelectionPath.translated(dx, dy));
    }

    // Set the overlay — user can move it before committing
    m_workspace->setOverlay(std::move(overlay), QPoint(offsetX, offsetY));

    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    // Switch to move tool so user can reposition the paste
    for (int i = 0; i < static_cast<int>(m_tools.size()); ++i) {
        auto* moveTool = dynamic_cast<MoveTool*>(m_tools[i].get());
        if (moveTool) {
            if (m_workspace->activeTool() != moveTool) {
                m_workspace->setActiveTool(moveTool);
            } else {
                // Already active — just adopt the new overlay without commit
                moveTool->adoptOverlay();
            }
            m_toolsDock->selectTool(i);
            break;
        }
    }
}

// --- Image menu actions ---

void MainWindow::updateImageMenuState() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    m_flattenAction->setEnabled(doc->layerCount() > 1);
    auto* sel = m_workspace->selection();
    bool hasSel = sel && !sel->isEmpty();
    m_cropAction->setEnabled(hasSel);
    m_zoomToSelAction->setEnabled(hasSel);
}

// --- Layer menu actions ---

void MainWindow::onImportFromFile() {
    QString filePath = QFileDialog::getOpenFileName(this, tr("Import From File"),
                                                     QString(), openFileFilter(),
                                                     nullptr, QFileDialog::DontUseNativeDialog);
    if (filePath.isEmpty()) return;
    importFromPath(filePath);
}

void MainWindow::importFromPath(const QString& filePath) {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QString error;
    auto importedDoc = loadDocument(filePath, &error);
    if (!importedDoc) {
        QMessageBox::warning(this, tr("Import Error"), error);
        return;
    }

    int oldActive = m_workspace->activeLayerIndex();
    QString baseName = QFileInfo(filePath).completeBaseName();

    for (int i = 0; i < importedDoc->layerCount(); ++i) {
        auto* srcLayer = dynamic_cast<BitmapLayer*>(importedDoc->layerAt(i));
        if (!srcLayer) continue;

        // Pad or crop imported layer to match document size
        Surface padded(doc->width(), doc->height());
        padded.copySurface(srcLayer->surface());

        auto newLayer = std::make_unique<BitmapLayer>(std::move(padded));
        if (importedDoc->layerCount() == 1)
            newLayer->setName(baseName);
        else
            newLayer->setName(tr("%1 - %2").arg(baseName, srcLayer->name()));

        int insertIdx = m_workspace->activeLayerIndex() + 1;
        doc->insertLayer(insertIdx, std::move(newLayer));
        m_workspace->setActiveLayerIndex(insertIdx);

        SetActiveIndexFn setActiveFn = [this](int idx) { m_workspace->setActiveLayerIndex(idx); };
        auto memento = std::make_unique<AddLayerMemento>(
            tr("Import Layer"), doc, insertIdx, setActiveFn, oldActive);
        m_workspace->historyStack()->pushNewMemento(std::move(memento));
    }

    m_workspace->invalidateAll();
}

static void applyRotateZoom(const Surface& src, Surface& dst,
                            double angleDeg, double zoom, double panX, double panY,
                            bool tile, bool keepBackground) {
    int w = dst.width(), h = dst.height();
    int sw = src.width(), sh = src.height();
    double cx = w / 2.0, cy = h / 2.0;
    double rad = angleDeg * M_PI / 180.0;
    double cosA = std::cos(rad), sinA = std::sin(rad);
    double invZoom = 1.0 / std::max(zoom, 0.001);

    for (int dy = 0; dy < h; ++dy) {
        ColorBgra* dstRow = dst.rowPtr(dy);
        for (int dx = 0; dx < w; ++dx) {
            double rx = (dx - cx) * invZoom;
            double ry = (dy - cy) * invZoom;
            float srcX = static_cast<float>(rx * cosA + ry * sinA + cx - panX);
            float srcY = static_cast<float>(-rx * sinA + ry * cosA + cy - panY);

            if (tile) {
                // Wrap coordinates into source bounds
                srcX = std::fmod(srcX, static_cast<float>(sw));
                srcY = std::fmod(srcY, static_cast<float>(sh));
                if (srcX < 0) srcX += sw;
                if (srcY < 0) srcY += sh;
                dstRow[dx] = src.getBilinearSampleClamped(srcX, srcY);
            } else {
                ColorBgra sampled = src.getBilinearSample(srcX, srcY);
                if (keepBackground && sampled.a < 255) {
                    // Blend transformed pixel over original
                    ColorBgra bg = src.getPoint(dx, dy);
                    uint8_t sa = sampled.a;
                    uint8_t inv = 255 - sa;
                    dstRow[dx] = ColorBgra::fromBgra(
                        static_cast<uint8_t>((sampled.b * sa + bg.b * inv) / 255),
                        static_cast<uint8_t>((sampled.g * sa + bg.g * inv) / 255),
                        static_cast<uint8_t>((sampled.r * sa + bg.r * inv) / 255),
                        static_cast<uint8_t>((sampled.a * sa + bg.a * inv) / 255));
                } else {
                    dstRow[dx] = sampled;
                }
            }
        }
    }
}

void MainWindow::onRotateZoom() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rotate / Zoom"));
    auto* form = new QFormLayout(&dlg);

    // Angle
    auto* angleSpin = new QDoubleSpinBox;
    angleSpin->setRange(-360.0, 360.0);
    angleSpin->setValue(0.0);
    angleSpin->setSuffix(tr("\u00B0"));
    angleSpin->setDecimals(1);
    auto* angleSlider = new QSlider(Qt::Horizontal);
    angleSlider->setRange(-3600, 3600);
    angleSlider->setValue(0);
    auto* angleRow = new QHBoxLayout;
    angleRow->addWidget(angleSlider);
    angleRow->addWidget(angleSpin);
    form->addRow(tr("Angle:"), angleRow);
    connect(angleSlider, &QSlider::valueChanged, &dlg, [angleSpin](int v) {
        angleSpin->setValue(v / 10.0);
    });
    connect(angleSpin, &QDoubleSpinBox::valueChanged, &dlg, [angleSlider](double v) {
        angleSlider->setValue(static_cast<int>(v * 10));
    });

    // Zoom — exponential slider: zoom = 2^((slider - 512) / 128), so 512 = 1.0x center
    auto* zoomSpin = new QDoubleSpinBox;
    zoomSpin->setRange(0.01, 32.0);
    zoomSpin->setValue(1.0);
    zoomSpin->setSingleStep(0.1);
    zoomSpin->setDecimals(2);
    auto* zoomSlider = new QSlider(Qt::Horizontal);
    zoomSlider->setRange(0, 1024);
    zoomSlider->setValue(512); // center = 1.0x
    bool zoomUpdating = false;
    auto* zoomRow = new QHBoxLayout;
    zoomRow->addWidget(zoomSlider);
    zoomRow->addWidget(zoomSpin);
    form->addRow(tr("Zoom:"), zoomRow);
    connect(zoomSlider, &QSlider::valueChanged, &dlg, [&zoomUpdating, zoomSpin](int v) {
        if (zoomUpdating) return;
        zoomUpdating = true;
        zoomSpin->setValue(std::pow(2.0, (v - 512) / 128.0));
        zoomUpdating = false;
    });
    connect(zoomSpin, &QDoubleSpinBox::valueChanged, &dlg, [&zoomUpdating, zoomSlider](double v) {
        if (zoomUpdating) return;
        zoomUpdating = true;
        zoomSlider->setValue(static_cast<int>(512 + 128.0 * std::log2(std::max(v, 0.01))));
        zoomUpdating = false;
    });

    // Pan X/Y
    auto* panXSpin = new QDoubleSpinBox;
    panXSpin->setRange(-10000.0, 10000.0);
    panXSpin->setValue(0.0);
    panXSpin->setDecimals(1);
    form->addRow(tr("Pan X:"), panXSpin);

    auto* panYSpin = new QDoubleSpinBox;
    panYSpin->setRange(-10000.0, 10000.0);
    panYSpin->setValue(0.0);
    panYSpin->setDecimals(1);
    form->addRow(tr("Pan Y:"), panYSpin);

    // Options
    auto* tileCheck = new QCheckBox(tr("Tile source"));
    form->addRow(tileCheck);

    auto* keepBgCheck = new QCheckBox(tr("Keep background"));
    form->addRow(keepBgCheck);

    // Live preview
    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        applyRotateZoom(original, layer->surface(),
                        angleSpin->value(), zoomSpin->value(),
                        panXSpin->value(), panYSpin->value(),
                        tileCheck->isChecked(), keepBgCheck->isChecked());
        m_workspace->invalidateAll();
    });
    auto startPreview = [previewTimer]() { previewTimer->start(); };
    connect(angleSpin, &QDoubleSpinBox::valueChanged, &dlg, startPreview);
    connect(zoomSpin, &QDoubleSpinBox::valueChanged, &dlg, startPreview);
    connect(panXSpin, &QDoubleSpinBox::valueChanged, &dlg, startPreview);
    connect(panYSpin, &QDoubleSpinBox::valueChanged, &dlg, startPreview);
    connect(tileCheck, &QCheckBox::toggled, &dlg, startPreview);
    connect(keepBgCheck, &QCheckBox::toggled, &dlg, startPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Rotate / Zoom"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onLayerFlipHorizontal() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();
    QImage flipped = layer->surface().qimage().mirrored(true, false);
    layer->surface().copySurface(Surface(flipped));
    m_workspace->invalidateAll();

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Flip Layer Horizontal"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onLayerFlipVertical() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();
    QImage flipped = layer->surface().qimage().mirrored(false, true);
    layer->surface().copySurface(Surface(flipped));
    m_workspace->invalidateAll();

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Flip Layer Vertical"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onRemapChannel(int srcChannel, int dstChannel, bool transfer) {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    // Channel enum: R=0, G=1, B=2, A=3
    // ColorBgra memory layout: B=0, G=1, R=2, A=3
    static constexpr int byteOffset[] = {2, 1, 0, 3};
    const int srcOff = byteOffset[srcChannel];
    const int dstOff = byteOffset[dstChannel];

    auto& surf = layer->surface();
    for (int y = 0; y < surf.height(); ++y) {
        auto* row = reinterpret_cast<uint8_t*>(surf.rowPtr(y));
        for (int x = 0; x < surf.width(); ++x) {
            row[x * 4 + dstOff] = row[x * 4 + srcOff];
            if (transfer)
                row[x * 4 + srcOff] = 0;
        }
    }

    m_workspace->invalidateAll();

    static const QStringList names = {tr("Red"), tr("Green"), tr("Blue"), tr("Alpha")};
    const QString verb = transfer ? tr("Move") : tr("Copy");
    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("%1 %2 → %3").arg(verb, names[srcChannel], names[dstChannel]),
        doc, idx, QRegion(surf.bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

// --- Image menu actions ---

void MainWindow::onResize() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Resize Image"));
    auto* form = new QFormLayout(&dlg);

    int origW = doc->width();
    int origH = doc->height();
    double aspect = static_cast<double>(origW) / origH;
    bool updating = false;

    // Percentage controls
    auto* pctWidthSpin = new QSpinBox;
    pctWidthSpin->setRange(1, 10000);
    pctWidthSpin->setValue(100);
    pctWidthSpin->setSuffix(tr("%"));
    form->addRow(tr("Width %:"), pctWidthSpin);

    auto* pctHeightSpin = new QSpinBox;
    pctHeightSpin->setRange(1, 10000);
    pctHeightSpin->setValue(100);
    pctHeightSpin->setSuffix(tr("%"));
    form->addRow(tr("Height %:"), pctHeightSpin);

    // Pixel controls
    auto* widthSpin = new QSpinBox;
    widthSpin->setRange(1, 10000);
    widthSpin->setValue(origW);
    widthSpin->setSuffix(tr(" px"));
    form->addRow(tr("Width:"), widthSpin);

    auto* heightSpin = new QSpinBox;
    heightSpin->setRange(1, 10000);
    heightSpin->setValue(origH);
    heightSpin->setSuffix(tr(" px"));
    form->addRow(tr("Height:"), heightSpin);

    auto* constrainCheck = new QCheckBox(tr("Maintain aspect ratio"));
    constrainCheck->setChecked(true);
    form->addRow(constrainCheck);

    // Percent -> pixels
    connect(pctWidthSpin, &QSpinBox::valueChanged, &dlg, [&](int pct) {
        if (updating) return;
        updating = true;
        int w = qMax(1, static_cast<int>(origW * pct / 100.0 + 0.5));
        widthSpin->setValue(w);
        if (constrainCheck->isChecked()) {
            pctHeightSpin->setValue(pct);
            heightSpin->setValue(qMax(1, static_cast<int>(w / aspect + 0.5)));
        }
        updating = false;
    });
    connect(pctHeightSpin, &QSpinBox::valueChanged, &dlg, [&](int pct) {
        if (updating) return;
        updating = true;
        int h = qMax(1, static_cast<int>(origH * pct / 100.0 + 0.5));
        heightSpin->setValue(h);
        if (constrainCheck->isChecked()) {
            pctWidthSpin->setValue(pct);
            widthSpin->setValue(qMax(1, static_cast<int>(h * aspect + 0.5)));
        }
        updating = false;
    });

    // Pixels -> percent
    connect(widthSpin, &QSpinBox::valueChanged, &dlg, [&](int w) {
        if (updating) return;
        updating = true;
        pctWidthSpin->setValue(qMax(1, static_cast<int>(w * 100.0 / origW + 0.5)));
        if (constrainCheck->isChecked()) {
            int h = qMax(1, static_cast<int>(w / aspect + 0.5));
            heightSpin->setValue(h);
            pctHeightSpin->setValue(qMax(1, static_cast<int>(h * 100.0 / origH + 0.5)));
        }
        updating = false;
    });
    connect(heightSpin, &QSpinBox::valueChanged, &dlg, [&](int h) {
        if (updating) return;
        updating = true;
        pctHeightSpin->setValue(qMax(1, static_cast<int>(h * 100.0 / origH + 0.5)));
        if (constrainCheck->isChecked()) {
            int w = qMax(1, static_cast<int>(h * aspect + 0.5));
            widthSpin->setValue(w);
            pctWidthSpin->setValue(qMax(1, static_cast<int>(w * 100.0 / origW + 0.5)));
        }
        updating = false;
    });

    auto* resampleCombo = new QComboBox;
    resampleCombo->addItem(tr("Bicubic"), static_cast<int>(ResamplingAlgorithm::Bicubic));
    resampleCombo->addItem(tr("Bilinear"), static_cast<int>(ResamplingAlgorithm::Bilinear));
    resampleCombo->addItem(tr("Nearest Neighbor"), static_cast<int>(ResamplingAlgorithm::NearestNeighbor));
    resampleCombo->addItem(tr("Super Sampling (Best quality)"), static_cast<int>(ResamplingAlgorithm::SuperSampling));
    resampleCombo->setCurrentIndex(3); // default to SuperSampling
    form->addRow(tr("Resampling:"), resampleCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    int newW = widthSpin->value();
    int newH = heightSpin->value();
    if (newW == doc->width() && newH == doc->height()) return;

    auto algorithm = static_cast<ResamplingAlgorithm>(resampleCombo->currentData().toInt());

    // Build new document with resampled layers
    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        Surface resampled(newW, newH);
        resampled.fitSurface(algorithm, layer->surface());
        auto newLayer = std::make_unique<BitmapLayer>(std::move(resampled));
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    // Swap document, capture old for undo
    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Resize Image"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    m_workspace->canvas()->zoomToFit();
}

void MainWindow::onCanvasSize() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Canvas Size"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    layout->addLayout(form);

    auto* widthSpin = new QSpinBox;
    widthSpin->setRange(1, 10000);
    widthSpin->setValue(doc->width());
    form->addRow(tr("Width:"), widthSpin);

    auto* heightSpin = new QSpinBox;
    heightSpin->setRange(1, 10000);
    heightSpin->setValue(doc->height());
    form->addRow(tr("Height:"), heightSpin);

    // 3x3 anchor grid
    auto* anchorGroup = new QGroupBox(tr("Anchor"));
    auto* anchorGrid = new QGridLayout(anchorGroup);
    QRadioButton* anchorBtns[9];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            int idx = r * 3 + c;
            anchorBtns[idx] = new QRadioButton;
            anchorGrid->addWidget(anchorBtns[idx], r, c);
        }
    }
    // Arrow lookup: arrows[dr+1][dc+1] points from cell toward anchor
    // dr = anchorRow - cellRow, dc = anchorCol - cellCol
    const QString arrows[3][3] = {
        { QStringLiteral("\u2196"), QStringLiteral("\u2191"), QStringLiteral("\u2197") },  // ↖ ↑ ↗
        { QStringLiteral("\u2190"), QStringLiteral("\u25C6"), QStringLiteral("\u2192") },  // ← ◆ →
        { QStringLiteral("\u2199"), QStringLiteral("\u2193"), QStringLiteral("\u2198") },  // ↙ ↓ ↘
    };

    auto updateArrows = [&anchorBtns, &arrows]() {
        int sel = 4;
        for (int i = 0; i < 9; ++i) {
            if (anchorBtns[i]->isChecked()) { sel = i; break; }
        }
        int ar = sel / 3, ac = sel % 3;
        for (int i = 0; i < 9; ++i) {
            int r = i / 3, c = i % 3;
            int dr = r - ar, dc = c - ac;
            // Clamp to -1..1 for arrow direction
            dr = (dr > 0) ? 1 : (dr < 0) ? -1 : 0;
            dc = (dc > 0) ? 1 : (dc < 0) ? -1 : 0;
            anchorBtns[i]->setText(arrows[dr + 1][dc + 1]);
        }
    };

    anchorBtns[4]->setChecked(true);
    updateArrows();

    for (int i = 0; i < 9; ++i) {
        connect(anchorBtns[i], &QRadioButton::toggled, &dlg, [updateArrows](bool checked) {
            if (checked) updateArrows();
        });
    }
    layout->addWidget(anchorGroup);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    int newW = widthSpin->value();
    int newH = heightSpin->value();
    if (newW == doc->width() && newH == doc->height()) return;

    // Determine anchor position
    int anchorIdx = 4;
    for (int i = 0; i < 9; ++i) {
        if (anchorBtns[i]->isChecked()) { anchorIdx = i; break; }
    }
    int anchorCol = anchorIdx % 3; // 0=left, 1=center, 2=right
    int anchorRow = anchorIdx / 3; // 0=top, 1=middle, 2=bottom

    int offsetX = 0, offsetY = 0;
    if (anchorCol == 1) offsetX = (newW - doc->width()) / 2;
    else if (anchorCol == 2) offsetX = newW - doc->width();
    if (anchorRow == 1) offsetY = (newH - doc->height()) / 2;
    else if (anchorRow == 2) offsetY = newH - doc->height();

    // Build new document with repositioned layers
    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        auto newLayer = std::make_unique<BitmapLayer>(newW, newH, ColorBgra::white());
        newLayer->surface().copySurface(layer->surface(), QPoint(offsetX, offsetY),
                                        layer->surface().bounds());
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Canvas Size"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    m_workspace->canvas()->zoomToFit();
}

void MainWindow::onCropToSelection() {
    auto* doc = m_workspace->document();
    auto* sel = m_workspace->selection();
    if (!doc || !sel || sel->isEmpty()) return;

    QRect cropRect = sel->path().boundingRect().toAlignedRect().intersected(doc->bounds());
    if (cropRect.isEmpty()) return;

    int newW = cropRect.width();
    int newH = cropRect.height();

    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        auto newLayer = std::make_unique<BitmapLayer>(newW, newH);
        newLayer->surface().copySurface(layer->surface(), QPoint(0, 0), cropRect);
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Crop to Selection"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onFlipHorizontal() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    // Apply the flip
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;
        QImage mirrored = layer->surface().qimage().mirrored(true, false);
        layer->surface() = Surface(std::move(mirrored));
    }
    m_workspace->invalidateAll();

    auto memento = std::make_unique<FlipMemento>(
        tr("Flip Horizontal"), doc, FlipDirection::Horizontal);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onFlipVertical() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;
        QImage mirrored = layer->surface().qimage().mirrored(false, true);
        layer->surface() = Surface(std::move(mirrored));
    }
    m_workspace->invalidateAll();

    auto memento = std::make_unique<FlipMemento>(
        tr("Flip Vertical"), doc, FlipDirection::Vertical);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onRotate90CW() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QTransform rot;
    rot.rotate(90);

    int newW = doc->height();
    int newH = doc->width();

    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        QImage rotated = layer->surface().qimage().transformed(rot, Qt::SmoothTransformation);
        auto newLayer = std::make_unique<BitmapLayer>(Surface(std::move(rotated)));
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Rotate 90\u00B0 CW"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onRotate90CCW() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QTransform rot;
    rot.rotate(-90);

    int newW = doc->height();
    int newH = doc->width();

    auto newDoc = std::make_unique<Document>(newW, newH);
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        QImage rotated = layer->surface().qimage().transformed(rot, Qt::SmoothTransformation);
        auto newLayer = std::make_unique<BitmapLayer>(Surface(std::move(rotated)));
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Rotate 90\u00B0 CCW"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onRotate180() {
    auto* doc = m_workspace->document();
    if (!doc) return;

    QTransform rot;
    rot.rotate(180);

    auto newDoc = std::make_unique<Document>(doc->width(), doc->height());
    for (int i = 0; i < doc->layerCount(); ++i) {
        auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(i));
        if (!layer) continue;

        QImage rotated = layer->surface().qimage().transformed(rot, Qt::SmoothTransformation);
        auto newLayer = std::make_unique<BitmapLayer>(Surface(std::move(rotated)));
        newLayer->setName(layer->name());
        newLayer->setVisible(layer->isVisible());
        newLayer->setOpacity(layer->opacity());
        newDoc->addLayer(std::move(newLayer));
    }

    int activeIdx = m_workspace->activeLayerIndex();
    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), activeIdx);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Rotate 180\u00B0"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onFlattenImage() {
    auto* doc = m_workspace->document();
    if (!doc || doc->layerCount() <= 1) return;

    // Flatten: render all layers into one
    Surface flattened = doc->flatten();
    auto newDoc = std::make_unique<Document>(doc->width(), doc->height());
    auto newLayer = std::make_unique<BitmapLayer>(std::move(flattened));
    newLayer->setName(tr("Flattened"));
    newDoc->addLayer(std::move(newLayer));

    auto [oldDoc, oldActiveIdx] = m_workspace->replaceDocumentForHistory(std::move(newDoc), 0);

    ReplaceDocumentFn replaceFn = [this](std::unique_ptr<Document> d, int idx) {
        return m_workspace->replaceDocumentForHistory(std::move(d), idx);
    };
    auto memento = std::make_unique<ReplaceDocumentMemento>(
        tr("Flatten Image"), replaceFn, std::move(oldDoc), oldActiveIdx);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

// --- Adjustments menu actions ---

void MainWindow::onInvertColors() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Invert Colors"), doc, idx, QRegion(layer->surface().bounds()));
    adjustInvertColors(layer->surface());
    m_workspace->invalidateAll();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onGrayscale() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Grayscale"), doc, idx, QRegion(layer->surface().bounds()));
    adjustGrayscale(layer->surface());
    m_workspace->invalidateAll();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onSepia() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Sepia"), doc, idx, QRegion(layer->surface().bounds()));
    adjustSepia(layer->surface());
    m_workspace->invalidateAll();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onAutoLevel() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Auto-Level"), doc, idx, QRegion(layer->surface().bounds()));
    adjustAutoLevel(layer->surface());
    m_workspace->invalidateAll();
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onBrightnessContrast() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Brightness / Contrast"));
    auto* form = new QFormLayout(&dlg);

    auto* brightSlider = new QSlider(Qt::Horizontal);
    brightSlider->setRange(-100, 100);
    auto* brightSpin = new QSpinBox;
    brightSpin->setRange(-100, 100);
    auto* brightRow = new QHBoxLayout;
    brightRow->addWidget(brightSlider);
    brightRow->addWidget(brightSpin);
    form->addRow(tr("Brightness:"), brightRow);
    connect(brightSlider, &QSlider::valueChanged, brightSpin, &QSpinBox::setValue);
    connect(brightSpin, &QSpinBox::valueChanged, brightSlider, &QSlider::setValue);

    auto* contrastSlider = new QSlider(Qt::Horizontal);
    contrastSlider->setRange(-100, 100);
    auto* contrastSpin = new QSpinBox;
    contrastSpin->setRange(-100, 100);
    auto* contrastRow = new QHBoxLayout;
    contrastRow->addWidget(contrastSlider);
    contrastRow->addWidget(contrastSpin);
    form->addRow(tr("Contrast:"), contrastRow);
    connect(contrastSlider, &QSlider::valueChanged, contrastSpin, &QSpinBox::setValue);
    connect(contrastSpin, &QSpinBox::valueChanged, contrastSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        adjustBrightnessContrast(original, layer->surface(), brightSpin->value(), contrastSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(brightSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(contrastSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Brightness/Contrast"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onHueSaturation() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Hue / Saturation"));
    auto* form = new QFormLayout(&dlg);

    auto* hueSlider = new QSlider(Qt::Horizontal);
    hueSlider->setRange(-180, 180);
    auto* hueSpin = new QSpinBox;
    hueSpin->setRange(-180, 180);
    auto* hueRow = new QHBoxLayout;
    hueRow->addWidget(hueSlider);
    hueRow->addWidget(hueSpin);
    form->addRow(tr("Hue:"), hueRow);
    connect(hueSlider, &QSlider::valueChanged, hueSpin, &QSpinBox::setValue);
    connect(hueSpin, &QSpinBox::valueChanged, hueSlider, &QSlider::setValue);

    auto* satSlider = new QSlider(Qt::Horizontal);
    satSlider->setRange(0, 200);
    satSlider->setValue(100);
    auto* satSpin = new QSpinBox;
    satSpin->setRange(0, 200);
    satSpin->setValue(100);
    auto* satRow = new QHBoxLayout;
    satRow->addWidget(satSlider);
    satRow->addWidget(satSpin);
    form->addRow(tr("Saturation:"), satRow);
    connect(satSlider, &QSlider::valueChanged, satSpin, &QSpinBox::setValue);
    connect(satSpin, &QSpinBox::valueChanged, satSlider, &QSlider::setValue);

    auto* lightSlider = new QSlider(Qt::Horizontal);
    lightSlider->setRange(-100, 100);
    auto* lightSpin = new QSpinBox;
    lightSpin->setRange(-100, 100);
    auto* lightRow = new QHBoxLayout;
    lightRow->addWidget(lightSlider);
    lightRow->addWidget(lightSpin);
    form->addRow(tr("Lightness:"), lightRow);
    connect(lightSlider, &QSlider::valueChanged, lightSpin, &QSpinBox::setValue);
    connect(lightSpin, &QSpinBox::valueChanged, lightSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        adjustHueSaturationLightness(original, layer->surface(),
            hueSpin->value(), satSpin->value(), lightSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(hueSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(satSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(lightSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Hue/Saturation"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onPosterize() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Posterize"));
    auto* form = new QFormLayout(&dlg);

    auto* redSlider = new QSlider(Qt::Horizontal);
    redSlider->setRange(2, 64);
    redSlider->setValue(16);
    auto* redSpin = new QSpinBox;
    redSpin->setRange(2, 64);
    redSpin->setValue(16);
    auto* redRow = new QHBoxLayout;
    redRow->addWidget(redSlider);
    redRow->addWidget(redSpin);
    form->addRow(tr("Red:"), redRow);
    connect(redSlider, &QSlider::valueChanged, redSpin, &QSpinBox::setValue);
    connect(redSpin, &QSpinBox::valueChanged, redSlider, &QSlider::setValue);

    auto* greenSlider = new QSlider(Qt::Horizontal);
    greenSlider->setRange(2, 64);
    greenSlider->setValue(16);
    auto* greenSpin = new QSpinBox;
    greenSpin->setRange(2, 64);
    greenSpin->setValue(16);
    auto* greenRow = new QHBoxLayout;
    greenRow->addWidget(greenSlider);
    greenRow->addWidget(greenSpin);
    form->addRow(tr("Green:"), greenRow);
    connect(greenSlider, &QSlider::valueChanged, greenSpin, &QSpinBox::setValue);
    connect(greenSpin, &QSpinBox::valueChanged, greenSlider, &QSlider::setValue);

    auto* blueSlider = new QSlider(Qt::Horizontal);
    blueSlider->setRange(2, 64);
    blueSlider->setValue(16);
    auto* blueSpin = new QSpinBox;
    blueSpin->setRange(2, 64);
    blueSpin->setValue(16);
    auto* blueRow = new QHBoxLayout;
    blueRow->addWidget(blueSlider);
    blueRow->addWidget(blueSpin);
    form->addRow(tr("Blue:"), blueRow);
    connect(blueSlider, &QSlider::valueChanged, blueSpin, &QSpinBox::setValue);
    connect(blueSpin, &QSpinBox::valueChanged, blueSlider, &QSlider::setValue);

    auto* linkCheck = new QCheckBox(tr("Link channels"));
    linkCheck->setChecked(true);
    form->addRow(linkCheck);

    bool linking = false;
    auto syncLinked = [&](int val) {
        if (linking || !linkCheck->isChecked()) return;
        linking = true;
        redSpin->setValue(val);
        greenSpin->setValue(val);
        blueSpin->setValue(val);
        linking = false;
    };
    connect(redSpin, &QSpinBox::valueChanged, &dlg, syncLinked);
    connect(greenSpin, &QSpinBox::valueChanged, &dlg, syncLinked);
    connect(blueSpin, &QSpinBox::valueChanged, &dlg, syncLinked);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        adjustPosterize(original, layer->surface(),
            redSpin->value(), greenSpin->value(), blueSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(redSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(greenSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(blueSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    // Apply initial preview
    previewTimer->start();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Posterize"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onLevels() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Levels"));
    auto* form = new QFormLayout(&dlg);

    auto makeSpin = [](int min, int max, int val) {
        auto* spin = new QSpinBox;
        spin->setRange(min, max);
        spin->setValue(val);
        return spin;
    };

    form->addRow(new QLabel(tr("<b>Input Levels</b>")));
    auto* inBlackR = makeSpin(0, 255, 0);
    auto* inBlackG = makeSpin(0, 255, 0);
    auto* inBlackB = makeSpin(0, 255, 0);
    auto* inBlackRow = new QHBoxLayout;
    inBlackRow->addWidget(new QLabel(tr("R:"))); inBlackRow->addWidget(inBlackR);
    inBlackRow->addWidget(new QLabel(tr("G:"))); inBlackRow->addWidget(inBlackG);
    inBlackRow->addWidget(new QLabel(tr("B:"))); inBlackRow->addWidget(inBlackB);
    form->addRow(tr("Black:"), inBlackRow);

    auto* inWhiteR = makeSpin(0, 255, 255);
    auto* inWhiteG = makeSpin(0, 255, 255);
    auto* inWhiteB = makeSpin(0, 255, 255);
    auto* inWhiteRow = new QHBoxLayout;
    inWhiteRow->addWidget(new QLabel(tr("R:"))); inWhiteRow->addWidget(inWhiteR);
    inWhiteRow->addWidget(new QLabel(tr("G:"))); inWhiteRow->addWidget(inWhiteG);
    inWhiteRow->addWidget(new QLabel(tr("B:"))); inWhiteRow->addWidget(inWhiteB);
    form->addRow(tr("White:"), inWhiteRow);

    auto makeGammaSpin = []() {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(0.1, 10.0);
        spin->setSingleStep(0.1);
        spin->setDecimals(2);
        spin->setValue(1.0);
        return spin;
    };

    auto* gammaR = makeGammaSpin();
    auto* gammaG = makeGammaSpin();
    auto* gammaB = makeGammaSpin();
    auto* gammaRow = new QHBoxLayout;
    gammaRow->addWidget(new QLabel(tr("R:"))); gammaRow->addWidget(gammaR);
    gammaRow->addWidget(new QLabel(tr("G:"))); gammaRow->addWidget(gammaG);
    gammaRow->addWidget(new QLabel(tr("B:"))); gammaRow->addWidget(gammaB);
    form->addRow(tr("Gamma:"), gammaRow);

    form->addRow(new QLabel(tr("<b>Output Levels</b>")));
    auto* outBlackR = makeSpin(0, 255, 0);
    auto* outBlackG = makeSpin(0, 255, 0);
    auto* outBlackB = makeSpin(0, 255, 0);
    auto* outBlackRow = new QHBoxLayout;
    outBlackRow->addWidget(new QLabel(tr("R:"))); outBlackRow->addWidget(outBlackR);
    outBlackRow->addWidget(new QLabel(tr("G:"))); outBlackRow->addWidget(outBlackG);
    outBlackRow->addWidget(new QLabel(tr("B:"))); outBlackRow->addWidget(outBlackB);
    form->addRow(tr("Black:"), outBlackRow);

    auto* outWhiteR = makeSpin(0, 255, 255);
    auto* outWhiteG = makeSpin(0, 255, 255);
    auto* outWhiteB = makeSpin(0, 255, 255);
    auto* outWhiteRow = new QHBoxLayout;
    outWhiteRow->addWidget(new QLabel(tr("R:"))); outWhiteRow->addWidget(outWhiteR);
    outWhiteRow->addWidget(new QLabel(tr("G:"))); outWhiteRow->addWidget(outWhiteG);
    outWhiteRow->addWidget(new QLabel(tr("B:"))); outWhiteRow->addWidget(outWhiteB);
    form->addRow(tr("White:"), outWhiteRow);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        ColorBgra inputBlack = ColorBgra::fromBgr(
            static_cast<uint8_t>(inBlackB->value()),
            static_cast<uint8_t>(inBlackG->value()),
            static_cast<uint8_t>(inBlackR->value()));
        ColorBgra inputWhite = ColorBgra::fromBgr(
            static_cast<uint8_t>(inWhiteB->value()),
            static_cast<uint8_t>(inWhiteG->value()),
            static_cast<uint8_t>(inWhiteR->value()));
        ColorBgra outputBlack = ColorBgra::fromBgr(
            static_cast<uint8_t>(outBlackB->value()),
            static_cast<uint8_t>(outBlackG->value()),
            static_cast<uint8_t>(outBlackR->value()));
        ColorBgra outputWhite = ColorBgra::fromBgr(
            static_cast<uint8_t>(outWhiteB->value()),
            static_cast<uint8_t>(outWhiteG->value()),
            static_cast<uint8_t>(outWhiteR->value()));
        adjustLevels(original, layer->surface(), inputBlack, inputWhite,
                     outputBlack, outputWhite,
                     static_cast<float>(gammaR->value()),
                     static_cast<float>(gammaG->value()),
                     static_cast<float>(gammaB->value()));
        m_workspace->invalidateAll();
    });

    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    for (auto* spin : {inBlackR, inBlackG, inBlackB, inWhiteR, inWhiteG, inWhiteB,
                       outBlackR, outBlackG, outBlackB, outWhiteR, outWhiteG, outWhiteB}) {
        connect(spin, &QSpinBox::valueChanged, &dlg, requestPreview);
    }
    auto requestPreviewD = [previewTimer](double) { previewTimer->start(); };
    connect(gammaR, &QDoubleSpinBox::valueChanged, &dlg, requestPreviewD);
    connect(gammaG, &QDoubleSpinBox::valueChanged, &dlg, requestPreviewD);
    connect(gammaB, &QDoubleSpinBox::valueChanged, &dlg, requestPreviewD);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Levels"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

// ============================================================
// Effects menu handlers
// ============================================================

void MainWindow::onGaussianBlur() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Gaussian Blur"));
    auto* form = new QFormLayout(&dlg);

    auto* radiusSlider = new QSlider(Qt::Horizontal);
    radiusSlider->setRange(1, 200);
    radiusSlider->setValue(2);
    auto* radiusSpin = new QSpinBox;
    radiusSpin->setRange(1, 200);
    radiusSpin->setValue(2);
    auto* row = new QHBoxLayout;
    row->addWidget(radiusSlider);
    row->addWidget(radiusSpin);
    form->addRow(tr("Radius:"), row);
    connect(radiusSlider, &QSlider::valueChanged, radiusSpin, &QSpinBox::setValue);
    connect(radiusSpin, &QSpinBox::valueChanged, radiusSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectGaussianBlur(original, layer->surface(), radiusSpin->value());
        m_workspace->invalidateAll();
    });
    connect(radiusSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Gaussian Blur"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onMotionBlur() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Motion Blur"));
    auto* form = new QFormLayout(&dlg);

    auto* angleSlider = new QSlider(Qt::Horizontal);
    angleSlider->setRange(0, 360);
    angleSlider->setValue(0);
    auto* angleSpin = new QSpinBox;
    angleSpin->setRange(0, 360);
    angleSpin->setValue(0);
    auto* angleRow = new QHBoxLayout;
    angleRow->addWidget(angleSlider);
    angleRow->addWidget(angleSpin);
    form->addRow(tr("Angle:"), angleRow);
    connect(angleSlider, &QSlider::valueChanged, angleSpin, &QSpinBox::setValue);
    connect(angleSpin, &QSpinBox::valueChanged, angleSlider, &QSlider::setValue);

    auto* distSlider = new QSlider(Qt::Horizontal);
    distSlider->setRange(1, 200);
    distSlider->setValue(10);
    auto* distSpin = new QSpinBox;
    distSpin->setRange(1, 200);
    distSpin->setValue(10);
    auto* distRow = new QHBoxLayout;
    distRow->addWidget(distSlider);
    distRow->addWidget(distSpin);
    form->addRow(tr("Distance:"), distRow);
    connect(distSlider, &QSlider::valueChanged, distSpin, &QSpinBox::setValue);
    connect(distSpin, &QSpinBox::valueChanged, distSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectMotionBlur(original, layer->surface(), angleSpin->value(), distSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(angleSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(distSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Motion Blur"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onUnfocus() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Unfocus"));
    auto* form = new QFormLayout(&dlg);

    auto* radiusSlider = new QSlider(Qt::Horizontal);
    radiusSlider->setRange(1, 200);
    radiusSlider->setValue(4);
    auto* radiusSpin = new QSpinBox;
    radiusSpin->setRange(1, 200);
    radiusSpin->setValue(4);
    auto* row = new QHBoxLayout;
    row->addWidget(radiusSlider);
    row->addWidget(radiusSpin);
    form->addRow(tr("Radius:"), row);
    connect(radiusSlider, &QSlider::valueChanged, radiusSpin, &QSpinBox::setValue);
    connect(radiusSpin, &QSpinBox::valueChanged, radiusSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectUnfocus(original, layer->surface(), radiusSpin->value());
        m_workspace->invalidateAll();
    });
    connect(radiusSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Unfocus"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onAddNoise() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Noise"));
    auto* form = new QFormLayout(&dlg);

    auto* intSlider = new QSlider(Qt::Horizontal);
    intSlider->setRange(1, 100);
    intSlider->setValue(64);
    auto* intSpin = new QSpinBox;
    intSpin->setRange(1, 100);
    intSpin->setValue(64);
    auto* intRow = new QHBoxLayout;
    intRow->addWidget(intSlider);
    intRow->addWidget(intSpin);
    form->addRow(tr("Intensity:"), intRow);
    connect(intSlider, &QSlider::valueChanged, intSpin, &QSpinBox::setValue);
    connect(intSpin, &QSpinBox::valueChanged, intSlider, &QSlider::setValue);

    auto* satSlider = new QSlider(Qt::Horizontal);
    satSlider->setRange(0, 400);
    satSlider->setValue(100);
    auto* satSpin = new QSpinBox;
    satSpin->setRange(0, 400);
    satSpin->setValue(100);
    auto* satRow = new QHBoxLayout;
    satRow->addWidget(satSlider);
    satRow->addWidget(satSpin);
    form->addRow(tr("Saturation:"), satRow);
    connect(satSlider, &QSlider::valueChanged, satSpin, &QSpinBox::setValue);
    connect(satSpin, &QSpinBox::valueChanged, satSlider, &QSlider::setValue);

    auto* covSlider = new QSlider(Qt::Horizontal);
    covSlider->setRange(0, 100);
    covSlider->setValue(100);
    auto* covSpin = new QSpinBox;
    covSpin->setRange(0, 100);
    covSpin->setValue(100);
    auto* covRow = new QHBoxLayout;
    covRow->addWidget(covSlider);
    covRow->addWidget(covSpin);
    form->addRow(tr("Coverage:"), covRow);
    connect(covSlider, &QSlider::valueChanged, covSpin, &QSpinBox::setValue);
    connect(covSpin, &QSpinBox::valueChanged, covSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectAddNoise(original, layer->surface(), intSpin->value(), satSpin->value(), covSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(intSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(satSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(covSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Add Noise"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onMedian() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Median"));
    auto* form = new QFormLayout(&dlg);

    auto* radiusSlider = new QSlider(Qt::Horizontal);
    radiusSlider->setRange(1, 50);
    radiusSlider->setValue(5);
    auto* radiusSpin = new QSpinBox;
    radiusSpin->setRange(1, 50);
    radiusSpin->setValue(5);
    auto* radiusRow = new QHBoxLayout;
    radiusRow->addWidget(radiusSlider);
    radiusRow->addWidget(radiusSpin);
    form->addRow(tr("Radius:"), radiusRow);
    connect(radiusSlider, &QSlider::valueChanged, radiusSpin, &QSpinBox::setValue);
    connect(radiusSpin, &QSpinBox::valueChanged, radiusSlider, &QSlider::setValue);

    auto* pctSlider = new QSlider(Qt::Horizontal);
    pctSlider->setRange(0, 100);
    pctSlider->setValue(50);
    auto* pctSpin = new QSpinBox;
    pctSpin->setRange(0, 100);
    pctSpin->setValue(50);
    auto* pctRow = new QHBoxLayout;
    pctRow->addWidget(pctSlider);
    pctRow->addWidget(pctSpin);
    form->addRow(tr("Percentile:"), pctRow);
    connect(pctSlider, &QSlider::valueChanged, pctSpin, &QSpinBox::setValue);
    connect(pctSpin, &QSpinBox::valueChanged, pctSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectMedian(original, layer->surface(), radiusSpin->value(), pctSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(radiusSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(pctSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Median"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onPixelate() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Pixelate"));
    auto* form = new QFormLayout(&dlg);

    auto* cellSlider = new QSlider(Qt::Horizontal);
    cellSlider->setRange(1, 100);
    cellSlider->setValue(8);
    auto* cellSpin = new QSpinBox;
    cellSpin->setRange(1, 100);
    cellSpin->setValue(8);
    auto* row = new QHBoxLayout;
    row->addWidget(cellSlider);
    row->addWidget(cellSpin);
    form->addRow(tr("Cell Size:"), row);
    connect(cellSlider, &QSlider::valueChanged, cellSpin, &QSpinBox::setValue);
    connect(cellSpin, &QSpinBox::valueChanged, cellSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectPixelate(original, layer->surface(), cellSpin->value());
        m_workspace->invalidateAll();
    });
    connect(cellSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Pixelate"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onEdgeDetect() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edge Detect"));
    auto* form = new QFormLayout(&dlg);

    auto* angleSlider = new QSlider(Qt::Horizontal);
    angleSlider->setRange(0, 360);
    angleSlider->setValue(45);
    auto* angleSpin = new QSpinBox;
    angleSpin->setRange(0, 360);
    angleSpin->setValue(45);
    auto* row = new QHBoxLayout;
    row->addWidget(angleSlider);
    row->addWidget(angleSpin);
    form->addRow(tr("Angle:"), row);
    connect(angleSlider, &QSlider::valueChanged, angleSpin, &QSpinBox::setValue);
    connect(angleSpin, &QSpinBox::valueChanged, angleSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectEdgeDetect(original, layer->surface(), angleSpin->value());
        m_workspace->invalidateAll();
    });
    connect(angleSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Edge Detect"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onEmboss() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Emboss"));
    auto* form = new QFormLayout(&dlg);

    auto* angleSlider = new QSlider(Qt::Horizontal);
    angleSlider->setRange(0, 360);
    angleSlider->setValue(0);
    auto* angleSpin = new QSpinBox;
    angleSpin->setRange(0, 360);
    angleSpin->setValue(0);
    auto* row = new QHBoxLayout;
    row->addWidget(angleSlider);
    row->addWidget(angleSpin);
    form->addRow(tr("Angle:"), row);
    connect(angleSlider, &QSlider::valueChanged, angleSpin, &QSpinBox::setValue);
    connect(angleSpin, &QSpinBox::valueChanged, angleSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectEmboss(original, layer->surface(), angleSpin->value());
        m_workspace->invalidateAll();
    });
    connect(angleSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Emboss"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onRelief() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Relief"));
    auto* form = new QFormLayout(&dlg);

    auto* angleSlider = new QSlider(Qt::Horizontal);
    angleSlider->setRange(0, 360);
    angleSlider->setValue(0);
    auto* angleSpin = new QSpinBox;
    angleSpin->setRange(0, 360);
    angleSpin->setValue(0);
    auto* row = new QHBoxLayout;
    row->addWidget(angleSlider);
    row->addWidget(angleSpin);
    form->addRow(tr("Angle:"), row);
    connect(angleSlider, &QSlider::valueChanged, angleSpin, &QSpinBox::setValue);
    connect(angleSpin, &QSpinBox::valueChanged, angleSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectRelief(original, layer->surface(), angleSpin->value());
        m_workspace->invalidateAll();
    });
    connect(angleSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Relief"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onOutline() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Outline"));
    auto* form = new QFormLayout(&dlg);

    auto* intSlider = new QSlider(Qt::Horizontal);
    intSlider->setRange(0, 100);
    intSlider->setValue(50);
    auto* intSpin = new QSpinBox;
    intSpin->setRange(0, 100);
    intSpin->setValue(50);
    auto* intRow = new QHBoxLayout;
    intRow->addWidget(intSlider);
    intRow->addWidget(intSpin);
    form->addRow(tr("Intensity:"), intRow);
    connect(intSlider, &QSlider::valueChanged, intSpin, &QSpinBox::setValue);
    connect(intSpin, &QSpinBox::valueChanged, intSlider, &QSlider::setValue);

    auto* radiusSlider = new QSlider(Qt::Horizontal);
    radiusSlider->setRange(1, 200);
    radiusSlider->setValue(3);
    auto* radiusSpin = new QSpinBox;
    radiusSpin->setRange(1, 200);
    radiusSpin->setValue(3);
    auto* radiusRow = new QHBoxLayout;
    radiusRow->addWidget(radiusSlider);
    radiusRow->addWidget(radiusSpin);
    form->addRow(tr("Thickness:"), radiusRow);
    connect(radiusSlider, &QSlider::valueChanged, radiusSpin, &QSpinBox::setValue);
    connect(radiusSpin, &QSpinBox::valueChanged, radiusSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectOutline(original, layer->surface(), intSpin->value(), radiusSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(intSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(radiusSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Outline"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onGlow() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Glow"));
    auto* form = new QFormLayout(&dlg);

    auto* radiusSlider = new QSlider(Qt::Horizontal);
    radiusSlider->setRange(1, 20);
    radiusSlider->setValue(6);
    auto* radiusSpin = new QSpinBox;
    radiusSpin->setRange(1, 20);
    radiusSpin->setValue(6);
    auto* radiusRow = new QHBoxLayout;
    radiusRow->addWidget(radiusSlider);
    radiusRow->addWidget(radiusSpin);
    form->addRow(tr("Radius:"), radiusRow);
    connect(radiusSlider, &QSlider::valueChanged, radiusSpin, &QSpinBox::setValue);
    connect(radiusSpin, &QSpinBox::valueChanged, radiusSlider, &QSlider::setValue);

    auto* brightSlider = new QSlider(Qt::Horizontal);
    brightSlider->setRange(-100, 100);
    brightSlider->setValue(10);
    auto* brightSpin = new QSpinBox;
    brightSpin->setRange(-100, 100);
    brightSpin->setValue(10);
    auto* brightRow = new QHBoxLayout;
    brightRow->addWidget(brightSlider);
    brightRow->addWidget(brightSpin);
    form->addRow(tr("Brightness:"), brightRow);
    connect(brightSlider, &QSlider::valueChanged, brightSpin, &QSpinBox::setValue);
    connect(brightSpin, &QSpinBox::valueChanged, brightSlider, &QSlider::setValue);

    auto* contrastSlider = new QSlider(Qt::Horizontal);
    contrastSlider->setRange(-100, 100);
    contrastSlider->setValue(10);
    auto* contrastSpin = new QSpinBox;
    contrastSpin->setRange(-100, 100);
    contrastSpin->setValue(10);
    auto* contrastRow = new QHBoxLayout;
    contrastRow->addWidget(contrastSlider);
    contrastRow->addWidget(contrastSpin);
    form->addRow(tr("Contrast:"), contrastRow);
    connect(contrastSlider, &QSlider::valueChanged, contrastSpin, &QSpinBox::setValue);
    connect(contrastSpin, &QSpinBox::valueChanged, contrastSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectGlow(original, layer->surface(), radiusSpin->value(), brightSpin->value(), contrastSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(radiusSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(brightSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(contrastSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Glow"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onSharpen() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Sharpen"));
    auto* form = new QFormLayout(&dlg);

    auto* amountSlider = new QSlider(Qt::Horizontal);
    amountSlider->setRange(1, 20);
    amountSlider->setValue(2);
    auto* amountSpin = new QSpinBox;
    amountSpin->setRange(1, 20);
    amountSpin->setValue(2);
    auto* row = new QHBoxLayout;
    row->addWidget(amountSlider);
    row->addWidget(amountSpin);
    form->addRow(tr("Amount:"), row);
    connect(amountSlider, &QSlider::valueChanged, amountSpin, &QSpinBox::setValue);
    connect(amountSpin, &QSpinBox::valueChanged, amountSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectSharpen(original, layer->surface(), amountSpin->value());
        m_workspace->invalidateAll();
    });
    connect(amountSpin, &QSpinBox::valueChanged, &dlg, [previewTimer]() { previewTimer->start(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Sharpen"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onOilPainting() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Oil Painting"));
    auto* form = new QFormLayout(&dlg);

    auto* brushSlider = new QSlider(Qt::Horizontal);
    brushSlider->setRange(1, 8);
    brushSlider->setValue(3);
    auto* brushSpin = new QSpinBox;
    brushSpin->setRange(1, 8);
    brushSpin->setValue(3);
    auto* brushRow = new QHBoxLayout;
    brushRow->addWidget(brushSlider);
    brushRow->addWidget(brushSpin);
    form->addRow(tr("Brush Size:"), brushRow);
    connect(brushSlider, &QSlider::valueChanged, brushSpin, &QSpinBox::setValue);
    connect(brushSpin, &QSpinBox::valueChanged, brushSlider, &QSlider::setValue);

    auto* coarseSlider = new QSlider(Qt::Horizontal);
    coarseSlider->setRange(3, 255);
    coarseSlider->setValue(50);
    auto* coarseSpin = new QSpinBox;
    coarseSpin->setRange(3, 255);
    coarseSpin->setValue(50);
    auto* coarseRow = new QHBoxLayout;
    coarseRow->addWidget(coarseSlider);
    coarseRow->addWidget(coarseSpin);
    form->addRow(tr("Coarseness:"), coarseRow);
    connect(coarseSlider, &QSlider::valueChanged, coarseSpin, &QSpinBox::setValue);
    connect(coarseSpin, &QSpinBox::valueChanged, coarseSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectOilPainting(original, layer->surface(), brushSpin->value(), coarseSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(brushSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(coarseSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Oil Painting"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onPencilSketch() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Pencil Sketch"));
    auto* form = new QFormLayout(&dlg);

    auto* sizeSlider = new QSlider(Qt::Horizontal);
    sizeSlider->setRange(1, 20);
    sizeSlider->setValue(2);
    auto* sizeSpin = new QSpinBox;
    sizeSpin->setRange(1, 20);
    sizeSpin->setValue(2);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(sizeSlider);
    sizeRow->addWidget(sizeSpin);
    form->addRow(tr("Pencil Size:"), sizeRow);
    connect(sizeSlider, &QSlider::valueChanged, sizeSpin, &QSpinBox::setValue);
    connect(sizeSpin, &QSpinBox::valueChanged, sizeSlider, &QSlider::setValue);

    auto* rangeSlider = new QSlider(Qt::Horizontal);
    rangeSlider->setRange(-20, 20);
    rangeSlider->setValue(0);
    auto* rangeSpin = new QSpinBox;
    rangeSpin->setRange(-20, 20);
    rangeSpin->setValue(0);
    auto* rangeRow = new QHBoxLayout;
    rangeRow->addWidget(rangeSlider);
    rangeRow->addWidget(rangeSpin);
    form->addRow(tr("Range:"), rangeRow);
    connect(rangeSlider, &QSlider::valueChanged, rangeSpin, &QSpinBox::setValue);
    connect(rangeSpin, &QSpinBox::valueChanged, rangeSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectPencilSketch(original, layer->surface(), sizeSpin->value(), rangeSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(sizeSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(rangeSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Pencil Sketch"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void MainWindow::onInkSketch() {
    auto* doc = m_workspace->document();
    if (!doc) return;
    int idx = m_workspace->activeLayerIndex();
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(idx));
    if (!layer) return;

    Surface original = layer->surface().clone();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Ink Sketch"));
    auto* form = new QFormLayout(&dlg);

    auto* inkSlider = new QSlider(Qt::Horizontal);
    inkSlider->setRange(0, 99);
    inkSlider->setValue(50);
    auto* inkSpin = new QSpinBox;
    inkSpin->setRange(0, 99);
    inkSpin->setValue(50);
    auto* inkRow = new QHBoxLayout;
    inkRow->addWidget(inkSlider);
    inkRow->addWidget(inkSpin);
    form->addRow(tr("Ink Outline:"), inkRow);
    connect(inkSlider, &QSlider::valueChanged, inkSpin, &QSpinBox::setValue);
    connect(inkSpin, &QSpinBox::valueChanged, inkSlider, &QSlider::setValue);

    auto* colorSlider = new QSlider(Qt::Horizontal);
    colorSlider->setRange(0, 100);
    colorSlider->setValue(50);
    auto* colorSpin = new QSpinBox;
    colorSpin->setRange(0, 100);
    colorSpin->setValue(50);
    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(colorSlider);
    colorRow->addWidget(colorSpin);
    form->addRow(tr("Coloring:"), colorRow);
    connect(colorSlider, &QSlider::valueChanged, colorSpin, &QSpinBox::setValue);
    connect(colorSpin, &QSpinBox::valueChanged, colorSlider, &QSlider::setValue);

    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(30);
    connect(previewTimer, &QTimer::timeout, &dlg, [&]() {
        effectInkSketch(original, layer->surface(), inkSpin->value(), colorSpin->value());
        m_workspace->invalidateAll();
    });
    auto requestPreview = [previewTimer]() { previewTimer->start(); };
    connect(inkSpin, &QSpinBox::valueChanged, &dlg, requestPreview);
    connect(colorSpin, &QSpinBox::valueChanged, &dlg, requestPreview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    previewTimer->start();
    if (dlg.exec() != QDialog::Accepted) {
        layer->surface().copySurface(original);
        m_workspace->invalidateAll();
        return;
    }

    auto memento = std::make_unique<BitmapHistoryMemento>(
        tr("Ink Sketch"), doc, idx, QRegion(layer->surface().bounds()), std::move(original));
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

} // namespace paintnux
