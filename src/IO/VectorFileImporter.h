#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QString>
#include <QStringList>

// The single dispatch point File > Open, File > Place, drag-and-drop and the
// agent's place tool all go through for a file that becomes layers rather
// than pixels: SVG/SVGZ, PDF, Illustrator (.ai), EPS/PostScript, Figma
// (.fig), Sketch, Penpot and Excalidraw. Figma is recognised by content;
// the rest by `path`'s extension.
namespace VectorFileImporter {
// True when `path` is read as layers here; false means it's a raster image.
bool canRead(const QString &path);
// True for a multi-page design file (Sketch, Penpot, Figma): dropped on an open
// canvas it opens in its own tab rather than being placed.
bool isDesignFile(const QString &path);
// Every page or artboard the file has.
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
// File > Place's rule for a multi-page/artboard file: only the first page is
// placed, with a warning naming how many there were.
VectorDocument readFirstArtboard(const QString &path, QStringList *warnings = nullptr);
}
