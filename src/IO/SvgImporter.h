#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>
#include <QStringList>

// SVG in: nanosvg reads the shapes and their paint; the XML itself gives the
// layers, groups, names, text, images and clip groups. Filters, masks and
// <use> copies can't be represented and are left out, with a warning each.
namespace SvgImporter {
// A whole document: the SVG's viewBox (or width and height) is the artboard.
// Top-level shapes land in a layer named after the file; relative image links
// resolve against its folder.
VectorDocument read(const QString &path, QStringList *warnings = nullptr);
VectorDocument parse(const QByteArray &svg, QStringList *warnings = nullptr);
// What the last read or parse on this thread left out, one sentence each.
QStringList lastWarnings();
}
