#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QString>
#include <QStringList>

// The single dispatch point File > Open, File > Place, drag-and-drop and the
// agent's place tool all go through for a vector file: SVG, PDF, Illustrator
// (.ai) or EPS/PostScript, chosen by `path`'s extension. Kept here so the
// three importer branches (feat/import-pdf, feat/import-figma,
// feat/import-open) each add one line to `read`, not a copy of this dispatch
// at every call site.
namespace VectorFileImporter {
// Every page or artboard the file has.
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
// File > Place's rule for a multi-page/artboard file: only the first page is
// placed, with a warning naming how many there were.
VectorDocument readFirstArtboard(const QString &path, QStringList *warnings = nullptr);
}
