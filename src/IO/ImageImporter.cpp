#include "IO/ImageImporter.h"
#include "IO/ImageImporterParts.h"
#include "Logging.h"
#include <QColorSpace>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSet>

namespace {
constexpr int maximumSide = 30'000;
// Qt's default of 256 MB refuses big photos; a 30,000 px square at 8 bytes a pixel fits.
constexpr int allocationLimitMB = 8 * 1024;

QString fileName(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}
}

namespace ImageImporter {
QImage read(const QString &path, QStringList *warnings)
{
    static const bool raised = (QImageReader::setAllocationLimit(allocationLimitMB), true);
    Q_UNUSED(raised)
    if (isVector(path))
        throw FileError(QStringLiteral("“%1” is a vector file: it is placed as paths, not pixels.").arg(fileName(path)));

    QFile probe(path);
    if (probe.open(QIODevice::ReadOnly)) {
        const QByteArray content = probe.readAll();
        if (ImageImport::isPsd(content)) {
            QImage image = ImageImport::readPsdComposite(content);
            if (warnings)
                *warnings << QStringLiteral("Layers were left out; the PSD was placed as a flattened image.");
            qCInfo(lcIO).noquote() << "placed PSD composite" << path << image.width() << "x" << image.height();
            return image;
        }
        if (ImageImport::isHeicOrAvif(content)) {
            QImage image = ImageImport::readHeicOrAvif(path, content);
            if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb)
                image.convertToColorSpace(QColorSpace::SRgb);
            image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
            qCInfo(lcIO).noquote() << "placed HEIC/AVIF" << path << image.width() << "x" << image.height();
            return image;
        }
    }

    QImageReader reader(path);
    // EXIF orientation: photos come in upright.
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        qCWarning(lcIO).noquote() << "cannot read" << path + ":" << reader.errorString();
        throw FileError(QStringLiteral("“%1” is not an image this app can read.").arg(fileName(path)));
    }
    const QSize size = reader.size();
    if (size.isValid() && (size.width() > maximumSide || size.height() > maximumSide))
        throw FileError(QStringLiteral("“%1” is %2 × %3 pixels; images up to %4 pixels a side can be placed.")
                            .arg(fileName(path)).arg(size.width()).arg(size.height()).arg(maximumSide));
    QImage image;
    if (!reader.read(&image) || image.isNull()) {
        qCWarning(lcIO).noquote() << "cannot decode" << path + ":" << reader.errorString();
        throw FileError(QStringLiteral("“%1” could not be read: %2").arg(fileName(path), reader.errorString()));
    }
    if (image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb)
        image.convertToColorSpace(QColorSpace::SRgb);
    // The codec and renderer keep placed images premultiplied.
    image = std::move(image).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        throw FileError(QStringLiteral("There was not enough memory to place “%1”.").arg(fileName(path)));
    qCInfo(lcIO).noquote() << "placed" << path << image.width() << "x" << image.height();
    return image;
}

QStringList nameFilters()
{
    // Formats this Qt build can read, in the order users look for them.
    const QList<QByteArray> supported = QImageReader::supportedImageFormats();
    const QList<std::pair<QString, QStringList>> known{
        {QStringLiteral("SVG"), {QStringLiteral("*.svg"), QStringLiteral("*.svgz")}},
        {QStringLiteral("PDF"), {QStringLiteral("*.pdf")}},
        {QStringLiteral("Adobe Illustrator"), {QStringLiteral("*.ai")}},
        {QStringLiteral("EPS"), {QStringLiteral("*.eps"), QStringLiteral("*.ps")}},
        {QStringLiteral("PNG"), {QStringLiteral("*.png")}},
        {QStringLiteral("JPEG"), {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}},
        {QStringLiteral("TIFF"), {QStringLiteral("*.tif"), QStringLiteral("*.tiff")}},
        {QStringLiteral("WebP"), {QStringLiteral("*.webp")}},
        {QStringLiteral("GIF"), {QStringLiteral("*.gif")}},
        {QStringLiteral("BMP"), {QStringLiteral("*.bmp")}},
        {QStringLiteral("PSD"), {QStringLiteral("*.psd"), QStringLiteral("*.psb")}},
#ifdef OMASTRATOR_HAVE_LIBHEIF
        {QStringLiteral("HEIC"), {QStringLiteral("*.heic"), QStringLiteral("*.heif")}},
        {QStringLiteral("AVIF"), {QStringLiteral("*.avif")}},
#endif
        {QStringLiteral("Excalidraw"), {QStringLiteral("*.excalidraw")}},
        {QStringLiteral("Sketch"), {QStringLiteral("*.sketch")}},
        {QStringLiteral("Penpot"), {QStringLiteral("*.penpot")}},
    };
    // Formats read by our own importers rather than a QImageReader plugin: always offered.
    static const QSet<QByteArray> ownFormats{"svg", "pdf", "ai", "eps", "ps", "psd", "heic", "avif", "excalidraw", "sketch", "penpot"};
    QStringList all, each;
    for (const auto &[name, patterns] : known) {
        const QByteArray format = patterns.front().mid(2).toLatin1();
        if (!ownFormats.contains(format) && !supported.contains(format))
            continue;
        all << patterns;
        each << QStringLiteral("%1 (%2)").arg(name, patterns.join(QLatin1Char(' ')));
    }
    return QStringList{QStringLiteral("Images (%1)").arg(all.join(QLatin1Char(' ')))} + each + QStringList{QStringLiteral("All files (*)")};
}

bool isVector(const QString &path)
{
    static const QSet<QString> vectorSuffixes{QStringLiteral("svg"), QStringLiteral("svgz"), QStringLiteral("pdf"), QStringLiteral("ai"),
                                               QStringLiteral("eps"), QStringLiteral("ps")};
    return vectorSuffixes.contains(QFileInfo(path).suffix().toLower());
}
}
