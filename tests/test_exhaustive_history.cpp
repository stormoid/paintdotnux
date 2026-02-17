#include <gtest/gtest.h>
#include "history/historystack.h"
#include "history/bitmaphistorymemento.h"
#include "history/selectionhistorymemento.h"
#include "history/flipmemento.h"
#include "history/layerhistorymemento.h"
#include "data/document.h"
#include "data/bitmaplayer.h"
#include "data/selection.h"
#include "core/surface.h"
#include "core/colorbgra.h"

#include <QPainterPath>
#include <QRegion>

using namespace paintnux;

// ===== HistoryStack =====

class HistoryStackTest : public ::testing::Test {
protected:
    void SetUp() override {
        doc = std::make_unique<Document>(10, 10);
        doc->addLayer(BitmapLayer::createBackground(10, 10));
        stack = std::make_unique<HistoryStack>(doc.get());
    }

    std::unique_ptr<Document> doc;
    std::unique_ptr<HistoryStack> stack;
};

TEST_F(HistoryStackTest, InitiallyEmpty) {
    EXPECT_FALSE(stack->canUndo());
    EXPECT_FALSE(stack->canRedo());
    EXPECT_EQ(stack->undoCount(), 0);
    EXPECT_EQ(stack->redoCount(), 0);
}

TEST_F(HistoryStackTest, PushIncrementsUndo) {
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(0));
    layer->surface().setPoint(0, 0, ColorBgra::fromBgra(255, 0, 0, 255));
    auto m = std::make_unique<BitmapHistoryMemento>(
        "test", doc.get(), 0, QRegion(doc->bounds()));
    stack->pushNewMemento(std::move(m));
    EXPECT_TRUE(stack->canUndo());
    EXPECT_EQ(stack->undoCount(), 1);
    EXPECT_EQ(stack->undoName(0), "test");
}

TEST_F(HistoryStackTest, UndoRedo) {
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(0));
    // Save original state
    auto m = std::make_unique<BitmapHistoryMemento>(
        "paint", doc.get(), 0, QRegion(doc->bounds()));
    // Modify
    layer->surface().setPoint(0, 0, ColorBgra::fromBgra(0, 0, 255, 255));
    stack->pushNewMemento(std::move(m));

    // Undo
    stack->stepBackward();
    EXPECT_FALSE(stack->canUndo());
    EXPECT_TRUE(stack->canRedo());
    EXPECT_EQ(stack->redoCount(), 1);

    // Redo
    stack->stepForward();
    EXPECT_TRUE(stack->canUndo());
    EXPECT_FALSE(stack->canRedo());
}

TEST_F(HistoryStackTest, PushClearsRedo) {
    auto m1 = std::make_unique<BitmapHistoryMemento>(
        "a", doc.get(), 0, QRegion(doc->bounds()));
    stack->pushNewMemento(std::move(m1));
    stack->stepBackward();
    EXPECT_TRUE(stack->canRedo());

    auto m2 = std::make_unique<BitmapHistoryMemento>(
        "b", doc.get(), 0, QRegion(doc->bounds()));
    stack->pushNewMemento(std::move(m2));
    EXPECT_FALSE(stack->canRedo());
}

TEST_F(HistoryStackTest, ClearAll) {
    auto m = std::make_unique<BitmapHistoryMemento>(
        "x", doc.get(), 0, QRegion(doc->bounds()));
    stack->pushNewMemento(std::move(m));
    stack->clearAll();
    EXPECT_FALSE(stack->canUndo());
    EXPECT_FALSE(stack->canRedo());
    EXPECT_EQ(stack->undoCount(), 0);
}

TEST_F(HistoryStackTest, MultipleUndoRedo) {
    for (int i = 0; i < 5; ++i) {
        auto m = std::make_unique<BitmapHistoryMemento>(
            QString("op%1").arg(i), doc.get(), 0, QRegion(doc->bounds()));
        stack->pushNewMemento(std::move(m));
    }
    EXPECT_EQ(stack->undoCount(), 5);

    // Undo all
    for (int i = 0; i < 5; ++i) stack->stepBackward();
    EXPECT_EQ(stack->undoCount(), 0);
    EXPECT_EQ(stack->redoCount(), 5);

    // Redo all
    for (int i = 0; i < 5; ++i) stack->stepForward();
    EXPECT_EQ(stack->undoCount(), 5);
    EXPECT_EQ(stack->redoCount(), 0);
}

