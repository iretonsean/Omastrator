#include "Document/FontFeatures.h"
#include "Document/TextLayout.h"
#include "Document/VectorDocument.h"
#include <QFontDatabase>
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <utility>

namespace {
const std::array<std::pair<TextAlignment, const char *>, 7> alignmentNames{{
    {TextAlignment::left, "left"}, {TextAlignment::center, "center"}, {TextAlignment::right, "right"},
    {TextAlignment::justify, "justify"}, {TextAlignment::justifyAll, "justifyAll"},
    {TextAlignment::justifyCenter, "justifyCenter"}, {TextAlignment::justifyRight, "justifyRight"},
}};
const std::array<std::pair<TextKerning, const char *>, 2> kerningNames{{{TextKerning::metrics, "metrics"}, {TextKerning::none, "none"}}};
const std::array<std::pair<TextCase, const char *>, 3> caseNames{{
    {TextCase::normal, "normal"}, {TextCase::allCaps, "allCaps"}, {TextCase::smallCaps, "smallCaps"},
}};

template <typename Enum, size_t count>
QString nameOf(const std::array<std::pair<Enum, const char *>, count> &names, Enum value)
{
    for (const auto &[candidate, name] : names) {
        if (candidate == value)
            return QString::fromLatin1(name);
    }
    return QString::fromLatin1(names.front().second);
}

template <typename Enum, size_t count>
std::optional<Enum> valueOf(const std::array<std::pair<Enum, const char *>, count> &names, const QString &raw)
{
    for (const auto &[value, name] : names) {
        if (raw == QLatin1String(name))
            return value;
    }
    return std::nullopt;
}

// Style names say their weight when the family isn't installed.
const std::array<std::pair<const char *, int>, 13> weightWords{{
    {"thin", 100}, {"hairline", 100}, {"extralight", 200}, {"ultralight", 200}, {"light", 300}, {"book", 400},
    {"medium", 500}, {"semibold", 600}, {"demibold", 600}, {"extrabold", 800}, {"ultrabold", 800}, {"bold", 700}, {"black", 900},
}};

int weightFromName(const QString &style)
{
    const QString squashed = style.toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    for (const auto &[word, weight] : weightWords) {
        if (squashed.contains(QLatin1String(word)))
            return weight;
    }
    if (squashed.contains(QLatin1String("heavy")))
        return 900;
    return 400;
}

bool italicFromName(const QString &style)
{
    const QString lower = style.toLower();
    return lower.contains(QLatin1String("italic")) || lower.contains(QLatin1String("oblique"));
}

QString weightName(int weight)
{
    static const std::array<std::pair<int, const char *>, 9> names{{
        {100, "Thin"}, {200, "ExtraLight"}, {300, "Light"}, {400, "Regular"}, {500, "Medium"},
        {600, "SemiBold"}, {700, "Bold"}, {800, "ExtraBold"}, {900, "Black"},
    }};
    const auto nearest = std::min_element(names.begin(), names.end(), [weight](const auto &a, const auto &b) {
        return std::abs(a.first - weight) < std::abs(b.first - weight);
    });
    return QString::fromLatin1(nearest->second);
}

struct Laid {
    TextContent text;
    QPainterPath outline;
    std::vector<std::pair<std::optional<QColor>, QPainterPath>> fills;
    QRectF frame;
    bool overflows = false;
};

// Painting asks for the same outlines many times a second; a short memory of recent ones.
const Laid &laidOut(const TextContent &text)
{
    thread_local std::deque<Laid> recent;
    for (const Laid &laid : recent) {
        if (laid.text == text)
            return laid;
    }
    const TextLayout layout(text);
    recent.push_front({text, layout.outline(), layout.fills(), layout.frame(), layout.overflows()});
    if (recent.size() > 48)
        recent.pop_back();
    return recent.front();
}
}

QString rawValue(TextAlignment alignment)
{
    return nameOf(alignmentNames, alignment);
}

std::optional<TextAlignment> textAlignment(const QString &raw)
{
    return valueOf(alignmentNames, raw);
}

