#include "data/document.h"
#include "data/bitmaplayer.h"

#include <gtest/gtest.h>

using namespace paintnux;

TEST(Document, CreateEmpty) {
    Document doc(100, 50);
    EXPECT_EQ(doc.width(), 100);
    EXPECT_EQ(doc.height(), 50);
    EXPECT_EQ(doc.layerCount(), 0);
}

TEST(Document, AddLayer) {
    Document doc(100, 100);
    doc.addLayer(std::make_unique<BitmapLayer>(100, 100));
    EXPECT_EQ(doc.layerCount(), 1);
    EXPECT_TRUE(doc.isDirty());
}

TEST(Document, InsertLayer) {
    Document doc(100, 100);
    doc.addLayer(std::make_unique<BitmapLayer>(100, 100));
    doc.addLayer(std::make_unique<BitmapLayer>(100, 100));
    doc.insertLayer(1, std::make_unique<BitmapLayer>(100, 100));
    EXPECT_EQ(doc.layerCount(), 3);
}

TEST(Document, RemoveLayer) {
    Document doc(100, 100);
    doc.addLayer(std::make_unique<BitmapLayer>(100, 100));
    doc.addLayer(std::make_unique<BitmapLayer>(100, 100));
    auto removed = doc.removeLayer(0);
    EXPECT_EQ(doc.layerCount(), 1);
    EXPECT_NE(removed, nullptr);
}

TEST(Document, FlattenSingleWhiteLayer) {
    Document doc(10, 10);
    doc.addLayer(BitmapLayer::createBackground(10, 10));
    Surface flat = doc.flatten();
    EXPECT_EQ(flat.getPoint(0, 0), ColorBgra::white());
    EXPECT_EQ(flat.getPoint(9, 9), ColorBgra::white());
}

TEST(Document, FlattenTwoLayers) {
    Document doc(10, 10);

    // White background
    doc.addLayer(BitmapLayer::createBackground(10, 10));

    // Semi-transparent red foreground
    auto fg = std::make_unique<BitmapLayer>(10, 10, ColorBgra::fromBgra(0, 0, 255, 128));
    doc.addLayer(std::move(fg));

    Surface flat = doc.flatten();
    auto p = flat.getPoint(5, 5);
    // White bg + semi-transparent red -> pinkish
    EXPECT_GT(p.r, 180);
    EXPECT_LT(p.g, 140);
    EXPECT_LT(p.b, 140);
    EXPECT_EQ(p.a, 255);
}

TEST(Document, InvisibleLayerSkipped) {
    Document doc(10, 10);
    doc.addLayer(BitmapLayer::createBackground(10, 10));

    auto fg = std::make_unique<BitmapLayer>(10, 10, ColorBgra::fromBgra(0, 0, 255, 255));
    fg->setVisible(false);
    doc.addLayer(std::move(fg));

    Surface flat = doc.flatten();
    // Should be white (red layer is hidden)
    EXPECT_EQ(flat.getPoint(5, 5), ColorBgra::white());
}

TEST(Document, MoveLayer) {
    Document doc(10, 10);
    auto bg = BitmapLayer::createBackground(10, 10);
    bg->setName("bg");
    doc.addLayer(std::move(bg));

    auto fg = std::make_unique<BitmapLayer>(10, 10);
    fg->setName("fg");
    doc.addLayer(std::move(fg));

    EXPECT_EQ(doc.layerAt(0)->name(), "bg");
    EXPECT_EQ(doc.layerAt(1)->name(), "fg");

    doc.moveLayer(1, 0);
    EXPECT_EQ(doc.layerAt(0)->name(), "fg");
    EXPECT_EQ(doc.layerAt(1)->name(), "bg");
}

TEST(Document, FlattenEmpty) {
    Document doc(10, 10);
    Surface flat = doc.flatten();
    // Should be transparent
    EXPECT_EQ(flat.getPoint(0, 0).bgra, 0u);
}

TEST(BitmapLayer, CreateBackground) {
    auto layer = BitmapLayer::createBackground(100, 100);
    EXPECT_EQ(layer->name(), "Background");
    EXPECT_TRUE(layer->isBackground());
    EXPECT_EQ(layer->surface().getPoint(0, 0), ColorBgra::white());
}

TEST(BitmapLayer, Clone) {
    auto layer = std::make_unique<BitmapLayer>(10, 10, ColorBgra::fromBgra(100, 100, 100, 255));
    layer->setName("test");
    layer->setOpacity(200);

    auto cloned = layer->clone();
    auto* bmp = dynamic_cast<BitmapLayer*>(cloned.get());
    ASSERT_NE(bmp, nullptr);
    EXPECT_EQ(bmp->name(), "test");
    EXPECT_EQ(bmp->opacity(), 200);
    EXPECT_EQ(bmp->surface().getPoint(0, 0), ColorBgra::fromBgra(100, 100, 100, 255));
}

TEST(BitmapLayer, Thumbnail) {
    auto layer = std::make_unique<BitmapLayer>(200, 100, ColorBgra::white());
    Surface thumb = layer->renderThumbnail(50);
    EXPECT_EQ(thumb.width(), 50);
    EXPECT_EQ(thumb.height(), 25);
}
