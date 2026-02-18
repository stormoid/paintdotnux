#pragma once

#include <QString>

#include <memory>

namespace paintnux {

class Document;

struct FileIOResult {
    bool success = false;
    QString errorMessage;
};

/// Load a document from any supported file.
/// Returns nullptr on failure, with error message in errorOut.
std::unique_ptr<Document> loadDocument(const QString& filePath, QString* errorOut = nullptr);

/// Save a document. For flat formats, flattens first. For .pnx, preserves layers.
FileIOResult saveDocument(const Document& doc, const QString& filePath);

/// Returns a file filter string for QFileDialog::getOpenFileName().
QString openFileFilter();

/// Returns a file filter string for QFileDialog::getSaveFileName().
QString saveFileFilter();

/// Returns a file filter string for Export (flat formats only, no .pnx).
QString exportFileFilter();

enum class PngBitDepth {
    AutoDetect = 0,
    Bpp32 = 1,
    Bpp24 = 2,
    Bpp8 = 3
};

struct ExportOptions {
    int jpegQuality = 95;           // 0-100
    bool jpegProgressive = false;
    PngBitDepth pngBitDepth = PngBitDepth::AutoDetect;
    int pngDitherLevel = 7;         // 0-8, Floyd-Steinberg weight
    int pngThreshold = 128;         // 0-255, alpha threshold for 8-bit
    int webpQuality = 90;           // 0-100
    int tiffCompression = 1;        // 0=None, 1=LZW
};

/// Export a document to a flat format with user-specified options.
FileIOResult exportDocument(const Document& doc, const QString& filePath, const ExportOptions& opts);

} // namespace paintnux
