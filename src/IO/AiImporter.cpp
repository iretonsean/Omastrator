#include "IO/AiImporter.h"
#include "IO/EpsImporter.h"
#include "IO/PdfImporter.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>

namespace {
thread_local QStringList lastWarningList;

// Illustrator can save a PDF-compatible .ai with the artwork left out of the
// PDF content entirely (kept only in its own private data instead). That
// shows up here as a document with no paint at all.
bool isEmptyOfArt(const VectorDocument &document)
{
    for (const VectorObject &object : document.objects) {
        if (object.kind == ObjectKind::path || object.kind == ObjectKind::text || object.kind == ObjectKind::image)
            return false;
    }
    return true;
}
}

namespace AiImporter {

bool canRead(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(64);
    return head.startsWith("%PDF-") || head.startsWith("%!PS-Adobe")
        || (head.size() >= 4 && uchar(head[0]) == 0xC5 && uchar(head[1]) == 0xD0 && uchar(head[2]) == 0xD3 && uchar(head[3]) == 0xC6);
}

VectorDocument parse(const QByteArray &data, QStringList *warnings)
{
    QStringList localWarnings;
    VectorDocument result;
    if (data.startsWith("%PDF-")) {
        result = PdfImporter::parse(data, &localWarnings);
        if (isEmptyOfArt(result)) {
            throw FileError(QStringLiteral("This Illustrator file was saved without PDF content. In Illustrator, turn on "
                                            "“Create PDF Compatible File” in Illustrator Options and save again."));
        }
    } else if (data.startsWith("%!PS-Adobe") || (data.size() >= 4 && uchar(data[0]) == 0xC5 && uchar(data[1]) == 0xD0)) {
        result = EpsImporter::parse(data, &localWarnings);
    } else {
        throw FileError(QStringLiteral("This is not an Illustrator file."));
    }
    lastWarningList = localWarnings;
    if (warnings)
        *warnings = localWarnings;
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
