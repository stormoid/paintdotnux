#pragma once

#include <QComboBox>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <vector>

namespace paintnux {

class DocumentWorkspace;

class LayersDock : public QDockWidget {
    Q_OBJECT

public:
    explicit LayersDock(QWidget* parent = nullptr);

    void setWorkspace(DocumentWorkspace* ws);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

public slots:
    void addLayer();
    void deleteLayer();
    void duplicateLayer();
    void mergeLayerDown();
    void moveLayerUp();
    void moveLayerDown();

private:
    void refresh();
    void updateThumbnails();
    void updateButtonStates();
    void onOpacitySliderPressed();
    void onOpacitySliderReleased();
    void onOpacitySpinFinished();
    void onLayerRowClicked(int layerIndex);
    void onLayerVisibilityToggled(int layerIndex, bool visible);
    void onLayerRenamed(int layerIndex, const QString& newName);

    DocumentWorkspace* m_workspace = nullptr;
    QComboBox* m_blendModeCombo = nullptr;
    QVBoxLayout* m_layerList = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QSpinBox* m_opacitySpin = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_dupBtn = nullptr;
    QPushButton* m_mergeBtn = nullptr;
    QPushButton* m_upBtn = nullptr;
    QPushButton* m_downBtn = nullptr;
    int m_opacityDragStart = 255;
    bool m_refreshing = false;

    // Thumbnail labels indexed by layer index (for lightweight updates)
    std::vector<QLabel*> m_thumbLabels;
    QTimer* m_thumbTimer = nullptr;
};

} // namespace paintnux
