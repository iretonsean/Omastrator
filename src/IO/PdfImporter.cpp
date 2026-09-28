#include "IO/PdfImporter.h"
#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>

using Pdf::Dict;
using Pdf::Object;

namespace {
thread_local QStringList lastWarningList;

// A page's own box, llx/lly/urx/ury, CropBox first, MediaBox otherwise, a US
// Letter default if neither is usable.
QRectF boxFromArray(const QList<double> &box)
{
    if (box.size() != 4)
        return {};
    const double llx = std::min(box[0], box[2]), urx = std::max(box[0], box[2]);
    const double lly = std::min(box[1], box[3]), ury = std::max(box[1], box[3]);
    if (urx - llx <= 0 || ury - lly <= 0)
        return {};
    return QRectF(llx, lly, urx - llx, ury - lly);
}

QList<double> toDoubleList(const Pdf::Document &document, const Object &object)
{
    QList<double> out;
    for (const Object &item : document.resolve(object).toArray())
        out.append(document.resolve(item).toReal());
    return out;
}

QByteArray concatenatedContent(const Pdf::Document &document, const Dict &pageDict, QStringList *warnings)
{
    const Object contents = document.resolve(pageDict.value(QStringLiteral("Contents")));
    QByteArray result;
    const auto appendStream = [&](const Object &streamObject) {
        const PdfFilters::Decoded decoded = document.streamData(document.resolve(streamObject));
        if (warnings)
            *warnings << decoded.warnings;
        result += decoded.bytes;
        result += ' ';
    };
    if (contents.isStream()) {
        appendStream(contents);
    } else if (contents.isArray()) {
        for (const Object &item : contents.toArray())
            appendStream(item);
    }
    return result;
}

VectorDocument buildDocument(std::unique_ptr<Pdf::Document> document, QStringList *warnings)
{
    const QList<Dict> pages = document->pages();
    if (pages.isEmpty())
        throw FileError(QStringLiteral("This PDF has no readable pages."));

    VectorDocument result;
    std::vector<Artboard> artboards;
    Pdf::Interpreter interpreter(*document, result, warnings);

    constexpr double gap = 40;
    double xOffset = 0;
    for (int i = 0; i < pages.size(); ++i) {
        const Dict &pageDict = pages[i];
        QRectF box = boxFromArray(toDoubleList(*document, pageDict.value(QStringLiteral("CropBox"))));
        if (!box.isValid())
            box = boxFromArray(toDoubleList(*document, pageDict.value(QStringLiteral("MediaBox"))));
        if (!box.isValid())
            box = QRectF(0, 0, 612, 792);

        const int rotation = ((int(document->resolve(pageDict.value(QStringLiteral("Rotate"))).toInt(0)) % 360) + 360) % 360;
        QSizeF size = box.size();
        if (rotation == 90 || rotation == 270)
            size = QSizeF(size.height(), size.width());

        Artboard artboard;
        artboard.name = QStringLiteral("Page %1").arg(i + 1);
        artboard.rect = QRectF(QPointF(xOffset, 0), size);
        artboard.background = Qt::white;
        artboards.push_back(artboard);

        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = artboard.name;
        layer.layerColor = nextLayerColor(i);
        const QUuid layerId = layer.id;
        result.objects.push_back(layer);

        // llx/lly/ury flip PDF's y-up page space to this artboard's y-down local space.
        QTransform flip(1, 0, 0, -1, -box.left(), box.bottom());
        if (rotation == 90)
            flip *= QTransform(0, 1, -1, 0, box.height(), 0);
        else if (rotation == 180)
            flip *= QTransform(-1, 0, 0, -1, box.width(), box.height());
        else if (rotation == 270)
            flip *= QTransform(0, -1, 1, 0, 0, box.width());
        const QTransform pageTransform = flip * QTransform::fromTranslate(xOffset, 0);

        const Dict resources = document->resolve(pageDict.value(QStringLiteral("Resources"))).toDict();
        const QByteArray content = concatenatedContent(*document, pageDict, warnings);
        interpreter.runPage(content, resources, pageTransform, artboard.rect, layerId);

        xOffset += size.width() + gap;
    }

    result.setArtboards(artboards);
    if (!artboards.empty()) {
        result.size = artboards.front().rect.size();
        result.background = artboards.front().background;
    }
    if (warnings)
        warnings->removeDuplicates();
    return result;
}
}

namespace PdfImporter {

bool canRead(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(1024);
    return head.indexOf("%PDF-") >= 0;
}

VectorDocument parse(const QByteArray &data, QStringList *warnings)
{
    QStringList localWarnings;
    const qsizetype pdfMarker = data.indexOf("%PDF-");
    if (pdfMarker < 0 || pdfMarker > 1024)
        throw FileError(QStringLiteral("This is not a PDF file."));

    std::unique_ptr<Pdf::Document> document = Pdf::Document::load(data, &localWarnings);
    VectorDocument result = buildDocument(std::move(document), &localWarnings);

    lastWarningList = localWarnings;
    if (warnings)
        *warnings = localWarnings;
    qCInfo(lcIO).noquote() << "imported a PDF with" << result.artboards.size() << "page(s) and" << localWarnings.size() << "warning(s)";
    return result;
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcIO).noquote() << "cannot read" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    const QByteArray data = file.readAll();
    try {
        return parse(data, warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

QStringList lastWarnings()
{
    return lastWarningList;
}

}
