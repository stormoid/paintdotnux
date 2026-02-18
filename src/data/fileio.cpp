#include "data/fileio.h"
#include "data/document.h"
#include "data/bitmaplayer.h"
#include "core/surface.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QByteArray>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace paintnux {

// --- .pnx native format ---

static const char PNX_MAGIC[] = "PNX1";

static std::unique_ptr<Document> loadPnx(const QString& filePath, QString* errorOut) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Cannot open file: ") + file.errorString();
        return nullptr;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    // Read and verify magic
    char magic[4];
    if (in.readRawData(magic, 4) != 4 || memcmp(magic, PNX_MAGIC, 4) != 0) {
        if (errorOut) *errorOut = QStringLiteral("Not a valid .pnx file");
        return nullptr;
    }

    quint32 docW, docH, layerCount;
    in >> docW >> docH >> layerCount;

    if (docW == 0 || docH == 0 || docW > 65536 || docH > 65536) {
        if (errorOut) *errorOut = QStringLiteral("Invalid document dimensions");
        return nullptr;
    }
    if (layerCount == 0 || layerCount > 1000) {
        if (errorOut) *errorOut = QStringLiteral("Invalid layer count");
        return nullptr;
    }

    auto doc = std::make_unique<Document>(static_cast<int>(docW), static_cast<int>(docH));

    for (quint32 i = 0; i < layerCount; ++i) {
        // Layer name
        quint32 nameLen;
        in >> nameLen;
        if (nameLen > 10000) {
            if (errorOut) *errorOut = QStringLiteral("Invalid layer name length");
            return nullptr;
        }
        QByteArray nameBytes(static_cast<int>(nameLen), Qt::Uninitialized);
        if (in.readRawData(nameBytes.data(), static_cast<int>(nameLen)) != static_cast<int>(nameLen)) {
            if (errorOut) *errorOut = QStringLiteral("Unexpected end of file reading layer name");
            return nullptr;
        }
        QString layerName = QString::fromUtf8(nameBytes);

        // Visible, opacity
        quint8 visible, opacity;
        in >> visible >> opacity;

        // Blend mode
        quint8 blendModeByte = 0;
        in >> blendModeByte;
        if (blendModeByte >= static_cast<quint8>(BlendMode::Count))
            blendModeByte = 0;

        // Layer dimensions
        quint32 layerW, layerH;
        in >> layerW >> layerH;
        if (layerW == 0 || layerH == 0 || layerW > 65536 || layerH > 65536) {
            if (errorOut) *errorOut = QStringLiteral("Invalid layer dimensions");
            return nullptr;
        }

        // Compressed pixel data
        quint32 compLen;
        in >> compLen;
        if (compLen > 256 * 1024 * 1024) { // 256 MB sanity check
            if (errorOut) *errorOut = QStringLiteral("Compressed data too large");
            return nullptr;
        }
        QByteArray compData(static_cast<int>(compLen), Qt::Uninitialized);
        if (in.readRawData(compData.data(), static_cast<int>(compLen)) != static_cast<int>(compLen)) {
            if (errorOut) *errorOut = QStringLiteral("Unexpected end of file reading pixel data");
            return nullptr;
        }

        // Decompress
        int rawSize = static_cast<int>(layerW * layerH * 4);
        QByteArray rawData = qUncompress(compData);
        if (rawData.size() != rawSize) {
            if (errorOut) *errorOut = QStringLiteral("Decompressed pixel data size mismatch");
            return nullptr;
        }

        // Build QImage from raw BGRA data
        QImage img(static_cast<int>(layerW), static_cast<int>(layerH), QImage::Format_ARGB32);
        for (int y = 0; y < static_cast<int>(layerH); ++y) {
            memcpy(img.scanLine(y),
                   rawData.constData() + y * static_cast<int>(layerW) * 4,
                   static_cast<size_t>(layerW) * 4);
        }

        auto layer = std::make_unique<BitmapLayer>(Surface(std::move(img)));
        layer->setName(layerName);
        layer->setVisible(visible != 0);
        layer->setOpacity(opacity);
        layer->setBlendMode(static_cast<BlendMode>(blendModeByte));
        if (i == 0) layer->setIsBackground(true);
        doc->addLayer(std::move(layer));
    }

    return doc;
}

