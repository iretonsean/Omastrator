#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>

// SVG in, through nanosvg: every shape becomes an editable path with its fill,
// stroke and opacity. Text arrives as outlines; filters and masks are dropped.
namespace SvgImporter {
// A whole document: the SVG's viewBox (or width and height) is the artboard.
VectorDocument read(const QString &path);
VectorDocument parse(const QByteArray &svg);
}
