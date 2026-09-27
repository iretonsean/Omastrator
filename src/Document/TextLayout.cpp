#include "Document/TextLayout.h"
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QRawFont>
#include <QTextLayout>
#include <algorithm>
#include <cmath>
#include <limits>

struct TextLayout::Paragraph {
    int start = 0;
    ParagraphFormat format;
    QTextLayout layout;
    std::vector<QTextLine> lines;
};

namespace {
Qt::Alignment qtAlignment(TextAlignment alignment)
{
    switch (alignment) {
    case TextAlignment::center:
        return Qt::AlignHCenter;
    case TextAlignment::right:
        return Qt::AlignRight;
    case TextAlignment::justify:
    case TextAlignment::justifyAll:
    case TextAlignment::justifyCenter:
    case TextAlignment::justifyRight:
        return Qt::AlignJustify;
    default:
        return Qt::AlignLeft;
    }
}

bool isSpace(QChar character)
{
    return character.isSpace() && character != QChar::Nbsp;
}
}

TextLayout::TextLayout(const TextContent &text) : m_text(text)
{
    m_formats.push_back(text.character());
    m_formatOf.assign(size_t(text.text.size()), 0);
    for (const TextRun &run : text.runs) {
        auto found = std::find(m_formats.begin(), m_formats.end(), run.format);
        if (found == m_formats.end())
            found = m_formats.insert(m_formats.end(), run.format);
        const int index = int(found - m_formats.begin());
        for (int at = std::max(0, run.start); at < std::min(run.start + run.length, int(m_formatOf.size())); ++at)
            m_formatOf[size_t(at)] = index;
    }
    // Laid out in whole pixels; sizes that miss the object's pixel grid get a finer one.
    m_scale = text.size / std::max(1.0, double(std::lround(text.size)));
    for (const CharacterFormat &format : m_formats) {
        const double pixels = format.size / m_scale;
        if (std::abs(pixels - std::round(pixels)) > 1e-6) {
            m_scale = text.size / std::max(1.0, double(std::lround(text.size * 16)));
            break;
        }
    }
    const double perPoint = 1 / m_scale;
    for (const CharacterFormat &format : m_formats) {
        const QFontMetricsF metrics(format.font(perPoint, text.kerning));
        m_formatAscents.push_back(metrics.ascent() * m_scale);
    }
    const QFont font = text.character().font(perPoint, text.kerning);
    m_horizontal = m_scale * std::max(1.0, text.horizontalScale) / 100;
    const QFontMetricsF metrics(font);
    m_ascent = metrics.ascent() * m_scale;
    m_descent = metrics.descent() * m_scale;
    const bool area = text.area.has_value();
    const double areaWidth = area ? std::max(1.0, text.area->width()) : 0;
    const QStringList paragraphs = text.text.split(QLatin1Char('\n'));
    double baseline = 0;
    int start = 0;
    for (int index = 0; index < paragraphs.size(); ++index) {
        auto paragraph = std::make_unique<Paragraph>();
        paragraph->start = start;
        paragraph->format = text.paragraphAt(index);
        const ParagraphFormat &format = paragraph->format;
        QTextLayout &layout = paragraph->layout;
        layout.setText(paragraphs[index]);
        layout.setFont(font);
        QTextOption option;
        option.setUseDesignMetrics(true);
        option.setWrapMode(area ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        option.setAlignment(area ? qtAlignment(format.alignment) : Qt::AlignLeft);
        layout.setTextOption(option);
        layout.setFormats(characterFormats(start, int(paragraphs[index].size())));
        if (index > 0)
            baseline += m_paragraphs.back()->format.spaceAfter + format.spaceBefore;
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            const bool firstLine = paragraph->lines.empty();
            const double indent = format.leftIndent + (firstLine ? format.firstLineIndent : 0);
            if (area)
                line.setLineWidth(std::max(1.0, (areaWidth - indent - format.rightIndent) / m_horizontal));
            else
                line.setLineWidth(1e7);
            line.setPosition(QPointF(indent / m_horizontal, 0));
            Line placed;
            placed.start = start + line.textStart();
            placed.length = line.textLength();
            // Area type's first baseline sits its line's ascent below the top; later ones a leading apart.
            if (!m_lines.empty())
                baseline += leadingOf(format, placed.start, placed.length);
            else if (area)
                baseline += ascentOf(placed.start, placed.length);
            placed.paragraph = index;
            placed.index = int(paragraph->lines.size());
            placed.baseline = baseline;
            placed.ascent = line.ascent() * m_scale;
            placed.descent = line.descent() * m_scale;
            placed.lastInParagraph = false;
            m_lines.push_back(placed);
            paragraph->lines.push_back(line);
        }
        layout.endLayout();
        if (!m_lines.empty())
            m_lines.back().lastInParagraph = true;
        start += int(paragraphs[index].size()) + 1;
        m_paragraphs.push_back(std::move(paragraph));
    }
    for (Line &line : m_lines) {
        const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
        const TextAlignment alignment = paragraph.format.alignment;
        const QString &content = paragraph.layout.text();
        int end = line.start + line.length - paragraph.start;
        while (end > line.start - paragraph.start && isSpace(content[end - 1]))
            --end;
        const double width = rawX(line, end + paragraph.start) - rawX(line, line.start);
        // Point type hangs from the origin as its alignment says.
        if (!area && (alignment == TextAlignment::center || alignment == TextAlignment::right))
            line.offset = alignment == TextAlignment::center ? -rawWidth(line) / 2 : -rawWidth(line);
        // A justified paragraph's last line can sit centred or flush right.
        if (area && line.lastInParagraph && (alignment == TextAlignment::justifyCenter || alignment == TextAlignment::justifyRight)) {
            const double available = text.area->width() - paragraph.format.leftIndent - paragraph.format.rightIndent
                - (line.index == 0 ? paragraph.format.firstLineIndent : 0);
            line.offset = std::max(0.0, available - width) * (alignment == TextAlignment::justifyCenter ? 0.5 : 1);
        }
        line.left = rawX(line, line.start);
        line.right = std::max(line.left, rawX(line, end + paragraph.start));
        line.right += shift(line, line.right);
        if (area && text.area->height() > 0 && line.baseline + line.descent > text.area->height() + 0.01) {
            line.hidden = true;
            m_overflows = true;
        }
    }
}