TEST_F(HistoryStackTest, UndoNames) {
    for (int i = 0; i < 3; ++i) {
        auto m = std::make_unique<BitmapHistoryMemento>(
            QString("op%1").arg(i), doc.get(), 0, QRegion(doc->bounds()));
        stack->pushNewMemento(std::move(m));
    }
    auto names = stack->undoNames();
    EXPECT_EQ(names.size(), 3);
    EXPECT_EQ(names[0], "op2"); // most recent first
    EXPECT_EQ(names[2], "op0");
}

// ===== BitmapHistoryMemento =====

TEST(BitmapHistoryMemento_Test, UndoRestoresPixels) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(0));

    // Set a pixel
    ColorBgra original = ColorBgra::fromBgra(255, 255, 255, 255);
    layer->surface().setPoint(3, 3, original);

    // Create memento (captures current state)
    auto memento = std::make_unique<BitmapHistoryMemento>(
        "paint", doc.get(), 0, QRegion(QRect(3, 3, 1, 1)));

    // Modify pixel
    ColorBgra modified = ColorBgra::fromBgra(0, 0, 255, 255);
    layer->surface().setPoint(3, 3, modified);
    EXPECT_EQ(layer->surface().getPoint(3, 3).bgra, modified.bgra);

    // Undo — should restore original
    auto reverse = memento->performUndo();
    EXPECT_EQ(layer->surface().getPoint(3, 3).bgra, original.bgra);

    // Redo — should restore modified
    auto reverse2 = reverse->performUndo();
    EXPECT_EQ(layer->surface().getPoint(3, 3).bgra, modified.bgra);
}

TEST(BitmapHistoryMemento_Test, PreSavedSurface) {
    auto doc = std::make_unique<Document>(5, 5);
    doc->addLayer(BitmapLayer::createBackground(5, 5));
    auto* layer = dynamic_cast<BitmapLayer*>(doc->layerAt(0));

    // Pre-save
    Surface saved = layer->surface().clone();
    layer->surface().setPoint(2, 2, ColorBgra::fromBgra(255, 0, 0, 255));

    auto memento = std::make_unique<BitmapHistoryMemento>(
        "paint", doc.get(), 0, QRegion(doc->bounds()), std::move(saved));

    auto reverse = memento->performUndo();
    // Should have restored white background
    auto p = layer->surface().getPoint(2, 2);
    EXPECT_EQ(p.r, 255);
    EXPECT_EQ(p.g, 255);
    EXPECT_EQ(p.b, 255);
}

// ===== SelectionHistoryMemento =====

TEST(SelectionHistoryMemento_Test, UndoRedoSwapsPath) {
    Selection sel;
    QPainterPath rect;
    rect.addRect(0, 0, 50, 50);
    sel.setPath(rect);

    // Capture state
    auto memento = std::make_unique<SelectionHistoryMemento>("select", &sel);

    // Modify selection
    QPainterPath circle;
    circle.addEllipse(10, 10, 30, 30);
    sel.setPath(circle);

    EXPECT_FALSE(sel.path().isEmpty());

    // Undo — should restore rect
    auto reverse = memento->performUndo();
    EXPECT_TRUE(sel.path().boundingRect().width() > 0);

    // Redo — should restore circle
    [[maybe_unused]] auto r2 = reverse->performUndo();
}

TEST(SelectionHistoryMemento_Test, PreCapturedPath) {
    Selection sel;
    QPainterPath empty;
    auto memento = std::make_unique<SelectionHistoryMemento>("deselect", &sel, empty);

    QPainterPath rect;
    rect.addRect(0, 0, 100, 100);
    sel.setPath(rect);
    EXPECT_FALSE(sel.isEmpty());

    // Undo — should set to empty path
    auto reverse = memento->performUndo();
    EXPECT_TRUE(sel.isEmpty());
}

