#pragma once
#include "Document/VectorDocument.h"
#include "IO/SvgSource.h"
#include <QStringList>
#include <optional>

struct NSVGpaint;
struct NSVGshape;

// Shared by SvgImporter's files: nanosvg's shapes into objects, and the text
// and images nanosvg leaves out.
namespace SvgImport {
// Geometry and paint only; names, opacity and visibility come from the XML.
VectorObject pathObject(const NSVGshape *shape);
// Fill and stroke. `toLocal` takes document space into the object's own,
// where `bounds` are and where `scale` shrinks nanosvg's stroke widths.
void applyPaint(VectorObject &object, const NSVGshape *shape, const QRectF &bounds, const QTransform &toLocal = {}, double scale = 1);

// A <text> read into point type; its first baseline sits at `origin`.
struct TextRun {
    TextContent content;
    QPointF origin;
    // The element whose paint the glyphs take: the first span with characters.
    int style = -1;
};
std::optional<TextRun> readText(const SvgSource &source, int element);
// `probe` is the rectangle nanosvg painted as the text would be.
VectorObject textObject(const SvgSource &source, int element, const TextRun &run, const QTransform &parentCTM, const NSVGshape *probe);

// An <image>, with the rectangle that crops it when it is sliced.
struct PlacedImage {
    VectorObject object;
    std::optional<VectorPath> crop;
};
// Relative files resolve against `folder`; what fails lands in `warnings`.
std::optional<PlacedImage> imageObject(const SvgSource &source, int element, const QTransform &parentCTM, const QString &folder,
                                       QStringList &warnings);
}
