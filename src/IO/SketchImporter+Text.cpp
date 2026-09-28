#include "IO/SketchImporterParts.h"
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

namespace {

// "Helvetica-BoldOblique" -> family "Helvetica", style "Bold Italic". A
// heuristic over PostScript names, not a real font-metadata lookup: good
// enough to get close, and the app's own missing-font flow catches the rest.
void splitFontName(const QString &postscriptName, QString &family, QString &style)
{
    QString name = postscriptName;
    static const QRegularExpression trailingMT(QStringLiteral("MT$"));
    name.remove(trailingMT);
    const qsizetype dash = name.lastIndexOf(QLatin1Char('-'));
    if (dash < 0) {
        family = name;
        style = QStringLiteral("Regular");
        return;
    }
    const QString head = name.left(dash), tail = name.mid(dash + 1);
    static const QSet<QString> known{QStringLiteral("Regular"),   QStringLiteral("Bold"),      QStringLiteral("Italic"),
                                      QStringLiteral("BoldItalic"), QStringLiteral("Oblique"),   QStringLiteral("BoldOblique"),
                                      QStringLiteral("Light"),     QStringLiteral("Medium"),    QStringLiteral("SemiBold"),
                                      QStringLiteral("Black"),     QStringLiteral("Thin")};
    if (!known.contains(tail)) {
        family = name;
        style = QStringLiteral("Regular");
        return;
    }
    family = head;
    static const QHash<QString, QString> spaced{{QStringLiteral("BoldItalic"), QStringLiteral("Bold Italic")},
                                                 {QStringLiteral("BoldOblique"), QStringLiteral("Bold Italic")},
                                                 {QStringLiteral("Oblique"), QStringLiteral("Italic")}};
    style = spaced.value(tail, tail);
}

TextAlignment alignmentFor(int value)
{
    switch (value) {
    case 1: return TextAlignment::right;
    case 2: return TextAlignment::center;
    case 3: return TextAlignment::justify;
    default: return TextAlignment::left;
    }
}

CharacterFormat characterFormatFor(const QJsonObject &attributes)
{
    CharacterFormat format;
    const QJsonObject font = attributes.value(QStringLiteral("MSAttributedStringFontAttribute")).toObject().value(QStringLiteral("attributes")).toObject();
    const QString postscriptName = font.value(QStringLiteral("name")).toString();
    if (!postscriptName.isEmpty())
        splitFontName(postscriptName, format.family, format.style);
    format.size = font.value(QStringLiteral("size")).toDouble(format.size);
    if (attributes.contains(QStringLiteral("MSAttributedStringColorAttribute")))
        format.fill = SketchImport::color(attributes.value(QStringLiteral("MSAttributedStringColorAttribute")).toObject());
    if (attributes.contains(QStringLiteral("kerning")) && format.size > 0)
        format.tracking = attributes.value(QStringLiteral("kerning")).toDouble() / format.size * 1000.0;
    return format;
}

ParagraphFormat paragraphFormatFor(const QJsonObject &attributes)
{
    ParagraphFormat format;
    const QJsonObject paragraph = attributes.value(QStringLiteral("paragraphStyle")).toObject();
    format.alignment = alignmentFor(paragraph.value(QStringLiteral("alignment")).toInt(0));
    const double lineHeight = paragraph.value(QStringLiteral("maximumLineHeight")).toDouble(0);
    if (lineHeight > 0)
        format.leading = lineHeight;
    return format;
}

}

namespace SketchImport {

void readText(const QJsonObject &layer, TextContent &content, QStringList &warnings)
{
    const QJsonObject attributedString = layer.value(QStringLiteral("attributedString")).toObject();
    content.text = attributedString.value(QStringLiteral("string")).toString();
    const QJsonArray runs = attributedString.value(QStringLiteral("attributes")).toArray();
    bool first = true;
    for (const QJsonValue &value : runs) {
        const QJsonObject run = value.toObject();
        const int location = run.value(QStringLiteral("location")).toInt(0);
        const int length = run.value(QStringLiteral("length")).toInt(0);
        const QJsonObject attributes = run.value(QStringLiteral("attributes")).toObject();
        const CharacterFormat character = characterFormatFor(attributes);
        const ParagraphFormat paragraph = paragraphFormatFor(attributes);
        if (first) {
            content.character() = character;
            content.paragraph() = paragraph;
            first = false;
        }
        if (length <= 0 || location < 0 || location + length > content.text.size())
            continue;
        content.runs.push_back({location, length, character});
        content.paragraphFormats[content.paragraphOf(location)] = paragraph;
    }

    // 0 Flexible (both grow), 1 Fixed width (auto height), 2 Fixed (neither grows).
    const int behaviour = layer.value(QStringLiteral("textBehaviour")).toInt(0);
    const QJsonObject frame = layer.value(QStringLiteral("frame")).toObject();
    const double width = frame.value(QStringLiteral("width")).toDouble();
    const double height = frame.value(QStringLiteral("height")).toDouble();
    if (behaviour == 1)
        content.area = QSizeF(width, 0);
    else if (behaviour == 2)
        content.area = QSizeF(width, height);
    else
        content.area.reset();

    if (layer.value(QStringLiteral("automaticallyDrawOnUnderlyingPath")).toBool(false))
        warnings << QStringLiteral("Type on a Path was left out.");
}

}
