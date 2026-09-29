#include "IO/ScreenExport.h"
#include "IO/DocumentExporter.h"
#include "IO/SvgExporter.h"
#include "Rendering/VectorRenderer.h"
#include <QDir>
#include <QImageWriter>
#include <cmath>

namespace {
QString sanitize(const QString &name)
{
    QString safe = name;
    for (const QChar bad : QStringLiteral("/\\:*?\"<>|"))
        safe.replace(bad, QChar('_'));
    safe = safe.trimmed();
    return safe.isEmpty() ? QStringLiteral("Untitled") : safe;
}

bool isVectorFormat(const QString &format)
{
    return format == QLatin1String("svg") || format == QLatin1String("pdf");
}

// A subject to export: its own flattened document (moved to the origin) and a safe file name.
struct Subject {
    QString name;
    VectorDocument document;
};

std::vector<Subject> subjects(const VectorDocument &document, const std::vector<int> &artboardIndices, const std::vector<QUuid> &assetIds)
{
    std::vector<Subject> result;
    for (int index : artboardIndices) {
        if (index < 0 || index >= document.artboardCount())
            continue;
        const Artboard board = document.artboard(index);
        if (!board.exported)
            continue;
        result.push_back({sanitize(board.name), document.artboards.empty() ? document : document.artboardDocument(index)});
    }
    for (const QUuid &id : assetIds) {
        const VectorObject *object = document.find(id);
        if (!object)
            continue;
        result.push_back({sanitize(object->name), document.croppedTo({id})});
    }
    return result;
}
}

namespace ScreenExport {
QString scaleSuffix(double scale)
{
    if (std::abs(scale - 1) < 1e-9)
        return {};
    QString text = QString::number(scale, 'g', 6);
    return QStringLiteral("@%1x").arg(text);
}

QStringList run(const VectorDocument &document, const std::vector<int> &artboardIndices, const std::vector<QUuid> &assetIds,
                const Settings &settings, const std::function<void(const Progress &)> &progress)
{
    QStringList written;
    const std::vector<Subject> items = subjects(document, artboardIndices, assetIds);
    const std::vector<double> scales = settings.scales.empty() ? std::vector<double>{1} : settings.scales;
    QDir().mkpath(settings.folder);
    int total = 0;
    for (const QString &format : settings.formats)
        total += int(items.size()) * (isVectorFormat(format) ? 1 : int(scales.size()));
    int done = 0;
    const auto report = [&](const QString &name) {
        if (progress)
            progress({name, ++done, total});
    };
    for (const Subject &item : items) {
        const bool transparent = item.document.background.alpha() == 0;
        for (const QString &format : settings.formats) {
            const QString ext = format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
            try {
                if (format == QLatin1String("svg")) {
                    const QString path = QDir(settings.folder).filePath(item.name + QStringLiteral(".svg"));
                    SvgExporter::write(item.document, path, {false, !transparent});
                    written << path;
                    report(item.name);
                    continue;
                }
                if (format == QLatin1String("pdf")) {
                    const QString path = QDir(settings.folder).filePath(item.name + QStringLiteral(".pdf"));
                    DocumentExporter::writePdf(item.document, path);
                    written << path;
                    report(item.name);
                    continue;
                }
                for (double scale : scales) {
                    const QString path = QDir(settings.folder).filePath(item.name + scaleSuffix(scale) + QStringLiteral(".") + ext);
                    if (format == QLatin1String("webp")) {
                        // Fails cleanly, file by file, when the plugin isn't installed.
                        const QImage image = VectorRenderer::render(item.document, scale, transparent);
                        QImageWriter writer(path, "webp");
                        if (writer.write(image))
                            written << path;
                    } else if (format == QLatin1String("jpg") || format == QLatin1String("jpeg")) {
                        DocumentExporter::writeJpeg(item.document, path, scale, 90);
                        written << path;
                    } else {
                        DocumentExporter::writePng(item.document, path, scale, transparent);
                        written << path;
                    }
                    report(item.name);
                }
            } catch (const FileError &) {
                // One subject's failure (too large, an unwritable folder) doesn't stop the rest.
                report(item.name);
            }
        }
    }
    return written;
}
}
