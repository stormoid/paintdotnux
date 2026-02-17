#include "ui/layersdock.h"
#include "ui/documentworkspace.h"
#include "data/bitmaplayer.h"
#include "history/layerhistorymemento.h"
#include "core/blendops.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QWidget>

#include <cstring>

namespace paintnux {

LayersDock::LayersDock(QWidget* parent)
    : QDockWidget(tr("Layers"), parent) {
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto* content = new QWidget;
    auto* vbox = new QVBoxLayout(content);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // Blend mode row
    auto* blendRow = new QHBoxLayout;
    blendRow->addWidget(new QLabel(tr("Blend:")));
    m_blendModeCombo = new QComboBox;
    for (int i = 0; i < blendModeCount(); ++i)
        m_blendModeCombo->addItem(QString::fromLatin1(blendModeName(static_cast<BlendMode>(i))));
    blendRow->addWidget(m_blendModeCombo, 1);
    vbox->addLayout(blendRow);

    // Opacity row
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(new QLabel(tr("Opacity:")));
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 255);
    m_opacitySlider->setValue(255);
    opacityRow->addWidget(m_opacitySlider, 1);
    m_opacitySpin = new QSpinBox;
    m_opacitySpin->setRange(0, 255);
    m_opacitySpin->setValue(255);
    m_opacitySpin->setFixedWidth(60);
    opacityRow->addWidget(m_opacitySpin);
    vbox->addLayout(opacityRow);

    // Scroll area for layer rows
    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* scrollContent = new QWidget;
    m_layerList = new QVBoxLayout(scrollContent);
    m_layerList->setContentsMargins(0, 0, 0, 0);
    m_layerList->setSpacing(1);
    m_layerList->addStretch();
    m_scrollArea->setWidget(scrollContent);
    vbox->addWidget(m_scrollArea, 1);

    // Toolbar buttons
    auto* toolbar = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+"));
    m_addBtn->setToolTip(tr("Add New Layer (Ctrl+Shift+N)"));
    m_deleteBtn = new QPushButton(tr("\u2212"));
    m_deleteBtn->setToolTip(tr("Delete Layer"));
    m_dupBtn = new QPushButton(tr("\u2398"));
    m_dupBtn->setToolTip(tr("Duplicate Layer (Ctrl+Shift+D)"));
    m_mergeBtn = new QPushButton(tr("\u2935"));
    m_mergeBtn->setToolTip(tr("Merge Layer Down (Ctrl+M)"));
    m_upBtn = new QPushButton(tr("\u2191"));
    m_upBtn->setToolTip(tr("Move Layer Up"));
    m_downBtn = new QPushButton(tr("\u2193"));
    m_downBtn->setToolTip(tr("Move Layer Down"));

    for (auto* btn : {m_addBtn, m_deleteBtn, m_dupBtn, m_mergeBtn, m_upBtn, m_downBtn}) {
        btn->setFixedSize(32, 28);
        toolbar->addWidget(btn);
    }
    toolbar->addStretch();
    vbox->addLayout(toolbar);

    setWidget(content);

    // Button connections
    connect(m_addBtn, &QPushButton::clicked, this, &LayersDock::addLayer);
    connect(m_deleteBtn, &QPushButton::clicked, this, &LayersDock::deleteLayer);
    connect(m_dupBtn, &QPushButton::clicked, this, &LayersDock::duplicateLayer);
    connect(m_mergeBtn, &QPushButton::clicked, this, &LayersDock::mergeLayerDown);
    connect(m_upBtn, &QPushButton::clicked, this, &LayersDock::moveLayerUp);
    connect(m_downBtn, &QPushButton::clicked, this, &LayersDock::moveLayerDown);

    // Thumbnail refresh timer (debounced, fires during painting)
    m_thumbTimer = new QTimer(this);
    m_thumbTimer->setSingleShot(true);
    m_thumbTimer->setInterval(200);
    connect(m_thumbTimer, &QTimer::timeout, this, &LayersDock::updateThumbnails);

