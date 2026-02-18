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

FileIOResult exportDocument(const Document& doc, const QString& filePath, const ExportOptions& opts) {
    Surface flat = doc.flatten();
    QImage img = flat.qimage();

    QString ext = QFileInfo(filePath).suffix().toLower();

    // JPEG needs RGB (no alpha)
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        img = img.convertToFormat(QImage::Format_RGB32);
    }

    QImageWriter writer(filePath);

    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
        writer.setQuality(opts.jpegQuality);
        if (opts.jpegProgressive)
            writer.setOptimizedWrite(true);  // progressive for JPEG
    } else if (ext == QStringLiteral("png")) {
        writer.setQuality(opts.pngCompression);
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
