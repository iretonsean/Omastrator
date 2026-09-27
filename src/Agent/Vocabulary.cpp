#include "Agent/Vocabulary.h"
#include <QDir>
#include <QFile>

namespace Vocabulary {
QString defaultText()
{
    return QStringLiteral(
        "# Omastrator's dictation vocabulary: one term per line. Add your own below.\n"
        "# It is passed to Whisper as the initial prompt, so spelling here is what you get.\n"
        "\n"
        "# Tools\n"
        "Selection\nDirect Selection\nPen\nPencil\nType\nLine Segment\nRectangle\nRounded Rectangle\nEllipse\n"
        "Polygon\nStar\nRotate\nScale\nEyedropper\nHand\nZoom\n"
        "\n"
        "# Commands\n"
        "Align\nDistribute\nBring to Front\nBring Forward\nSend Backward\nSend to Back\nGroup\nUngroup\nDuplicate\n"
        "Undo\nRedo\nFill\nStroke\nStroke Weight\nOpacity\nBlend Mode\n"
        "\n"
        "# Pathfinder and paths\n"
        "Pathfinder\nUnite\nMinus Front\nIntersect\nExclude\nOutline Stroke\nOffset Path\nSimplify\nCompound Path\n"
        "Clipping Mask\nImage Trace\nVectorize\nAnchor Point\nBézier\n"
        "\n"
        "# Type\n"
        "kerning\ntracking\nleading\nbaseline\nPoint Type\nCreate Outlines\n"
        "\n"
        "# Units and colour\n"
        "pt\npx\npoints\npixels\npercent\ndegrees\nhex\nRGB\nHSB\nPantone\n");
}

QString path()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : given;
    return QDir(config).filePath(QStringLiteral("omastrator/vocabulary.txt"));
}

QStringList parse(const QString &text)
{
    QStringList terms;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString term = line.section(QLatin1Char('#'), 0, 0).trimmed();
        if (!term.isEmpty() && !terms.contains(term))
            terms << term;
    }
    return terms;
}

QStringList load()
{
    QFile file(path());
    return parse(file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : defaultText());
}
}