static FileIOResult savePnx(const Document& doc, const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return {false, QStringLiteral("Cannot open file for writing: ") + file.errorString()};
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    // Magic
    out.writeRawData(PNX_MAGIC, 4);

    // Document header
    out << static_cast<quint32>(doc.width())
        << static_cast<quint32>(doc.height())
        << static_cast<quint32>(doc.layerCount());

    for (int i = 0; i < doc.layerCount(); ++i) {
        auto* layer = doc.layerAt(i);
        auto* bmpLayer = dynamic_cast<BitmapLayer*>(layer);
        if (!bmpLayer) {
            return {false, QStringLiteral("Layer %1 is not a bitmap layer").arg(i)};
        }

        // Name
        QByteArray nameBytes = layer->name().toUtf8();
        out << static_cast<quint32>(nameBytes.size());
        out.writeRawData(nameBytes.constData(), nameBytes.size());

        // Visible, opacity, blend mode
        out << static_cast<quint8>(layer->isVisible() ? 1 : 0);
        out << layer->opacity();
        out << static_cast<quint8>(bmpLayer->blendMode());

        // Dimensions
        const Surface& surf = bmpLayer->surface();
        out << static_cast<quint32>(surf.width())
            << static_cast<quint32>(surf.height());

        // Gather raw pixel data
        int rawSize = surf.width() * surf.height() * 4;
        QByteArray rawData(rawSize, Qt::Uninitialized);
        for (int y = 0; y < surf.height(); ++y) {
            memcpy(rawData.data() + y * surf.width() * 4,
                   surf.rowPtr(y),
                   static_cast<size_t>(surf.width()) * 4);
        }

        // Compress and write
        QByteArray compData = qCompress(rawData);
        out << static_cast<quint32>(compData.size());
        out.writeRawData(compData.constData(), compData.size());
    }

    return {true, {}};
}

// --- Flat image format support ---

static std::unique_ptr<Document> loadFlat(const QString& filePath, QString* errorOut) {
    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        if (errorOut) *errorOut = QStringLiteral("Failed to load image: ") + reader.errorString();
        return nullptr;
    }

    if (img.format() != QImage::Format_ARGB32) {
        img = img.convertToFormat(QImage::Format_ARGB32);
    }

    auto doc = std::make_unique<Document>(img.width(), img.height());
    auto layer = std::make_unique<BitmapLayer>(Surface(std::move(img)));
    layer->setName(QStringLiteral("Background"));
    layer->setIsBackground(true);
    doc->addLayer(std::move(layer));
    return doc;
}

static FileIOResult saveFlat(const Document& doc, const QString& filePath) {
    Surface flat = doc.flatten();
    QImage img = flat.qimage();

    // For JPEG, convert to RGB (no alpha)
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        img = img.convertToFormat(QImage::Format_RGB32);
    }

    QImageWriter writer(filePath);
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        writer.setQuality(95);
    } else if (ext == QStringLiteral("png")) {
        writer.setQuality(50); // PNG compression level (0=none, 100=max)
    }

    if (!writer.write(img)) {
        return {false, QStringLiteral("Failed to save image: ") + writer.errorString()};
    }

    return {true, {}};
}

// --- Public API ---

std::unique_ptr<Document> loadDocument(const QString& filePath, QString* errorOut) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("pnx")) {
        return loadPnx(filePath, errorOut);
    }
    return loadFlat(filePath, errorOut);
}

FileIOResult saveDocument(const Document& doc, const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("pnx")) {
        return savePnx(doc, filePath);
    }
    return saveFlat(doc, filePath);
}

QString openFileFilter() {
    return QStringLiteral(
        "All Supported (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp *.pnx);;"
        "Paint.nux Project (*.pnx);;"
        "PNG (*.png);;"
        "JPEG (*.jpg *.jpeg);;"
        "BMP (*.bmp);;"
        "GIF (*.gif);;"
        "TIFF (*.tif *.tiff);;"
        "WebP (*.webp);;"
        "All Files (*)");
}

QString saveFileFilter() {
    return QStringLiteral(
        "Paint.nux Project (*.pnx);;"
        "PNG (*.png);;"
        "JPEG (*.jpg *.jpeg);;"
        "BMP (*.bmp);;"
        "TIFF (*.tif *.tiff);;"
        "WebP (*.webp)");
}

