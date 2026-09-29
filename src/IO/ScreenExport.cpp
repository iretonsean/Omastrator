#include "IO/ScreenExport.h"
#include "IO/DocumentExporter.h"
#include "IO/SvgExporter.h"
#include "Rendering/VectorRenderer.h"
#include <QDir>
#include <QImageWriter>
#include <QSet>
#include <map>
#include <cmath>

namespace {
QString sanitize(const QString &name)
{
    QString safe = name;
    for (const QChar bad : QStringLiteral("/\\:*?\"<>|"))
        safe.replace(bad, QChar('_'));
    safe = safe.trimmed();
    if (safe.isEmpty())
        return QStringLiteral("Untitled");
    // "." and ".." are directories, not names: a page called ".." would write outside the folder.
    if (safe.count(QChar('.')) == safe.size())
        return QStringLiteral("_");
    return safe;
}

// `name`, or `name 2`, `name 3`... when `taken` has it (compared without case, as some disks do).
QString distinct(const QString &name, QSet<QString> &taken)
{
    QString candidate = name;
    for (int number = 2; taken.contains(candidate.toLower()); ++number)
        candidate = QStringLiteral("%1 %2").arg(name).arg(number);
    taken.insert(candidate.toLower());
    return candidate;
}

bool isVectorFormat(const QString &format)
{
    return format == QLatin1String("svg") || format == QLatin1String("pdf");
}

// A subject to export: its own flattened document (moved to the origin), a safe file name and, with
// two or more pages, the folder its page gets.
struct Subject {
    QString name;
    QString folder;
    VectorDocument document;
};

std::vector<Subject> subjects(const VectorDocument &document, const std::vector<QUuid> &artboardIds, const std::vector<QUuid> &assetIds)
{
    std::vector<Subject> result;
    const std::vector<Page> pages = document.allPages();
    // Two pages whose names sanitise alike ("A/B", "A_B") still get folders of their own.
    QSet<QString> takenFolders;
    std::map<QUuid, QString> folders;
    if (pages.size() > 1) {
        for (const Page &page : pages)
            folders[page.id] = distinct(sanitize(page.name), takenFolders);
    }
    const auto folderOf = [&](const QUuid &page) { return pages.size() > 1 ? folders[document.resolvePage(page)] : QString(); };
    for (const QUuid &id : artboardIds) {
        for (const Page &page : pages) {
            VectorDocument shown = document;
            shown.currentPage = page.id;
            const int index = shown.artboardIndex(id);
            if (index < 0)
                continue;
            if (!shown.artboard(index).exported)
                break;
            result.push_back({sanitize(shown.artboard(index).name), folderOf(page.id), shown.artboards.empty() ? shown : shown.artboardDocument(index)});
            break;
        }
    }
    for (const QUuid &id : assetIds) {
        const VectorObject *object = document.find(id);
        if (!object)
            continue;
        const QUuid page = document.pageOf(id);
        VectorDocument shown = document;
        shown.currentPage = page;
        result.push_back({sanitize(object->name), folderOf(page), shown.croppedTo({id})});
    }
    // Files sharing a folder and a name would overwrite each other.
    QSet<QString> takenFiles;
    for (Subject &item : result) {
        const QString base = item.name;
        for (int number = 2; takenFiles.contains((item.folder + QLatin1Char('/') + item.name).toLower()); ++number)
            item.name = QStringLiteral("%1 %2").arg(base).arg(number);
        takenFiles.insert((item.folder + QLatin1Char('/') + item.name).toLower());
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

QStringList run(const VectorDocument &document, const std::vector<QUuid> &artboardIds, const std::vector<QUuid> &assetIds,
                const Settings &settings, const std::function<void(const Progress &)> &progress, QStringList *skipped)
{
    QStringList written;
    const std::vector<Subject> items = subjects(document, artboardIds, assetIds);
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
        const QString folder = item.folder.isEmpty() ? settings.folder : QDir(settings.folder).filePath(item.folder);
        QDir().mkpath(folder);
        const bool transparent = item.document.background.alpha() == 0;
        for (const QString &format : settings.formats) {
            const QString ext = format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
            const auto skip = [&](const FileError &error) {
                // One file's failure (too large at that scale, an unwritable folder) doesn't stop the rest.
                if (skipped && !skipped->contains(error.message()))
                    skipped->append(error.message());
                report(item.name);
            };
            if (format == QLatin1String("svg") || format == QLatin1String("pdf")) {
                const bool svg = format == QLatin1String("svg");
                const QString path = QDir(folder).filePath(item.name + (svg ? QStringLiteral(".svg") : QStringLiteral(".pdf")));
                try {
                    if (svg)
                        SvgExporter::write(item.document, path, {false, !transparent});
                    else
                        DocumentExporter::writePdf(item.document, path);
                    written << path;
                    report(item.name);
                } catch (const FileError &error) {
                    skip(error);
                }
                continue;
            }
            for (double scale : scales) {
                const QString path = QDir(folder).filePath(item.name + scaleSuffix(scale) + QStringLiteral(".") + ext);
                try {
                    if (format == QLatin1String("webp")) {
                        // Fails cleanly, file by file, when the plugin isn't installed.
                        const QImage image = DocumentExporter::renderPage(item.document, scale, transparent);
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
                } catch (const FileError &error) {
                    skip(error);
                }
            }
        }
    }
    return written;
}
}