TextLayout::~TextLayout() = default;

// Runs and manual kerns as QTextLayout formats, for one paragraph's characters.
QList<QTextLayout::FormatRange> TextLayout::characterFormats(int start, int length) const
{
    QList<QTextLayout::FormatRange> ranges;
    const double perPoint = 1 / m_scale;
    // A manual kern widens the gap after the character before the one it moves.
    const auto kernAfter = [&](int at) {
        const auto found = m_text.kerns.find(at + 1);
        return found == m_text.kerns.end() ? 0.0 : found->second;
    };
    for (int local = 0; local < length;) {
        const int format = m_formatOf[size_t(start + local)];
        const double kern = kernAfter(start + local);
        int end = local + 1;
        while (end < length && m_formatOf[size_t(start + end)] == format && kernAfter(start + end) == kern)
            ++end;
        if (format != 0 || kern != 0) {
            const CharacterFormat &character = m_formats[size_t(format)];
            const QFont font = character.font(perPoint, m_text.kerning);
            QTextCharFormat made;
            made.setFont(font, QTextCharFormat::FontPropertiesAll);
            made.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            made.setFontLetterSpacing((character.tracking + kern) / 1000 * font.pixelSize());
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            QMap<QFont::Tag, quint32> features;
            for (const QFont::Tag &tag : font.featureTags())
                features.insert(tag, font.featureValue(tag));
            made.setFontFeatures(features);
#endif
            ranges.append({local, end - local, made});
        }
        local = end;
    }
    return ranges;
}

// Auto leading is 120 % of the line's largest size.
double TextLayout::leadingOf(const ParagraphFormat &format, int start, int length) const
{
    if (format.leading)
        return *format.leading;
    double size = 0;
    for (int at = start; at < start + length && at < int(m_formatOf.size()); ++at)
        size = std::max(size, m_formats[size_t(m_formatOf[size_t(at)])].size);
    if (size <= 0)
        size = m_formatOf.empty() ? m_text.size : m_formats[size_t(m_formatOf[size_t(std::clamp(start, 0, int(m_formatOf.size()) - 1))])].size;
    return size * 1.2;
}

