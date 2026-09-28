#include "IO/PenpotImporterParts.h"
#include <QJsonArray>

namespace {

CharacterFormat characterFormatFor(const QJsonObject &span)
{
    CharacterFormat format;
    const QString family = span.value(QStringLiteral("fontFamily")).toString();
    if (!family.isEmpty())
        format.family = family;
    bool ok = false;
    const double size = span.value(QStringLiteral("fontSize")).toString().toDouble(&ok);
    if (ok)
        format.size = size;
    const int weight = span.value(QStringLiteral("fontWeight")).toString().toInt();
    const bool italic = span.value(QStringLiteral("fontStyle")).toString() == QLatin1String("italic");
    QStringList face;
    if (weight >= 600)
        face << QStringLiteral("Bold");
    if (italic)
        face << QStringLiteral("Italic");
    format.style = face.isEmpty() ? QStringLiteral("Regular") : face.join(QLatin1Char(' '));
    double spacing = 0;
    if (span.contains(QStringLiteral("letterSpacing"))) {
        bool spacingOk = false;
        spacing = span.value(QStringLiteral("letterSpacing")).toString().toDouble(&spacingOk);
        if (spacingOk && format.size > 0)
            format.tracking = spacing / format.size * 1000.0;
    }
    const QJsonArray fills = span.value(QStringLiteral("fills")).toArray();
    if (!fills.isEmpty()) {
        const QJsonObject fill = fills.first().toObject();
        if (fill.contains(QStringLiteral("fillColor")))
            format.fill = PenpotImport::color(fill.value(QStringLiteral("fillColor")).toString(), fill.value(QStringLiteral("fillOpacity")).toDouble(1));
    }
    const QString decoration = span.value(QStringLiteral("textDecoration")).toString();
    format.underline = decoration == QLatin1String("underline");
    format.strikethrough = decoration == QLatin1String("line-through");
    if (span.value(QStringLiteral("textTransform")).toString() == QLatin1String("uppercase"))
        format.textCase = TextCase::allCaps;
    return format;
}

TextAlignment alignmentFor(const QString &value)
{
    if (value == QLatin1String("center"))
        return TextAlignment::center;
    if (value == QLatin1String("right"))
        return TextAlignment::right;
    if (value == QLatin1String("justify"))
        return TextAlignment::justify;
    return TextAlignment::left;
}

}

namespace PenpotImport {

void readText(const QJsonObject &content, TextContent &out, QStringList &warnings)
{
    QString text;
    std::vector<TextRun> runs;
    std::vector<std::pair<int, ParagraphFormat>> paragraphStarts;
    bool haveCharacterDefault = false;

    for (const QJsonValue &setValue : content.value(QStringLiteral("children")).toArray()) {
        for (const QJsonValue &paragraphValue : setValue.toObject().value(QStringLiteral("children")).toArray()) {
            const QJsonObject paragraph = paragraphValue.toObject();
            if (!text.isEmpty())
                text += QLatin1Char('\n');
            ParagraphFormat format;
            format.alignment = alignmentFor(paragraph.value(QStringLiteral("textAlign")).toString());
            paragraphStarts.push_back({int(text.size()), format});

            for (const QJsonValue &spanValue : paragraph.value(QStringLiteral("children")).toArray()) {
                const QJsonObject span = spanValue.toObject();
                const QString spanText = span.value(QStringLiteral("text")).toString();
                if (spanText.isEmpty())
                    continue;
                const CharacterFormat characterFormat = characterFormatFor(span);
                if (!haveCharacterDefault) {
                    out.character() = characterFormat;
                    haveCharacterDefault = true;
                }
                runs.push_back({int(text.size()), int(spanText.size()), characterFormat});
                text += spanText;
            }
        }
    }
    out.text = text;
    out.runs = std::move(runs);
    for (const auto &[start, format] : paragraphStarts)
        out.paragraphFormats[out.paragraphOf(start)] = format;
    if (!paragraphStarts.empty())
        out.paragraph() = paragraphStarts.front().second;

    const QString verticalAlign = content.value(QStringLiteral("verticalAlign")).toString();
    if (!verticalAlign.isEmpty() && verticalAlign != QLatin1String("top"))
        warnings << QStringLiteral("Vertical text alignment (center/bottom in the box) was left out.");
}

}