QString exportFileFilter() {
    return QStringLiteral(
        "PNG (*.png);;"
        "JPEG (*.jpg *.jpeg);;"
        "BMP (*.bmp);;"
        "GIF (*.gif);;"
        "TIFF (*.tif *.tiff);;"
        "WebP (*.webp)");
}

// --- Octree color quantizer for 8-bit PNG ---

struct OctreeNode {
    uint64_t rSum = 0, gSum = 0, bSum = 0;
    uint64_t pixelCount = 0;
    int paletteIndex = -1;
    std::array<int, 8> children{}; // indices into node pool, 0 = none
    bool isLeaf = false;
    int level = 0;

    OctreeNode() { children.fill(0); }
};

static int childIndex(uint8_t r, uint8_t g, uint8_t b, int level) {
    int shift = 7 - level;
    return ((r >> shift) & 1) << 2 | ((g >> shift) & 1) << 1 | ((b >> shift) & 1);
}

struct OctreeQuantizer {
    std::vector<OctreeNode> nodes;
    std::array<std::vector<int>, 8> levelNodes; // node indices per level
    int leafCount = 0;
    int maxColors = 255; // reserve 1 slot for transparent

    OctreeQuantizer() {
        nodes.reserve(8192);
        nodes.emplace_back(); // root at index 0
    }

    void addColor(uint8_t r, uint8_t g, uint8_t b) {
        int nodeIdx = 0; // root
        for (int level = 0; level < 8; ++level) {
            int ci = childIndex(r, g, b, level);
            if (nodes[nodeIdx].children[ci] == 0) {
                int newIdx = static_cast<int>(nodes.size());
                nodes.emplace_back();
                nodes[newIdx].level = level + 1;
                // Re-fetch after potential realloc
                nodes[nodeIdx].children[ci] = newIdx;
                if (level == 7) {
                    nodes[newIdx].isLeaf = true;
                    leafCount++;
                } else {
                    levelNodes[level + 1].push_back(newIdx);
                }
            }
            nodeIdx = nodes[nodeIdx].children[ci];
        }
        nodes[nodeIdx].rSum += r;
        nodes[nodeIdx].gSum += g;
        nodes[nodeIdx].bSum += b;
        nodes[nodeIdx].pixelCount++;
    }

    void reduce() {
        while (leafCount > maxColors) {
            // Find deepest non-leaf level with nodes
            int level = 7;
            while (level > 0 && levelNodes[level].empty()) --level;
            if (level == 0) break;

            // Merge children of all nodes at this level
            auto& lnodes = levelNodes[level];
            for (int idx : lnodes) {
                auto& node = nodes[idx];
                if (node.isLeaf) continue;
                int childrenMerged = 0;
                for (int ci = 0; ci < 8; ++ci) {
                    if (node.children[ci] != 0) {
                        auto& child = nodes[node.children[ci]];
                        node.rSum += child.rSum;
                        node.gSum += child.gSum;
                        node.bSum += child.bSum;
                        node.pixelCount += child.pixelCount;
                        if (child.isLeaf) childrenMerged++;
                        node.children[ci] = 0;
                    }
                }
                node.isLeaf = true;
                leafCount -= (childrenMerged - 1); // merged N leaves into 1
            }
            lnodes.clear();
        }
    }

    QVector<QRgb> buildPalette(bool hasTransparency) {
        QVector<QRgb> palette;
        if (hasTransparency) palette.append(qRgba(0, 0, 0, 0)); // index 0 = transparent
        assignIndices(0, palette);
        return palette;
    }

    void assignIndices(int nodeIdx, QVector<QRgb>& palette) {
        auto& node = nodes[nodeIdx];
        if (node.isLeaf && node.pixelCount > 0) {
            node.paletteIndex = palette.size();
            uint8_t r = static_cast<uint8_t>(node.rSum / node.pixelCount);
            uint8_t g = static_cast<uint8_t>(node.gSum / node.pixelCount);
            uint8_t b = static_cast<uint8_t>(node.bSum / node.pixelCount);
            palette.append(qRgb(r, g, b));
            return;
        }
        for (int ci = 0; ci < 8; ++ci) {
            if (node.children[ci] != 0)
                assignIndices(node.children[ci], palette);
        }
    }