// ===== FlipMemento =====

TEST(FlipMemento_Test, DoubleFlipHorizontalIsIdentity) {
    auto doc = std::make_unique<Document>(4, 4);
    auto layer = std::make_unique<BitmapLayer>(4, 4);
    // Set a distinct pixel
    layer->surface().setPoint(0, 0, ColorBgra::fromBgra(255, 0, 0, 255));
    layer->surface().setPoint(3, 0, ColorBgra::fromBgra(0, 255, 0, 255));
    auto original00 = layer->surface().getPoint(0, 0);
    auto original30 = layer->surface().getPoint(3, 0);
    doc->addLayer(std::move(layer));

    // Flip horizontal
    auto m1 = std::make_unique<FlipMemento>("flip h", doc.get(), FlipDirection::Horizontal);
    auto* bl = dynamic_cast<BitmapLayer*>(doc->layerAt(0));

    // After first flip: pixel at (0,0) should now be at (3,0) and vice versa
    auto r1 = m1->performUndo();
    EXPECT_EQ(bl->surface().getPoint(3, 0).bgra, original00.bgra);
    EXPECT_EQ(bl->surface().getPoint(0, 0).bgra, original30.bgra);

    // Flip again (undo) — should restore original
    [[maybe_unused]] auto r2 = r1->performUndo();
    EXPECT_EQ(bl->surface().getPoint(0, 0).bgra, original00.bgra);
    EXPECT_EQ(bl->surface().getPoint(3, 0).bgra, original30.bgra);
}

TEST(FlipMemento_Test, DoubleFlipVerticalIsIdentity) {
    auto doc = std::make_unique<Document>(4, 4);
    auto layer = std::make_unique<BitmapLayer>(4, 4);
    layer->surface().setPoint(0, 0, ColorBgra::fromBgra(255, 0, 0, 255));
    layer->surface().setPoint(0, 3, ColorBgra::fromBgra(0, 255, 0, 255));
    auto original00 = layer->surface().getPoint(0, 0);
    auto original03 = layer->surface().getPoint(0, 3);
    doc->addLayer(std::move(layer));

    auto m = std::make_unique<FlipMemento>("flip v", doc.get(), FlipDirection::Vertical);
    auto* bl = dynamic_cast<BitmapLayer*>(doc->layerAt(0));

    auto r = m->performUndo();
    EXPECT_EQ(bl->surface().getPoint(0, 3).bgra, original00.bgra);
    EXPECT_EQ(bl->surface().getPoint(0, 0).bgra, original03.bgra);

    [[maybe_unused]] auto r2 = r->performUndo();
    EXPECT_EQ(bl->surface().getPoint(0, 0).bgra, original00.bgra);
    EXPECT_EQ(bl->surface().getPoint(0, 3).bgra, original03.bgra);
}

// ===== Layer Mementos =====

TEST(LayerPropertyMemento_Test, NameRoundTrip) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->layerAt(0)->setName("Original");

    auto memento = std::make_unique<LayerPropertyMemento>(
        "rename", doc.get(), 0, LayerPropertyMemento::Name, "Original");

    doc->layerAt(0)->setName("Changed");
    EXPECT_EQ(doc->layerAt(0)->name(), "Changed");

    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerAt(0)->name(), "Original");

    [[maybe_unused]] auto r2 = reverse->performUndo();
    EXPECT_EQ(doc->layerAt(0)->name(), "Changed");
}

TEST(LayerPropertyMemento_Test, VisibilityRoundTrip) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));

    auto memento = std::make_unique<LayerPropertyMemento>(
        "visibility", doc.get(), 0, LayerPropertyMemento::Visible, true);

    doc->layerAt(0)->setVisible(false);
    EXPECT_FALSE(doc->layerAt(0)->isVisible());

    auto reverse = memento->performUndo();
    EXPECT_TRUE(doc->layerAt(0)->isVisible());
}

