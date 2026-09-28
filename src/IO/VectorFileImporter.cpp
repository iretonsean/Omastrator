#include "IO/VectorFileImporter.h"
#include "IO/AiImporter.h"
#include "IO/EpsImporter.h"
#include "IO/ExcalidrawImporter.h"
#include "IO/FigmaImporter.h"
#include "IO/ImageImporter.h"
#include "IO/PdfImporter.h"
#include "IO/PenpotImporter.h"
#include "IO/SketchImporter.h"
#include "IO/SvgImporter.h"
#include <QFileInfo>
#include <QSet>

namespace VectorFileImporter {

namespace {
bool isLayeredSuffix(const QString &suffix)
{
    static const QSet<QString> layered{QStringLiteral("excalidraw"), QStringLiteral("sketch"), QStringLiteral("penpot")};
    return layered.contains(suffix);
}
}

bool canRead(const QString &path)
{
    return ImageImporter::isVector(path) || isLayeredSuffix(QFileInfo(path).suffix().toLower()) || FigmaImporter::canRead(path);
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("excalidraw"))
        return ExcalidrawImporter::read(path, warnings);
    if (suffix == QLatin1String("sketch"))
        return SketchImporter::read(path, warnings);
    if (suffix == QLatin1String("penpot"))
        return PenpotImporter::read(path, warnings);
    if (suffix == QLatin1String("pdf"))
        return PdfImporter::read(path, warnings);
    if (suffix == QLatin1String("ai"))
        return AiImporter::read(path, warnings);
    if (suffix == QLatin1String("eps") || suffix == QLatin1String("ps"))
        return EpsImporter::read(path, warnings);
    // Checked after the suffixes: sniffing a zip for canvas.fig reads it whole.
    if (!ImageImporter::isVector(path) && FigmaImporter::canRead(path))
        return FigmaImporter::read(path, warnings);
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