    // Opacity slider connections
    connect(m_opacitySlider, &QSlider::sliderPressed, this, &LayersDock::onOpacitySliderPressed);
    connect(m_opacitySlider, &QSlider::sliderReleased, this, &LayersDock::onOpacitySliderReleased);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_refreshing || !m_workspace || !m_workspace->document()) return;
        m_refreshing = true; // prevent full refresh from the property signal chain
        m_opacitySpin->setValue(value);
        auto* layer = m_workspace->document()->layerAt(m_workspace->activeLayerIndex());
        if (layer) {
            layer->setOpacity(static_cast<uint8_t>(value));
            // setOpacity() already emits invalidated() through the signal chain
        }
        m_refreshing = false;
    });
    connect(m_opacitySpin, &QSpinBox::editingFinished, this, &LayersDock::onOpacitySpinFinished);

    // Blend mode combo connection
    connect(m_blendModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_refreshing || !m_workspace || !m_workspace->document()) return;
        auto* layer = dynamic_cast<BitmapLayer*>(
            m_workspace->document()->layerAt(m_workspace->activeLayerIndex()));
        if (!layer) return;

        BlendMode oldMode = layer->blendMode();
        BlendMode newMode = static_cast<BlendMode>(index);
        if (oldMode == newMode) return;

        layer->setBlendMode(newMode);
        m_workspace->invalidateAll();

        auto* stack = m_workspace->historyStack();
        if (!stack) return;

        auto memento = std::make_unique<LayerPropertyMemento>(
            tr("Change Blend Mode"), m_workspace->document(),
            m_workspace->activeLayerIndex(),
            LayerPropertyMemento::BlendMode,
            QVariant(static_cast<int>(oldMode)));
        stack->pushNewMemento(std::move(memento));
    });
}

void LayersDock::setWorkspace(DocumentWorkspace* ws) {
    if (m_workspace) {
        m_workspace->disconnect(this);
        if (m_workspace->document())
            m_workspace->document()->disconnect(this);
    }

    m_workspace = ws;
    if (!ws) return;

    connect(ws, &DocumentWorkspace::documentChanged, this, [this]() {
        auto* doc = m_workspace->document();
        if (doc) {
            connect(doc, &Document::layerAdded, this, [this](int) { refresh(); });
            connect(doc, &Document::layerRemoved, this, [this](int) { refresh(); });
            connect(doc, &Document::layersReordered, this, [this]() { refresh(); });
            connect(doc, &Document::layerChanged, this, [this](int) { refresh(); });
        }
        refresh();
    });
    connect(ws, &DocumentWorkspace::activeLayerChanged, this, [this](int) {
        if (!m_refreshing) refresh();
    });
    connect(ws, &DocumentWorkspace::compositionUpdated, this, [this]() {
        if (!m_thumbTimer->isActive())
            m_thumbTimer->start();
    });

    refresh();
}