bool isJustified(TextAlignment alignment)
{
    return alignment == TextAlignment::justify || alignment == TextAlignment::justifyAll || alignment == TextAlignment::justifyCenter
        || alignment == TextAlignment::justifyRight;
}

QString rawValue(TextKerning kerning)
{
    return nameOf(kerningNames, kerning);
}

std::optional<TextKerning> textKerning(const QString &raw)
{
    return valueOf(kerningNames, raw);
}

QString rawValue(TextCase textCase)
{
    return nameOf(caseNames, textCase);
}

std::optional<TextCase> textCase(const QString &raw)
{
    return valueOf(caseNames, raw);
}

bool CharacterFormat::isBold() const
{
    const int weight = QFontDatabase::styles(family).contains(style) ? QFontDatabase::weight(family, style) : weightFromName(style);
    return weight >= 600;
}

bool CharacterFormat::isItalic() const
{
    return QFontDatabase::styles(family).contains(style) ? QFontDatabase::italic(family, style) : italicFromName(style);
}

QString TextContent::styleFor(const QString &family, int weight, bool italic)
{
    const QStringList styles = QFontDatabase::styles(family);
    QString best;
    int bestCost = std::numeric_limits<int>::max();
    for (const QString &candidate : styles) {
        // Only a face of the right slant within a step of the weight will do.
        const int off = std::abs(QFontDatabase::weight(family, candidate) - weight);
        if (QFontDatabase::italic(family, candidate) != italic || off > 100)
            continue;
        // Condensed and other widths come last, so plain faces win ties.
        const int cost = off * 100 + int(candidate.size());
        if (cost < bestCost) {
            bestCost = cost;
            best = candidate;
        }
    }
    if (!best.isEmpty())
        return best;
    const QString name = weightName(weight);
    if (!italic)
        return name;
    return name == QLatin1String("Regular") ? QStringLiteral("Italic") : name + QStringLiteral(" Italic");
}

QFont CharacterFormat::font(double pixelsPerPoint, TextKerning kerning) const
{
    QFont font(family);
    if (QFontDatabase::styles(family).contains(style)) {
        // Weight and slant too, so the match and QFontInfo agree with the face.
        font.setWeight(QFont::Weight(QFontDatabase::weight(family, style)));
        font.setItalic(QFontDatabase::italic(family, style));
        font.setStyleName(style);
    } else {
        // A face the family lacks is made up from its weight and slant.
        font.setWeight(QFont::Weight(weightFromName(style)));
        font.setItalic(italicFromName(style));
    }
    font.setKerning(kerning == TextKerning::metrics);
    font.setCapitalization(textCase == TextCase::allCaps ? QFont::AllUppercase : textCase == TextCase::smallCaps ? QFont::SmallCaps : QFont::MixedCase);
    // Outlines at the design size: point sizes would follow the screen's DPI.
    const int pixels = std::max(1, int(std::lround(size * pixelsPerPoint)));
    font.setPixelSize(pixels);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking / 1000 * pixels);
    font.setHintingPreference(QFont::PreferNoHinting);
    FontFeatures::apply(font, features);
    return font;
}

QFont TextContent::font() const
{
    return character().font(std::max(1.0, double(std::lround(size))) / size, kerning);
}

QPainterPath TextContent::outline() const
{
    return laidOut(*this).outline;
}

std::vector<std::pair<std::optional<QColor>, QPainterPath>> TextContent::fills() const
{
    return laidOut(*this).fills;
}

bool TextContent::overflows() const
{
    return area && laidOut(*this).overflows;
}

QRectF TextContent::frame() const
{
    return laidOut(*this).frame;
}

void TextContent::replaceKerns(int from, int to, int length)
{
    std::map<int, double> moved;
    for (const auto &[at, kern] : kerns) {
        if (at <= from)
            moved[at] = kern;
        else if (at >= to)
            moved[at - (to - from) + length] = kern;
    }
    kerns = std::move(moved);
}
