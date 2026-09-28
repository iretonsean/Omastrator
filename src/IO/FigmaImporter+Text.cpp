#include "IO/FigmaMapperInternal.h"

namespace FigmaMap {
namespace {
TextCase mapTextCase(Context &ctx, const QString &value)
{
    if (value == QLatin1String("UPPER"))
        return TextCase::allCaps;
    if (value == QLatin1String("SMALL_CAPS") || value == QLatin1String("SMALL_CAPS_FORCED"))
        return TextCase::smallCaps;
    if (value == QLatin1String("LOWER") || value == QLatin1String("TITLE")) {
        ctx.warn(QStringLiteral("Lowercase and title-case text came in as regular text."));
        return TextCase::normal;
    }
    return TextCase::normal;
}

// letterSpacing/lineHeight: {units, value}. 1/1000 em, from a percent or pixel value.
double trackingEm(const QVariantMap &node, double fontSize)
{
    const QVariantMap spacing = map(node, "letterSpacing");
    if (spacing.isEmpty())
        return 0;
    const double value = num(spacing, "value");
    const QString units = str(spacing, "units");
    if (units == QLatin1String("PERCENT"))
        return value * 10;
    if (fontSize <= 0)
        return 0;
    return value / fontSize * 1000;
}

std::optional<double> leadingPt(const QVariantMap &node, double fontSize)
{
    const QVariantMap height = map(node, "lineHeight");
    if (height.isEmpty())
        return std::nullopt;
    const double value = num(height, "value");
    const QString units = str(height, "units");
    if (units == QLatin1String("PIXELS"))
        return value;
    if (units == QLatin1String("RAW"))
        return value * fontSize;
    // PERCENT of the normal (auto) leading; 100 is auto itself.
    if (value == 100)
        return std::nullopt;
    return (value / 100) * fontSize * 1.2;
}

void applyBaseFormat(Context &ctx, const QVariantMap &node, CharacterFormat &character, ParagraphFormat &paragraph)
{
    const QVariantMap fontName = map(node, "fontName");
    character.family = str(fontName, "family", character.family);
    character.style = str(fontName, "style", QStringLiteral("Regular"));
    character.size = num(node, "fontSize", character.size);
    character.tracking = trackingEm(node, character.size);
    character.textCase = mapTextCase(ctx, str(node, "textCase", QStringLiteral("ORIGINAL")));
    const QString decoration = str(node, "textDecoration");
    character.underline = decoration == QLatin1String("UNDERLINE");
    character.strikethrough = decoration == QLatin1String("STRIKETHROUGH");
    for (const QVariant &tag : list(node, "toggledOnOTFeatures"))
        character.features[tag.toString().toLower()] = 1;
    for (const QVariant &tag : list(node, "toggledOffOTFeatures"))
        character.features[tag.toString().toLower()] = 0;

    static const std::map<QString, TextAlignment> aligns{{QStringLiteral("LEFT"), TextAlignment::left},
                                                          {QStringLiteral("CENTER"), TextAlignment::center},
                                                          {QStringLiteral("RIGHT"), TextAlignment::right},
                                                          {QStringLiteral("JUSTIFIED"), TextAlignment::justify}};
    const auto align = aligns.find(str(node, "textAlignHorizontal", QStringLiteral("LEFT")));
    paragraph.alignment = align == aligns.end() ? TextAlignment::left : align->second;
    paragraph.leading = leadingPt(node, character.size);
    paragraph.spaceAfter = num(node, "paragraphSpacing");
}
}

void mapText(Context &ctx, const QVariantMap &node, VectorObject &object)
{
    TextContent &text = object.text;
    const QVariantMap textData = map(node, "textData");
    text.text = str(textData, "characters");
    applyBaseFormat(ctx, node, text.character(), text.paragraph());

    const QString autoResize = str(node, "textAutoResize", QStringLiteral("NONE"));
    const QVariantMap size = map(node, "size");
    if (autoResize == QLatin1String("WIDTH_AND_HEIGHT")) {
        text.area.reset();
    } else if (autoResize == QLatin1String("HEIGHT")) {
        text.area = QSizeF(num(size, "x", 100), 0);
    } else {
        text.area = QSizeF(num(size, "x", 100), num(size, "y", 100));
    }

    if (str(node, "textAlignVertical") == QLatin1String("CENTER") || str(node, "textAlignVertical") == QLatin1String("BOTTOM"))
        ctx.warn(QStringLiteral("Vertical text alignment isn't supported yet; text sits at the top of its box."));

    // Style runs: characterStyleIDs is one id per character (0 = the object's own,
    // unstyled, format); styleOverrideTable gives each id's changed fields. A run's
    // format takes only the character-level fields a style override changes
    // (paragraph fields, above, apply to the whole text object).
    const QVariantList styleIDs = list(textData, "characterStyleIDs");
    QVariantMap overrideTable;
    for (const QVariant &entry : list(textData, "styleOverrideTable")) {
        const QVariantMap style = entry.toMap();
        overrideTable[style.value(QStringLiteral("styleID")).toString()] = style;
    }
    const auto addRun = [&](int start, int end, const QString &id) {
        if (start < 0 || id.isEmpty() || id == QLatin1String("0") || !overrideTable.contains(id))
            return;
        TextRun run;
        run.start = start;
        run.length = end - start;
        run.format = text.character();
        ParagraphFormat unusedParagraph;
        applyBaseFormat(ctx, overrideTable.value(id).toMap(), run.format, unusedParagraph);
        text.runs.push_back(run);
    };
    int runStart = -1;
    QString runID;
    for (int i = 0; i < styleIDs.size(); ++i) {
        const QString id = styleIDs[i].toString();
        if (id != runID) {
            addRun(runStart, i, runID);
            runStart = i;
            runID = id;
        }
    }
    addRun(runStart, int(styleIDs.size()), runID);
    text.normalize();
}
}