void LayersDock::refresh() {
    m_refreshing = true;
    m_thumbLabels.clear();

    // Clear existing layer rows (except the stretch at the end).
    // Use deleteLater() so widgets aren't destroyed while their signals are on the call stack.
    while (m_layerList->count() > 1) {
        auto* item = m_layerList->takeAt(0);
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }

    auto* doc = m_workspace ? m_workspace->document() : nullptr;
    if (!doc) {
        updateButtonStates();
        m_refreshing = false;
        return;
    }

    int activeIndex = m_workspace->activeLayerIndex();
    m_thumbLabels.resize(doc->layerCount(), nullptr);

    // Add rows from top to bottom (highest index first)
    for (int i = doc->layerCount() - 1; i >= 0; --i) {
        Layer* layer = doc->layerAt(i);
        auto* row = new QWidget;
        row->setFixedHeight(40);
        auto* hbox = new QHBoxLayout(row);
        hbox->setContentsMargins(4, 2, 4, 2);
        hbox->setSpacing(4);

        // Thumbnail
        Surface thumb = layer->renderThumbnail(32);
        QImage thumbImg(reinterpret_cast<const uchar*>(thumb.rowPtr(0)),
                        thumb.width(), thumb.height(),
                        thumb.width() * 4, QImage::Format_ARGB32);
        auto* thumbLabel = new QLabel;
        thumbLabel->setPixmap(QPixmap::fromImage(thumbImg.copy()));
        thumbLabel->setFixedSize(32, 32);
        thumbLabel->setStyleSheet(QStringLiteral(
            "background: qchessboardpattern(8, #ccc, #fff); border: 1px solid #888;"));
        hbox->addWidget(thumbLabel);
        m_thumbLabels[i] = thumbLabel;

        // Name label (clickable, double-click to rename)
        auto* nameLabel = new QLabel(layer->name());
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        hbox->addWidget(nameLabel, 1);

        // Visibility checkbox
        auto* visCheck = new QCheckBox;
        visCheck->setChecked(layer->isVisible());
        visCheck->setToolTip(tr("Toggle visibility"));
        hbox->addWidget(visCheck);

        // Highlight active layer
        if (i == activeIndex) {
            row->setStyleSheet(QStringLiteral(
                "background-color: palette(highlight); color: palette(highlighted-text);"));
        } else {
            row->setStyleSheet(QStringLiteral("background-color: palette(base);"));
        }

        // Click to select, double-click to rename
        int layerIndex = i;
        row->setProperty("layerIndex", layerIndex);
        row->installEventFilter(this);

        // Visibility toggle
        connect(visCheck, &QCheckBox::toggled, this, [this, layerIndex](bool checked) {
            if (!m_refreshing)
                onLayerVisibilityToggled(layerIndex, checked);
        });

        // Insert before the stretch
        m_layerList->insertWidget(m_layerList->count() - 1, row);
    }

    // Update opacity and blend mode controls
    if (activeIndex >= 0 && activeIndex < doc->layerCount()) {
        int opacity = doc->layerAt(activeIndex)->opacity();
        m_opacitySlider->setValue(opacity);
        m_opacitySpin->setValue(opacity);

        auto* bmpLayer = dynamic_cast<BitmapLayer*>(doc->layerAt(activeIndex));
        if (bmpLayer)
            m_blendModeCombo->setCurrentIndex(static_cast<int>(bmpLayer->blendMode()));
    }

    updateButtonStates();
    m_refreshing = false;
}

void LayersDock::updateThumbnails() {
    auto* doc = m_workspace ? m_workspace->document() : nullptr;
    if (!doc) return;
    int count = doc->layerCount();
    if (static_cast<int>(m_thumbLabels.size()) != count) return; // stale, full refresh will fix

    for (int i = 0; i < count; ++i) {
        if (!m_thumbLabels[i]) continue;
        Layer* layer = doc->layerAt(i);
        Surface thumb = layer->renderThumbnail(32);
        QImage thumbImg(reinterpret_cast<const uchar*>(thumb.rowPtr(0)),
                        thumb.width(), thumb.height(),
                        thumb.width() * 4, QImage::Format_ARGB32);
        m_thumbLabels[i]->setPixmap(QPixmap::fromImage(thumbImg.copy()));
    }
}

void LayersDock::updateButtonStates() {
    auto* doc = m_workspace ? m_workspace->document() : nullptr;
    bool hasDoc = (doc != nullptr);
    int count = hasDoc ? doc->layerCount() : 0;
    int active = hasDoc ? m_workspace->activeLayerIndex() : 0;

    m_addBtn->setEnabled(hasDoc);
    m_deleteBtn->setEnabled(count > 1);
    m_dupBtn->setEnabled(hasDoc && count > 0);
    m_mergeBtn->setEnabled(hasDoc && active > 0); // can merge down if not bottom
    m_upBtn->setEnabled(hasDoc && active < count - 1);
    m_downBtn->setEnabled(hasDoc && active > 0);
    m_opacitySlider->setEnabled(hasDoc && count > 0);
    m_opacitySpin->setEnabled(hasDoc && count > 0);
    m_blendModeCombo->setEnabled(hasDoc && count > 0);
}

void LayersDock::onOpacitySliderPressed() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* layer = m_workspace->document()->layerAt(m_workspace->activeLayerIndex());
    if (layer)
        m_opacityDragStart = layer->opacity();
}

void LayersDock::onOpacitySliderReleased() {
    if (!m_workspace || !m_workspace->document()) return;
    int currentValue = m_opacitySlider->value();
    if (currentValue == m_opacityDragStart) return;

    auto* stack = m_workspace->historyStack();
    if (!stack) return;

    auto memento = std::make_unique<LayerPropertyMemento>(
        tr("Change Opacity"), m_workspace->document(),
        m_workspace->activeLayerIndex(),
        LayerPropertyMemento::Opacity,
        QVariant(static_cast<int>(m_opacityDragStart)));
    stack->pushNewMemento(std::move(memento));
}

