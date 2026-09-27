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
const std::array<std::pair<TextAlignment, const char *>, 5> alignmentNames{{
    {TextAlignment::left, "left"}, {TextAlignment::center, "center"}, {TextAlignment::right, "right"},
    {TextAlignment::justify, "justify"}, {TextAlignment::justifyAll, "justifyAll"},
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
    recent.push_front({text, layout.outline(), layout.frame(), layout.overflows()});
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

bool TextContent::isBold() const
{
    const int weight = QFontDatabase::styles(family).contains(style) ? QFontDatabase::weight(family, style) : weightFromName(style);
    return weight >= 600;
}

bool TextContent::isItalic() const
{
    return QFontDatabase::styles(family).contains(style) ? QFontDatabase::italic(family, style) : italicFromName(style);
}

QString TextContent::styleFor(const QString &family, int weight, bool italic)
{
    const QStringList styles = QFontDatabase::styles(family);
    QString best;
    int bestCost = std::numeric_limits<int>::max();
    for (const QString &candidate : styles) {
        // Condensed and other widths come last, so plain faces win ties.
        const int cost = std::abs(QFontDatabase::weight(family, candidate) - weight) + (QFontDatabase::italic(family, candidate) != italic ? 1000 : 0)
            + int(candidate.size());
        if (cost < bestCost) {
            bestCost = cost;
            best = candidate;
        }
    }
    if (!best.isEmpty() && bestCost < 1000 + 100)
        return best;
    const QString name = weightName(weight);
    if (!italic)
        return name;
    return name == QLatin1String("Regular") ? QStringLiteral("Italic") : name + QStringLiteral(" Italic");
}

QFont TextContent::font() const
{
    QFont font(family);
    if (QFontDatabase::styles(family).contains(style)) {
        font.setStyleName(style);
    } else {
        // A face the family lacks is made up from its weight and slant.
        font.setWeight(QFont::Weight(weightFromName(style)));
        font.setItalic(italicFromName(style));
    }
    font.setKerning(kerning == TextKerning::metrics);
    font.setCapitalization(textCase == TextCase::allCaps ? QFont::AllUppercase : textCase == TextCase::smallCaps ? QFont::SmallCaps : QFont::MixedCase);
    // Outlines at the design size: point sizes would follow the screen's DPI.
    const int pixels = std::max(1, int(std::lround(size)));
    font.setPixelSize(pixels);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking / 1000 * pixels);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QPainterPath TextContent::outline() const
{
    return laidOut(*this).outline;
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