double TextLayout::ascentOf(int start, int length) const
{
    double ascent = 0;
    for (int at = start; at < start + length && at < int(m_formatOf.size()); ++at)
        ascent = std::max(ascent, m_formatAscents[size_t(m_formatOf[size_t(at)])]);
    return ascent > 0 ? ascent : m_ascent;
}

double TextLayout::rawWidth(const Line &line) const
{
    return rawX(line, line.start + line.length) - rawX(line, line.start);
}

// A position's x before justify-all stretches the last line.
double TextLayout::rawX(const Line &line, int position) const
{
    const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
    return paragraph.lines[size_t(line.index)].cursorToX(position - paragraph.start) * m_horizontal + line.offset;
}

// Justified lines meet both edges exactly: justify-all spreads a paragraph's last
// line over its spaces, or its letters, and every justified line gets what Qt left over.
double TextLayout::shift(const Line &line, double x) const
{
    const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
    const ParagraphFormat &format = paragraph.format;
    const bool justified = format.alignment == TextAlignment::justifyAll || (isJustified(format.alignment) && !line.lastInParagraph);
    if (!m_text.area || !justified || line.length < 2)
        return 0;
    const QString &content = paragraph.layout.text();
    const int from = line.start - paragraph.start;
    int to = from + line.length;
    while (to > from && isSpace(content[to - 1]))
        --to;
    if (to - from < 2)
        return 0;
    const double available = m_text.area->width() - format.leftIndent - format.rightIndent - (line.index == 0 ? format.firstLineIndent : 0);
    const double extra = available - (rawX(line, to + paragraph.start) - rawX(line, line.start));
    if (extra <= 0)
        return 0;
    std::vector<int> stops;
    for (int index = from; index < to; ++index) {
        if (isSpace(content[index]))
            stops.push_back(index + 1);
    }
    if (stops.empty()) {
        // One long word only spreads when justify-all asks for its last line.
        if (!line.lastInParagraph)
            return 0;
        for (int index = from + 1; index < to; ++index)
            stops.push_back(index);
    }
    int passed = 0;
    for (const int stop : stops) {
        if (rawX(line, stop + paragraph.start) <= x + 1e-6)
            ++passed;
    }
    return extra * passed / double(stops.size());
}

int TextLayout::lineOf(int position) const
{
    for (size_t index = 0; index < m_lines.size(); ++index) {
        const Line &line = m_lines[index];
        if (position < line.start)
            return int(std::max<size_t>(index, 1) - 1);
        if (position < line.start + line.length || (position == line.start + line.length && line.lastInParagraph))
            return int(index);
    }
    return int(m_lines.size()) - 1;
}

double TextLayout::xAt(int position) const
{
    if (m_lines.empty())
        return 0;
    const Line &line = m_lines[size_t(lineOf(position))];
    const double x = rawX(line, std::clamp(position, line.start, line.start + line.length));
    return x + shift(line, x);
}

double TextLayout::xAt(int position, int line) const
{
    if (line < 0 || line >= int(m_lines.size()))
        return xAt(position);
    const Line &on = m_lines[size_t(line)];
    const double x = rawX(on, std::clamp(position, on.start, on.start + on.length));
    return x + shift(on, x);
}

int TextLayout::positionAt(QPointF local) const
{
    if (m_lines.empty())
        return 0;
    size_t chosen = 0;
    for (size_t index = 0; index < m_lines.size(); ++index) {
        if (m_lines[index].hidden)
            break;
        chosen = index;
        if (local.y() <= m_lines[index].baseline + m_lines[index].descent)
            break;
    }
    const Line &line = m_lines[chosen];
    // A wrapped line's end is the next line's start.
    const int last = line.lastInParagraph ? line.start + line.length : std::max(line.start, line.start + line.length - 1);
    int best = line.start;
    double distance = std::numeric_limits<double>::infinity();
    for (int position = line.start; position <= last; ++position) {
        const double x = rawX(line, position);
        const double gap = std::abs(x + shift(line, x) - local.x());
        if (gap < distance) {
            distance = gap;
            best = position;
        }
    }
    return best;
}