void LayersDock::onOpacitySpinFinished() {
    if (m_refreshing || !m_workspace || !m_workspace->document()) return;
    int value = m_opacitySpin->value();
    auto* layer = m_workspace->document()->layerAt(m_workspace->activeLayerIndex());
    if (!layer || layer->opacity() == static_cast<uint8_t>(value)) return;

    int oldOpacity = layer->opacity();
    layer->setOpacity(static_cast<uint8_t>(value));
    m_opacitySlider->setValue(value);

    auto* stack = m_workspace->historyStack();
    if (!stack) return;

    auto memento = std::make_unique<LayerPropertyMemento>(
        tr("Change Opacity"), m_workspace->document(),
        m_workspace->activeLayerIndex(),
        LayerPropertyMemento::Opacity,
        QVariant(oldOpacity));
    stack->pushNewMemento(std::move(memento));
}

void LayersDock::onLayerRowClicked(int layerIndex) {
    if (!m_workspace) return;
    m_workspace->setActiveLayerIndex(layerIndex);
}

void LayersDock::onLayerVisibilityToggled(int layerIndex, bool visible) {
    if (!m_workspace || !m_workspace->document()) return;
    auto* layer = m_workspace->document()->layerAt(layerIndex);
    if (!layer) return;

    bool oldVisible = layer->isVisible();
    if (oldVisible == visible) return;

    layer->setVisible(visible);

    auto* stack = m_workspace->historyStack();
    if (!stack) return;

    auto memento = std::make_unique<LayerPropertyMemento>(
        tr("Toggle Visibility"), m_workspace->document(),
        layerIndex, LayerPropertyMemento::Visible,
        QVariant(oldVisible));
    stack->pushNewMemento(std::move(memento));
}

void LayersDock::onLayerRenamed(int layerIndex, const QString& newName) {
    if (!m_workspace || !m_workspace->document()) return;
    auto* layer = m_workspace->document()->layerAt(layerIndex);
    if (!layer || layer->name() == newName) return;

    QString oldName = layer->name();
    layer->setName(newName);

    auto* stack = m_workspace->historyStack();
    if (!stack) return;

    auto memento = std::make_unique<LayerPropertyMemento>(
        tr("Rename Layer"), m_workspace->document(),
        layerIndex, LayerPropertyMemento::Name,
        QVariant(oldName));
    stack->pushNewMemento(std::move(memento));
}

// --- Layer operations ---

SetActiveIndexFn LayersDock_makeSetActiveIndex(DocumentWorkspace* ws) {
    return [ws](int idx) { ws->setActiveLayerIndex(idx); };
}