    int findNearest(uint8_t r, uint8_t g, uint8_t b) const {
        int nodeIdx = 0;
        int lastLeaf = -1;
        for (int level = 0; level < 8; ++level) {
            const auto& node = nodes[nodeIdx];
            if (node.isLeaf) { lastLeaf = node.paletteIndex; break; }
            int ci = childIndex(r, g, b, level);
            if (node.children[ci] == 0) {
                // Find any existing child
                for (int i = 0; i < 8; ++i) {
                    if (node.children[i] != 0) {
                        nodeIdx = node.children[i];
                        break;
                    }
                }
                // Walk down to leaf
                while (!nodes[nodeIdx].isLeaf) {
                    for (int i = 0; i < 8; ++i) {
                        if (nodes[nodeIdx].children[i] != 0) {
                            nodeIdx = nodes[nodeIdx].children[i];
                            break;
                        }
                    }
                }
                lastLeaf = nodes[nodeIdx].paletteIndex;
                break;
            }
            nodeIdx = node.children[ci];
        }
        return lastLeaf >= 0 ? lastLeaf : 0;
    }

    // Get the palette color for an index
    static void paletteColor(const QVector<QRgb>& palette, int idx, int& r, int& g, int& b) {
        QRgb c = palette[idx];
        r = qRed(c); g = qGreen(c); b = qBlue(c);
    }
};

static QImage quantizeTo8Bit(const QImage& src, bool withAlpha, int ditherLevel, int threshold) {
    int w = src.width(), h = src.height();

    // Build octree from opaque pixels
    OctreeQuantizer qt;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (withAlpha && qAlpha(row[x]) < threshold) continue;
            qt.addColor(qRed(row[x]), qGreen(row[x]), qBlue(row[x]));
        }
    }
    qt.reduce();
    QVector<QRgb> palette = qt.buildPalette(withAlpha);

    // Create indexed image
    QImage dst(w, h, QImage::Format_Indexed8);
    dst.setColorTable(palette);

    // Error diffusion buffers (Floyd-Steinberg)
    std::vector<int> errR0(w + 2, 0), errG0(w + 2, 0), errB0(w + 2, 0);
    std::vector<int> errR1(w + 2, 0), errG1(w + 2, 0), errB1(w + 2, 0);

    int transparentIdx = withAlpha ? 0 : -1;

    for (int y = 0; y < h; ++y) {
        const QRgb* srcRow = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        uint8_t* dstRow = dst.scanLine(y);

        std::fill(errR1.begin(), errR1.end(), 0);
        std::fill(errG1.begin(), errG1.end(), 0);
        std::fill(errB1.begin(), errB1.end(), 0);

        for (int x = 0; x < w; ++x) {
            QRgb px = srcRow[x];

            if (withAlpha && qAlpha(px) < threshold) {
                dstRow[x] = static_cast<uint8_t>(transparentIdx);
                continue;
            }

            // Apply accumulated error
            int r = qRed(px)   - (errR0[x + 1] * ditherLevel) / 8;
            int g = qGreen(px) - (errG0[x + 1] * ditherLevel) / 8;
            int b = qBlue(px)  - (errB0[x + 1] * ditherLevel) / 8;
            r = std::clamp(r, 0, 255);
            g = std::clamp(g, 0, 255);
            b = std::clamp(b, 0, 255);

            int idx = qt.findNearest(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
            dstRow[x] = static_cast<uint8_t>(idx);

            // Compute error
            int pr, pg, pb;
            OctreeQuantizer::paletteColor(palette, idx, pr, pg, pb);
            int er = r - pr, eg = g - pg, eb = b - pb;

            // Floyd-Steinberg distribution: 7/16 right, 3/16 below-left, 5/16 below, 1/16 below-right
            errR0[x + 2] += er * 7 / 16;  errG0[x + 2] += eg * 7 / 16;  errB0[x + 2] += eb * 7 / 16;
            errR1[x]     += er * 3 / 16;  errG1[x]     += eg * 3 / 16;  errB1[x]     += eb * 3 / 16;
            errR1[x + 1] += er * 5 / 16;  errG1[x + 1] += eg * 5 / 16;  errB1[x + 1] += eb * 5 / 16;
            errR1[x + 2] += er * 1 / 16;  errG1[x + 2] += eg * 1 / 16;  errB1[x + 2] += eb * 1 / 16;
        }

        std::swap(errR0, errR1);
        std::swap(errG0, errG1);
        std::swap(errB0, errB1);
    }

    return dst;
}

