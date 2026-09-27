#include "IO/DocumentExporter.h"
#include "Logging.h"
#include "Rendering/VectorRenderer.h"
#include <QBuffer>
#include <QFileInfo>
#include <QImageWriter>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <cmath>

namespace {
// Past these a render would not fit in memory on most machines.
constexpr int maximumSide = 30'000;
constexpr qint64 maximumPixels = 200'000'000;

// Every export lands through QSaveFile, so a failure leaves any old file whole.
void save(const QByteArray &bytes, const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qCWarning(lcIO).noquote() << "cannot write" << path + ":" << file.errorString();
        throw FileError(QStringLiteral("“%1” could not be saved: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    }
    qCInfo(lcIO).noquote() << "exported" << bytes.size() << "bytes to" << path;
}

QImage render(const VectorDocument &document, double scale, bool transparent)
{
    if (!(scale > 0) || !std::isfinite(scale))
        throw FileError(QStringLiteral("The export scale must be above zero."));
    // Several artboards: this call exports the first one alone.
    const VectorDocument page = document.artboards.empty() ? document : document.artboardDocument(0);
    const double width = std::ceil(page.size.width() * scale), height = std::ceil(page.size.height() * scale);
    if (width > maximumSide || height > maximumSide || width * height > double(maximumPixels))
        throw FileError(QStringLiteral("%1 × %2 pixels is too large to export. Choose a smaller scale.").arg(width).arg(height));
    QImage image = VectorRenderer::render(page, scale, transparent);
    if (image.isNull())
        throw FileError(QStringLiteral("There was not enough memory to render the artboard."));
    return image;
}

QByteArray encode(const QImage &image, const char *format, int quality, double scale)
{
    QImage tagged = image;
    // Points are 1/72 inch, so the file states the resolution the scale gives.
    const int dotsPerMeter = qRound(72 * scale / 0.0254);
    tagged.setDotsPerMeterX(dotsPerMeter);
    tagged.setDotsPerMeterY(dotsPerMeter);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setQuality(quality);
    if (!writer.write(tagged))
        throw FileError(QStringLiteral("The image could not be encoded: %1").arg(writer.errorString()));
    return bytes;
}
}

namespace DocumentExporter {
Format format(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return Format::pdf;
    if (suffix == QLatin1String("png"))
        return Format::png;
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return Format::jpeg;
    if (suffix == QLatin1String("svg"))
        return Format::svg;
    throw FileError(QStringLiteral("Choose a file name ending in .pdf, .svg, .png or .jpg."));
}

void writePdf(const VectorDocument &document, const QString &path)
{
    // Several artboards: this call exports the first one alone.
    const VectorDocument page = document.artboards.empty() ? document : document.artboardDocument(0);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setCreator(QStringLiteral("Omastrator"));
        writer.setTitle(QFileInfo(path).completeBaseName());
        // One device unit per point: document coordinates draw as they are.
        writer.setResolution(72);
        writer.setPageSize(QPageSize(page.size, QPageSize::Point, QString(), QPageSize::ExactMatch));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        QPainter painter;
        if (!painter.begin(&writer))
            throw FileError(QStringLiteral("The PDF could not be started."));
        VectorRenderer::draw(painter, page, {});
        painter.end();
    }
    save(bytes, path);
}

void writePng(const VectorDocument &document, const QString &path, double scale, bool transparent)
{
    save(encode(render(document, scale, transparent), "png", -1, scale), path);
}

void writeJpeg(const VectorDocument &document, const QString &path, double scale, int quality)
{
    const QImage image = render(document, scale, false);
    // JPEG has no alpha: whatever the paper leaves clear lies on white.
    QImage flattened(image.size(), QImage::Format_RGB888);
    if (flattened.isNull())
        throw FileError(QStringLiteral("There was not enough memory to render the artboard."));
    flattened.fill(Qt::white);
    {
        QPainter painter(&flattened);
        painter.drawImage(0, 0, image);
    }
    save(encode(flattened, "jpeg", std::clamp(quality, 0, 100), scale), path);
}
}