void LayersDock::addLayer() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();

    int insertIndex = m_workspace->activeLayerIndex() + 1;
    auto layer = std::make_unique<BitmapLayer>(doc->width(), doc->height());
    layer->setName(tr("Layer %1").arg(doc->layerCount() + 1));

    int oldActive = m_workspace->activeLayerIndex();
    doc->insertLayer(insertIndex, std::move(layer));
    m_workspace->setActiveLayerIndex(insertIndex);

    auto memento = std::make_unique<AddLayerMemento>(
        tr("Add Layer"), doc, insertIndex,
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void LayersDock::deleteLayer() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();
    if (doc->layerCount() <= 1) return;

    int index = m_workspace->activeLayerIndex();
    int oldActive = index;
    auto removed = doc->removeLayer(index);

    int newActive = (index >= doc->layerCount()) ? doc->layerCount() - 1 : index;
    m_workspace->setActiveLayerIndex(newActive);

    auto memento = std::make_unique<DeleteLayerMemento>(
        tr("Delete Layer"), doc, index, std::move(removed),
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void LayersDock::duplicateLayer() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();

    int srcIndex = m_workspace->activeLayerIndex();
    auto* srcLayer = doc->layerAt(srcIndex);
    auto clone = srcLayer->clone();
    clone->setName(srcLayer->name() + tr(" Copy"));

    int insertIndex = srcIndex + 1;
    int oldActive = srcIndex;
    doc->insertLayer(insertIndex, std::move(clone));
    m_workspace->setActiveLayerIndex(insertIndex);

    auto memento = std::make_unique<AddLayerMemento>(
        tr("Duplicate Layer"), doc, insertIndex,
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void LayersDock::mergeLayerDown() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();

    int topIndex = m_workspace->activeLayerIndex();
    if (topIndex <= 0) return;

    int bottomIndex = topIndex - 1;
    auto* topLayer = dynamic_cast<BitmapLayer*>(doc->layerAt(topIndex));
    auto* bottomLayer = dynamic_cast<BitmapLayer*>(doc->layerAt(bottomIndex));
    if (!topLayer || !bottomLayer) return;

    // Save state for undo
    auto topClone = topLayer->clone();
    Surface savedBottom(bottomLayer->surface().width(), bottomLayer->surface().height());
    for (int y = 0; y < savedBottom.height(); ++y) {
        std::memcpy(savedBottom.rowPtr(y),
                     bottomLayer->surface().rowPtr(y),
                     savedBottom.width() * sizeof(ColorBgra));
    }

    int oldActive = topIndex;

    // Blend top onto bottom
    topLayer->render(bottomLayer->surface(), bottomLayer->bounds());

    // Remove top layer
    doc->removeLayer(topIndex);
    m_workspace->setActiveLayerIndex(bottomIndex);

    auto memento = std::make_unique<MergeLayerDownMemento>(
        tr("Merge Down"), doc, topIndex, std::move(topClone), std::move(savedBottom),
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));

    m_workspace->invalidateAll();
}

void LayersDock::moveLayerUp() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();

    int index = m_workspace->activeLayerIndex();
    if (index >= doc->layerCount() - 1) return;

    int oldActive = index;
    doc->moveLayer(index, index + 1);
    m_workspace->setActiveLayerIndex(index + 1);

    auto memento = std::make_unique<MoveLayerMemento>(
        tr("Move Layer Up"), doc, index, index + 1,
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

void LayersDock::moveLayerDown() {
    if (!m_workspace || !m_workspace->document()) return;
    auto* doc = m_workspace->document();

    int index = m_workspace->activeLayerIndex();
    if (index <= 0) return;

    int oldActive = index;
    doc->moveLayer(index, index - 1);
    m_workspace->setActiveLayerIndex(index - 1);

    auto memento = std::make_unique<MoveLayerMemento>(
        tr("Move Layer Down"), doc, index, index - 1,
        LayersDock_makeSetActiveIndex(m_workspace), oldActive);
    m_workspace->historyStack()->pushNewMemento(std::move(memento));
}

bool LayersDock::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget) {
            bool ok = false;
            int index = widget->property("layerIndex").toInt(&ok);
            if (ok) {
                onLayerRowClicked(index);
                return false; // don't consume — let checkbox etc. still work
            }
        }
    } else if (event->type() == QEvent::MouseButtonDblClick) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget) {
            bool ok = false;
            int index = widget->property("layerIndex").toInt(&ok);
            if (ok && m_workspace && m_workspace->document()) {
                auto* layer = m_workspace->document()->layerAt(index);
                if (!layer) return false;

                // Find the name label in the row and replace with QLineEdit
                auto* hbox = widget->layout();
                if (!hbox || hbox->count() < 2) return false;
                auto* nameLabel = qobject_cast<QLabel*>(hbox->itemAt(1)->widget());
                if (!nameLabel) return false;

                auto* edit = new QLineEdit(layer->name(), widget);
                edit->selectAll();
                hbox->replaceWidget(nameLabel, edit);
                nameLabel->hide();
                edit->setFocus();

                connect(edit, &QLineEdit::editingFinished, this, [this, edit, nameLabel, index]() {
                    QString newName = edit->text().trimmed();
                    if (!newName.isEmpty()) {
                        onLayerRenamed(index, newName);
                    }
                    // Restore label
                    if (auto* layout = edit->parentWidget()->layout()) {
                        layout->replaceWidget(edit, nameLabel);
                        nameLabel->setText(newName.isEmpty() ? nameLabel->text() : newName);
                        nameLabel->show();
                    }
                    edit->deleteLater();
                });

                return true; // consume the double-click
            }
        }
    }
    return QDockWidget::eventFilter(obj, event);
}

} // namespace paintnux