static bool imageHasTransparency(const QImage& img) {
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) < 255) return true;
        }
    }
    return false;
}

static bool imageHasPartialAlpha(const QImage& img) {
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            uint8_t a = qAlpha(row[x]);
            if (a != 0 && a != 255) return true;
        }
    }
    return false;
}

static QImage applyPngBitDepth(const QImage& src, PngBitDepth depth, int ditherLevel, int threshold) {
    QImage img = src.format() == QImage::Format_ARGB32 ? src : src.convertToFormat(QImage::Format_ARGB32);

    bool hasTransparency = imageHasTransparency(img);

    switch (depth) {
    case PngBitDepth::Bpp32:
        return img; // full RGBA32

    case PngBitDepth::Bpp24:
        return img.convertToFormat(QImage::Format_RGB32); // drop alpha

    case PngBitDepth::Bpp8:
        return quantizeTo8Bit(img, hasTransparency, ditherLevel, threshold);

    case PngBitDepth::AutoDetect:
    default:
        // Auto: use 32-bit if partial alpha, 24-bit if fully opaque, 8-bit if <= 256 unique colors
        if (imageHasPartialAlpha(img)) return img; // need full alpha
        // Count unique colors (quick check, bail if > 256)
        {
            QSet<QRgb> colors;
            bool tooMany = false;
            for (int y = 0; y < img.height() && !tooMany; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    colors.insert(row[x]);
                    if (colors.size() > 256) { tooMany = true; break; }
                }
            }
            if (!tooMany)
                return quantizeTo8Bit(img, hasTransparency, ditherLevel, threshold);
        }
        if (!hasTransparency) return img.convertToFormat(QImage::Format_RGB32);
        return img; // keep RGBA32
    }
}

FileIOResult exportDocument(const Document& doc, const QString& filePath, const ExportOptions& opts) {
    Surface flat = doc.flatten();
    QImage img = flat.qimage();

    QString ext = QFileInfo(filePath).suffix().toLower();

    // Format-specific image conversion
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        img = img.convertToFormat(QImage::Format_RGB32);
    } else if (ext == QStringLiteral("png")) {
        img = applyPngBitDepth(img, opts.pngBitDepth, opts.pngDitherLevel, opts.pngThreshold);
    } else if (ext == QStringLiteral("bmp")) {
        if (img.format() != QImage::Format_ARGB32)
            img = img.convertToFormat(QImage::Format_ARGB32);
        bool need8bit = (opts.bmpBitDepth == BmpBitDepth::Bpp8);
        if (opts.bmpBitDepth == BmpBitDepth::AutoDetect) {
            QSet<QRgb> colors;
            bool tooMany = false;
            for (int y = 0; y < img.height() && !tooMany; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) {
                    colors.insert(row[x] | 0xFF000000u); // ignore alpha for BMP
                    if (colors.size() > 256) { tooMany = true; break; }
                }
            }
            need8bit = !tooMany;
        }
        if (need8bit)
            img = quantizeTo8Bit(img, false, opts.bmpDitherLevel, 0);
        else
            img = img.convertToFormat(QImage::Format_RGB32);
    } else if (ext == QStringLiteral("gif")) {
        if (img.format() != QImage::Format_ARGB32)
            img = img.convertToFormat(QImage::Format_ARGB32);
        bool hasAlpha = imageHasTransparency(img);
        img = quantizeTo8Bit(img, hasAlpha, opts.gifDitherLevel, opts.gifThreshold);
    }

    QImageWriter writer(filePath);

    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        writer.setQuality(opts.jpegQuality);
        if (opts.jpegProgressive)
            writer.setOptimizedWrite(true);  // progressive for JPEG
    } else if (ext == QStringLiteral("webp")) {
        writer.setQuality(opts.webpQuality);
    } else if (ext == QStringLiteral("tif") || ext == QStringLiteral("tiff")) {
        writer.setCompression(opts.tiffCompression);
    }

    if (!writer.write(img)) {
        return {false, QStringLiteral("Failed to export image: ") + writer.errorString()};
    }

    return {true, {}};
}

} // namespace paintnux
