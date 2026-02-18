#pragma once

#include "ui/documentworkspace.h"
#include "ui/rulerwidget.h"
#include "ui/toolsdock.h"
#include "ui/colorsdock.h"
#include "ui/tooloptionsbar.h"
#include "ui/historydock.h"
#include "ui/layersdock.h"
#include "tools/tool.h"

#include <QDockWidget>
#include <QListWidget>
#include <QMainWindow>
#include <QLabel>
#include <QPainterPath>
#include <QScrollBar>

#include <memory>
#include <vector>

class QPrinter;
class QNetworkAccessManager;

namespace paintnux {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

    [[nodiscard]] DocumentWorkspace* workspace() const { return m_workspace; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createMenus();
    void createStatusBar();
    void createCentralArea();
    void createTools();
    void createDocks();
    void createTestDocument();

    void onZoomChanged(double zoom);
    void onCursorPosition(QPointF docPos);
    void onToolSelected(Tool* tool);
    void syncToolSettings();

    // File menu actions
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileExport();
    void onFileClose();
    void onUndo();
    void onRedo();

    // Print
    void onPrint();
    void onPrintPreview();
    void renderForPrint(QPrinter* printer);

    // Edit menu actions (selection)
    void onSelectAll();
    void onDeselect();
    void onInvertSelection();
    void onCut();
    void onCopy();
    void onPaste();
    void onDelete();
    void onFillSelection();

    // Image menu actions
    void onResize();
    void onCanvasSize();
    void onCropToSelection();
    void onFlipHorizontal();
    void onFlipVertical();
    void onRotate90CW();
    void onRotate90CCW();
    void onRotate180();
    void onFlattenImage();
    void updateImageMenuState();

    // Adjustments menu actions
    void onInvertColors();
    void onGrayscale();
    void onSepia();
    void onAutoLevel();
    void onBrightnessContrast();
    void onHueSaturation();
    void onPosterize();
    void onLevels();

    // Effects menu actions
    void onGaussianBlur();
    void onMotionBlur();
    void onUnfocus();
    void onAddNoise();
    void onMedian();
    void onPixelate();
    void onEdgeDetect();
    void onEmboss();
    void onRelief();
    void onOutline();
    void onGlow();
    void onSharpen();
    void onOilPainting();
    void onPencilSketch();
    void onInkSketch();

    // Layer menu actions
    void onImportFromFile();
    void importFromPath(const QString& filePath);
    void importFromImage(const QImage& image, const QString& name);
    void importFromImageExpand(const QImage& image, const QString& name);
    void promptAndImportImage(const QImage& image, const QString& name);
    void handleDroppedUrl(const QUrl& url);
    void onRotateZoom();

    // Dirty tracking / title
    void updateWindowTitle();
    void setDocumentDirty();
    void clearDirty();
    bool maybeSave();

    // Settings persistence
    void saveSettings();
    void restoreSettings();

    // Recent files
    void addRecentFile(const QString& filePath);
    void updateRecentFilesMenu();
    void openRecentFile(const QString& filePath);

    // Tab management
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void addNewTab(std::unique_ptr<Document> doc, const QString& filePath);
    void updateTabLabel(int index);

    // Helper to set up a loaded/new document
    void setNewDocument(std::unique_ptr<Document> doc, const QString& filePath);

    DocumentWorkspace* m_workspace;

    // File state
    QString m_currentFilePath;
    int m_savedHistoryIndex = 0;
    bool m_dirty = false;

    // Tools
    std::vector<std::unique_ptr<Tool>> m_tools;

    // Docks
    ToolsDock* m_toolsDock;
    ColorsDock* m_colorsDock;
    HistoryDock* m_historyDock;
    LayersDock* m_layersDock;
    ToolOptionsBar* m_toolOptionsBar;

    // Status bar labels
    QLabel* m_posLabel;
    QLabel* m_sizeLabel;
    QLabel* m_zoomLabel;

    // Scrollbars
    QScrollBar* m_hScrollBar;
    QScrollBar* m_vScrollBar;

    // Rulers
    RulerWidget* m_hRuler = nullptr;
    RulerWidget* m_vRuler = nullptr;
    QWidget* m_rulerCorner = nullptr;

    // Clipboard: saved selection shape and origin from copy
    QPoint m_copyOrigin{0, 0};
    QSize m_copySize;
    QPainterPath m_copySelectionPath;

    // Network (lazy-init for browser image drops)
    QNetworkAccessManager* m_networkManager = nullptr;

    // Menu actions that need enable/disable
    QAction* m_undoAction;
    QAction* m_redoAction;
    QAction* m_saveAction;
    QAction* m_flattenAction = nullptr;
    QAction* m_cropAction = nullptr;
    QAction* m_zoomToSelAction = nullptr;
    QAction* m_pixelGridAction = nullptr;
    QAction* m_rulersAction = nullptr;
    QAction* m_unitsPixelsAction = nullptr;
    QAction* m_unitsInchesAction = nullptr;
    QAction* m_unitsCmAction = nullptr;

    // Recent files
    QMenu* m_recentFilesMenu = nullptr;
    QStringList m_recentFiles;
    static constexpr int MaxRecentFiles = 10;

    // Document tabs
    struct TabSession {
        WorkspaceState state;
        QString filePath;
        int savedHistoryIndex = 0;
        bool dirty = false;
        double zoom = 1.0;
        QPointF scrollPos;
        QPixmap thumbnail;
    };
    QListWidget* m_tabList = nullptr;
    QDockWidget* m_tabsDock = nullptr;
    QTimer* m_tabThumbTimer = nullptr;
    std::vector<std::unique_ptr<TabSession>> m_tabs;
    int m_activeTab = -1;
    bool m_switchingTabs = false;

    void updateActiveTabThumbnail();
    QPixmap generateThumbnail(const Surface* surface) const;
};

} // namespace paintnux