TEST(LayerPropertyMemento_Test, OpacityRoundTrip) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->layerAt(0)->setOpacity(255);

    auto memento = std::make_unique<LayerPropertyMemento>(
        "opacity", doc.get(), 0, LayerPropertyMemento::Opacity, 255);

    doc->layerAt(0)->setOpacity(128);
    EXPECT_EQ(doc->layerAt(0)->opacity(), 128);

    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerAt(0)->opacity(), 255);
}

TEST(AddLayerMemento_Test, UndoRemovesLayer) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->addLayer(std::make_unique<BitmapLayer>(10, 10));
    EXPECT_EQ(doc->layerCount(), 2);

    int activeIdx = 1;
    auto setActive = [&](int i) { activeIdx = i; };

    auto memento = std::make_unique<AddLayerMemento>(
        "add layer", doc.get(), 1, setActive, 0);

    // Undo should remove the added layer
    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerCount(), 1);
    EXPECT_EQ(activeIdx, 0);

    // Redo should re-add it
    [[maybe_unused]] auto r2 = reverse->performUndo();
    EXPECT_EQ(doc->layerCount(), 2);
}

TEST(DeleteLayerMemento_Test, UndoRestoresLayer) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->addLayer(std::make_unique<BitmapLayer>(10, 10));
    doc->layerAt(1)->setName("TopLayer");

    // Remove layer 1
    auto removedLayer = doc->removeLayer(1);
    EXPECT_EQ(doc->layerCount(), 1);

    int activeIdx = 0;
    auto setActive = [&](int i) { activeIdx = i; };

    auto memento = std::make_unique<DeleteLayerMemento>(
        "delete layer", doc.get(), 1, std::move(removedLayer), setActive, 1);

    // Undo should restore the layer
    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerCount(), 2);
    EXPECT_EQ(doc->layerAt(1)->name(), "TopLayer");
    EXPECT_EQ(activeIdx, 1);
}

TEST(MoveLayerMemento_Test, UndoMovesBack) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->layerAt(0)->setName("Bottom");
    doc->addLayer(std::make_unique<BitmapLayer>(10, 10));
    doc->layerAt(1)->setName("Top");

    // Move layer 0 to 1
    doc->moveLayer(0, 1);
    EXPECT_EQ(doc->layerAt(1)->name(), "Bottom");

    int activeIdx = 1;
    auto setActive = [&](int i) { activeIdx = i; };

    auto memento = std::make_unique<MoveLayerMemento>(
        "move layer", doc.get(), 0, 1, setActive, 0);

    // Undo — move from 1 back to 0
    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerAt(0)->name(), "Bottom");
    EXPECT_EQ(activeIdx, 0);
}

TEST(MergeLayerDownMemento_Test, UndoRestoresBothLayers) {
    auto doc = std::make_unique<Document>(10, 10);
    doc->addLayer(BitmapLayer::createBackground(10, 10));
    doc->addLayer(std::make_unique<BitmapLayer>(10, 10));
    doc->layerAt(1)->setName("TopToMerge");

    auto* bottomLayer = dynamic_cast<BitmapLayer*>(doc->layerAt(0));
    Surface savedBottom = bottomLayer->surface().clone();

    // Save top layer before merge
    auto savedTop = doc->removeLayer(1);

    // Simulate merge: paint top's pixels onto bottom (simplified)
    // In real code, the workspace does the merge. Here just test the memento.

    int activeIdx = 0;
    auto setActive = [&](int i) { activeIdx = i; };

    auto memento = std::make_unique<MergeLayerDownMemento>(
        "merge down", doc.get(), 1, std::move(savedTop), std::move(savedBottom),
        setActive, 1);

    EXPECT_EQ(doc->layerCount(), 1);

    // Undo should restore both layers
    auto reverse = memento->performUndo();
    EXPECT_EQ(doc->layerCount(), 2);
    EXPECT_EQ(doc->layerAt(1)->name(), "TopToMerge");
    EXPECT_EQ(activeIdx, 1);
}
