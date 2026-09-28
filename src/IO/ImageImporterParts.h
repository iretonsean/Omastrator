#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

// Formats ImageImporter reads beside QImageReader: split out because each
// decoder is its own small, self-contained block of format-specific code.
namespace ImageImport {
bool isHeicOrAvif(const QByteArray &head);
// Decodes through libheif; throws FileError if libheif isn't in this build, or the file can't be read.
QImage readHeicOrAvif(const QString &path, const QByteArray &data);

bool isPsd(const QByteArray &head);
// Reads a PSD/PSB's merged composite (the preview every PSD stores), flattened. Throws FileError.
QImage readPsdComposite(const QByteArray &data);
}