std::vector<std::pair<std::optional<QColor>, QPainterPath>> TextLayout::fills() const
{
    std::vector<std::pair<std::optional<QColor>, QPainterPath>> pieces;
    std::vector<QPainterPath> rules;
    const auto piece = [&](const std::optional<QColor> &fill) -> size_t {
        for (size_t index = 0; index < pieces.size(); ++index) {
            if (pieces[index].first == fill)
                return index;
        }
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        pieces.push_back({fill, path});
        rules.emplace_back();
        return pieces.size() - 1;
    };
    // The object's own colour comes first, even when no glyph takes it.
    piece(std::nullopt);
    const double vertical = m_scale * std::max(1.0, m_text.verticalScale) / 100;
    for (size_t lineIndex = 0; lineIndex < m_lines.size(); ++lineIndex) {
        const Line &line = m_lines[lineIndex];
        if (line.hidden || line.length == 0)
            continue;
        const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
        const QTextLine &qline = paragraph.lines[size_t(line.index)];
        // One stretch of one format at a time: each has its own shift, colour and rules.
        for (int from = line.start; from < line.start + line.length;) {
            const int format = m_formatOf[size_t(from)];
            int to = from + 1;
            while (to < line.start + line.length && m_formatOf[size_t(to)] == format)
                ++to;
            const CharacterFormat &character = m_formats[size_t(format)];
            const size_t into = piece(character.fill);
            const double baseline = line.baseline - character.baselineShift;
            for (const QGlyphRun &run : qline.glyphRuns(from - paragraph.start, to - from)) {
                const QRawFont font = run.rawFont();
                const QList<quint32> glyphs = run.glyphIndexes();
                const QList<QPointF> positions = run.positions();
                for (qsizetype glyph = 0; glyph < glyphs.size(); ++glyph) {
                    const QPointF at = positions[glyph];
                    const double x = at.x() * m_horizontal + line.offset;
                    const QTransform place(m_horizontal, 0, 0, vertical, x + shift(line, x), baseline + (at.y() - qline.ascent()) * vertical);
                    pieces[into].second.addPath(place.map(font.pathForGlyph(glyphs[glyph])));
                }
            }
            if (character.underline || character.strikethrough) {
                const double left = xAt(from, int(lineIndex));
                const double right = std::min(line.right, xAt(to, int(lineIndex)));
                if (right > left) {
                    const QFontMetricsF metrics(character.font(1 / m_scale, m_text.kerning));
                    const double thickness = std::max(0.5, metrics.lineWidth() * m_scale);
                    if (character.underline)
                        rules[into].addRect(QRectF(left, baseline + metrics.underlinePos() * m_scale, right - left, thickness));
                    if (character.strikethrough)
                        rules[into].addRect(QRectF(left, baseline - metrics.strikeOutPos() * m_scale - thickness / 2, right - left, thickness));
                }
            }
            from = to;
        }
    }
    for (size_t index = 0; index < pieces.size(); ++index) {
        if (rules[index].isEmpty())
            continue;
        // United, so a rule across a descender leaves no hole.
        QPainterPath united = pieces[index].second.united(rules[index]);
        united.setFillRule(Qt::WindingFill);
        pieces[index].second = united;
    }
    if (pieces.size() > 1 && pieces.front().second.isEmpty())
        pieces.erase(pieces.begin());
    return pieces;
}

QPainterPath TextLayout::outline() const
{
    const auto pieces = fills();
    if (pieces.size() == 1)
        return pieces.front().second;
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const auto &[fill, piece] : pieces)
        path.addPath(piece);
    return path;
}

int TextLayout::glyphCount() const
{
    int count = 0;
    for (const Line &line : m_lines) {
        if (line.hidden || line.length == 0)
            continue;
        const QTextLine &qline = m_paragraphs[size_t(line.paragraph)]->lines[size_t(line.index)];
        for (const QGlyphRun &run : qline.glyphRuns())
            count += int(run.glyphIndexes().size());
    }
    return count;
}

QRectF TextLayout::frame() const
{
    if (m_text.area) {
        double height = m_text.area->height();
        if (height <= 0)
            height = std::max(m_lines.empty() ? 0 : m_lines.back().baseline + m_lines.back().descent, m_ascent + m_descent);
        return QRectF(0, 0, m_text.area->width(), height);
    }
    const QRectF glyphs = outline().boundingRect();
    if (!glyphs.isEmpty())
        return glyphs;
    return QRectF(0, -m_ascent, 1, m_ascent + m_descent);
}
