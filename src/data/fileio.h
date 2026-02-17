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

} // namespace paintnux
