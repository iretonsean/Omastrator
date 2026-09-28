#include "IO/VectorFileImporter.h"
#include "IO/AiImporter.h"
#include "IO/EpsImporter.h"
#include "IO/PdfImporter.h"
#include "IO/SvgImporter.h"
#include <QFileInfo>

namespace VectorFileImporter {

VectorDocument read(const QString &path, QStringList *warnings)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return PdfImporter::read(path, warnings);
    if (suffix == QLatin1String("ai"))
        return AiImporter::read(path, warnings);
    if (suffix == QLatin1String("eps") || suffix == QLatin1String("ps"))
        return EpsImporter::read(path, warnings);
    return SvgImporter::read(path, warnings);
}

VectorDocument readFirstArtboard(const QString &path, QStringList *warnings)
{
    QStringList localWarnings;
    VectorDocument document = read(path, &localWarnings);
    if (document.artboards.size() > 1) {
        localWarnings << QStringLiteral("“%1” has %2 pages; only the first was placed.")
                             .arg(QFileInfo(path).fileName())
                             .arg(document.artboards.size());
        document = document.artboardDocument(0);
    }
    if (warnings)
        *warnings = localWarnings;
    return document;
}

}
