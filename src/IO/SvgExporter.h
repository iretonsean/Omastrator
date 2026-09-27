#pragma once
#include "Document/VectorDocument.h"
#include "IO/FileError.h"
#include <QByteArray>
#include <QString>

// SVG 1.1 out: layers and groups as <g>, paths as <path d>, type as <text>
// (or outlines), gradients in <defs>, placed images as data URIs.
namespace SvgExporter {
struct Options {
    bool textAsOutlines = false;
    bool includeBackground = false;
};
QByteArray serialize(const VectorDocument &document, const Options &options = {});
void write(const VectorDocument &document, const QString &path, const Options &options = {});
}
