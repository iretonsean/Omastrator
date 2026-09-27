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
    const QFont font = text.font();
    const double pixels = std::max(1.0, double(font.pixelSize()));
    m_scale = text.size / pixels;
    m_horizontal = m_scale * std::max(1.0, text.horizontalScale) / 100;
    const QFontMetricsF metrics(font);
    m_ascent = metrics.ascent() * m_scale;
    m_descent = metrics.descent() * m_scale;
    const bool area = text.area.has_value();
    const double areaWidth = area ? std::max(1.0, text.area->width()) : 0;
    const double leading = text.effectiveLeading();
    const QStringList paragraphs = text.text.split(QLatin1Char('\n'));
    // Area type's first baseline sits an ascent below the top.
    double baseline = area ? m_ascent : 0;
    int start = 0;
    for (int index = 0; index < paragraphs.size(); ++index) {
        auto paragraph = std::make_unique<Paragraph>();
        paragraph->start = start;
        QTextLayout &layout = paragraph->layout;
        layout.setText(paragraphs[index]);
        layout.setFont(font);
        QTextOption option;
        option.setUseDesignMetrics(true);
        option.setWrapMode(area ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        option.setAlignment(area ? qtAlignment(text.alignment) : Qt::AlignLeft);
        layout.setTextOption(option);
        // A manual kern widens the gap after the character before it.
        QList<QTextLayout::FormatRange> formats;
        for (const auto &[at, kern] : text.kerns) {
            const int local = at - start - 1;
            if (local < 0 || local >= paragraphs[index].size() || kern == 0)
                continue;
            QTextCharFormat format;
            format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
            format.setFontLetterSpacing((text.tracking + kern) / 1000 * pixels);
            formats.append({int(local), 1, format});
        }
        layout.setFormats(formats);
        if (index > 0)
            baseline += text.spaceAfter + text.spaceBefore;
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            const bool firstLine = paragraph->lines.empty();
            const double indent = text.leftIndent + (firstLine ? text.firstLineIndent : 0);
            if (area)
                line.setLineWidth(std::max(1.0, (areaWidth - indent - text.rightIndent) / m_horizontal));
            else
                line.setLineWidth(1e7);
            line.setPosition(QPointF(indent / m_horizontal, 0));
            if (!m_lines.empty())
                baseline += leading;
            Line placed;
            placed.start = start + line.textStart();
            placed.length = line.textLength();
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
        const QString &content = paragraph.layout.text();
        // Point type hangs from the origin as its alignment says.
        if (!area && (text.alignment == TextAlignment::center || text.alignment == TextAlignment::right)) {
            const double width = rawX(line, line.start + line.length) - rawX(line, line.start);
            line.offset = text.alignment == TextAlignment::center ? -width / 2 : -width;
        }
        int end = line.start + line.length - paragraph.start;
        while (end > line.start - paragraph.start && isSpace(content[end - 1]))
            --end;
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
    const bool justified = m_text.alignment == TextAlignment::justifyAll || (m_text.alignment == TextAlignment::justify && !line.lastInParagraph);
    if (!m_text.area || !justified || line.length < 2)
        return 0;
    const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
    const QString &content = paragraph.layout.text();
    const int from = line.start - paragraph.start;
    int to = from + line.length;
    while (to > from && isSpace(content[to - 1]))
        --to;
    if (to - from < 2)
        return 0;
    const double available = m_text.area->width() - m_text.leftIndent - m_text.rightIndent - (line.index == 0 ? m_text.firstLineIndent : 0);
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

QPainterPath TextLayout::outline() const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const double vertical = m_scale * std::max(1.0, m_text.verticalScale) / 100;
    for (const Line &line : m_lines) {
        if (line.hidden || line.length == 0)
            continue;
        const QTextLine &qline = m_paragraphs[size_t(line.paragraph)]->lines[size_t(line.index)];
        const double baseline = line.baseline - m_text.baselineShift;
        for (const QGlyphRun &run : qline.glyphRuns()) {
            const QRawFont font = run.rawFont();
            const QList<quint32> glyphs = run.glyphIndexes();
            const QList<QPointF> positions = run.positions();
            for (qsizetype glyph = 0; glyph < glyphs.size(); ++glyph) {
                const QPointF at = positions[glyph];
                const double x = at.x() * m_horizontal + line.offset;
                const QTransform place(m_horizontal, 0, 0, vertical, x + shift(line, x), baseline + (at.y() - qline.ascent()) * vertical);
                path.addPath(place.map(font.pathForGlyph(glyphs[glyph])));
            }
        }
    }
    if (!m_text.underline && !m_text.strikethrough)
        return path;
    const QFontMetricsF metrics(m_text.font());
    const double thickness = std::max(0.5, metrics.lineWidth() * m_scale);
    QPainterPath rules;
    for (const Line &line : m_lines) {
        if (line.hidden || line.right <= line.left)
            continue;
        const double baseline = line.baseline - m_text.baselineShift;
        if (m_text.underline)
            rules.addRect(QRectF(line.left, baseline + metrics.underlinePos() * m_scale, line.right - line.left, thickness));
        if (m_text.strikethrough)
            rules.addRect(QRectF(line.left, baseline - metrics.strikeOutPos() * m_scale - thickness / 2, line.right - line.left, thickness));
    }
    // United, so a rule across a descender leaves no hole.
    QPainterPath united = path.united(rules);
    united.setFillRule(Qt::WindingFill);
    return united;
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
