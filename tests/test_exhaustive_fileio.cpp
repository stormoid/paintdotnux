#include <gtest/gtest.h>
#include "data/fileio.h"
#include "data/document.h"
#include "data/bitmaplayer.h"
#include "core/surface.h"
#include "core/colorbgra.h"

#include <QTemporaryDir>
#include <QDir>

using namespace paintnux;

class FileIOTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tmpDir->isValid());
    }

    QString tmpPath(const QString& name) const {
        return tmpDir->path() + "/" + name;
    }

    std::unique_ptr<Document> makeDoc(int w = 20, int h = 15) {
        auto doc = std::make_unique<Document>(w, h);
        auto layer = BitmapLayer::createBackground(w, h);
        // Paint some non-white pixels
        layer->surface().setPoint(0, 0, ColorBgra::fromBgra(255, 0, 0, 255));
        layer->surface().setPoint(5, 5, ColorBgra::fromBgra(0, 255, 0, 128));
        doc->addLayer(std::move(layer));
        return doc;
    }

    std::unique_ptr<QTemporaryDir> tmpDir;
};

// ===== PNG round-trip =====

TEST_F(FileIOTest, PngRoundTrip) {
    auto doc = makeDoc();
    QString path = tmpPath("test.png");
    auto result = saveDocument(*doc, path);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    QString err;
    auto loaded = loadDocument(path, &err);
    ASSERT_NE(loaded, nullptr) << err.toStdString();
    EXPECT_EQ(loaded->width(), 20);
    EXPECT_EQ(loaded->height(), 15);
    EXPECT_EQ(loaded->layerCount(), 1);

    // Check pixel preserved
    auto* layer = dynamic_cast<BitmapLayer*>(loaded->layerAt(0));
    ASSERT_NE(layer, nullptr);
    auto p = layer->surface().getPoint(0, 0);
    EXPECT_EQ(p.b, 255);
    EXPECT_EQ(p.g, 0);
    EXPECT_EQ(p.r, 0);
}

// ===== BMP round-trip =====

TEST_F(FileIOTest, BmpRoundTrip) {
    auto doc = makeDoc();
    QString path = tmpPath("test.bmp");
    auto result = saveDocument(*doc, path);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    auto loaded = loadDocument(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->width(), 20);
    EXPECT_EQ(loaded->height(), 15);
}

// ===== JPEG round-trip (lossy — just check dimensions) =====

TEST_F(FileIOTest, JpegRoundTrip) {
    auto doc = makeDoc();
    QString path = tmpPath("test.jpg");
    auto result = saveDocument(*doc, path);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    auto loaded = loadDocument(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->width(), 20);
    EXPECT_EQ(loaded->height(), 15);
}

// ===== WebP round-trip =====

TEST_F(FileIOTest, WebpRoundTrip) {
    auto doc = makeDoc();
    QString path = tmpPath("test.webp");
    auto result = saveDocument(*doc, path);
    // WebP might not be available on all Qt builds
    if (!result.success) {
        GTEST_SKIP() << "WebP not supported: " << result.errorMessage.toStdString();
    }

    auto loaded = loadDocument(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->width(), 20);
    EXPECT_EQ(loaded->height(), 15);
}

// ===== Native .pnx format =====

TEST_F(FileIOTest, PnxSingleLayerRoundTrip) {
    auto doc = makeDoc();
    doc->layerAt(0)->setName("Background");
    doc->layerAt(0)->setOpacity(200);

    QString path = tmpPath("test.pnx");
    auto result = saveDocument(*doc, path);
    ASSERT_TRUE(result.success) << result.errorMessage.toStdString();

    auto loaded = loadDocument(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->width(), 20);
    EXPECT_EQ(loaded->height(), 15);
    EXPECT_EQ(loaded->layerCount(), 1);
    EXPECT_EQ(loaded->layerAt(0)->name(), "Background");
    EXPECT_EQ(loaded->layerAt(0)->opacity(), 200);
}

TEST_F(FileIOTest, PnxMultiLayerRoundTrip) {
    auto doc = std::make_unique<Document>(10, 10);
    auto bg = BitmapLayer::createBackground(10, 10);
    bg->setName("Background");
    doc->addLayer(std::move(bg));

    auto top = std::make_unique<BitmapLayer>(10, 10);
    top->setName("Top Layer");
    top->setOpacity(128);
    top->setVisible(false);
    top->surface().setPoint(0, 0, ColorBgra::fromBgra(0, 0, 255, 255));
    doc->addLayer(std::move(top));

    auto mid = std::make_unique<BitmapLayer>(10, 10);
    mid->setName("Middle");
    doc->insertLayer(1, std::move(mid));

    QString path = tmpPath("multi.pnx");
    auto result = saveDocument(*doc, path);
    ASSERT_TRUE(result.success);

    auto loaded = loadDocument(path);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->layerCount(), 3);
    EXPECT_EQ(loaded->layerAt(0)->name(), "Background");
    EXPECT_EQ(loaded->layerAt(1)->name(), "Middle");
    EXPECT_EQ(loaded->layerAt(2)->name(), "Top Layer");
    EXPECT_EQ(loaded->layerAt(2)->opacity(), 128);
    EXPECT_FALSE(loaded->layerAt(2)->isVisible());

    // Check pixel data
    auto* loadedTop = dynamic_cast<BitmapLayer*>(loaded->layerAt(2));
    ASSERT_NE(loadedTop, nullptr);
    auto p = loadedTop->surface().getPoint(0, 0);
    EXPECT_EQ(p.b, 0);
    EXPECT_EQ(p.g, 0);
    EXPECT_EQ(p.r, 255);
}

// ===== Error handling =====

TEST_F(FileIOTest, LoadInvalidPath) {
    QString err;
    auto loaded = loadDocument("/nonexistent/path/file.png", &err);
    EXPECT_EQ(loaded, nullptr);
}

TEST_F(FileIOTest, LoadCorruptFile) {
    // Write garbage to a file
    QString path = tmpPath("corrupt.png");
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("not a valid image file");
    f.close();

    QString err;
    auto loaded = loadDocument(path, &err);
    EXPECT_EQ(loaded, nullptr);
}

TEST_F(FileIOTest, LoadCorruptPnx) {
    QString path = tmpPath("corrupt.pnx");
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("PNX1garbage data here");
    f.close();

    QString err;
    auto loaded = loadDocument(path, &err);
    EXPECT_EQ(loaded, nullptr);
}

TEST_F(FileIOTest, SaveInvalidPath) {
    auto doc = makeDoc();
    auto result = saveDocument(*doc, "/nonexistent/dir/test.png");
    EXPECT_FALSE(result.success);
}

// ===== Filter strings =====

TEST_F(FileIOTest, OpenFilterContainsFormats) {
    auto filter = openFileFilter();
    EXPECT_TRUE(filter.contains("png", Qt::CaseInsensitive));
    EXPECT_TRUE(filter.contains("jpg", Qt::CaseInsensitive) ||
                filter.contains("jpeg", Qt::CaseInsensitive));
    EXPECT_TRUE(filter.contains("bmp", Qt::CaseInsensitive));
    EXPECT_TRUE(filter.contains("pnx", Qt::CaseInsensitive));
}

TEST_F(FileIOTest, SaveFilterContainsFormats) {
    auto filter = saveFileFilter();
    EXPECT_TRUE(filter.contains("png", Qt::CaseInsensitive));
    EXPECT_TRUE(filter.contains("pnx", Qt::CaseInsensitive));
}
