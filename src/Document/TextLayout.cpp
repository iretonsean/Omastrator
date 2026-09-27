#include "Document/TextLayout.h"
#include "Document/Hyphenator.h"
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
    // Hyphenation (P2-5): the display string Qt actually wraps may have soft hyphens
    // this paragraph's own text doesn't; these convert between the two, paragraph-local.
    std::vector<int> toDisplay;
    std::vector<int> fromDisplay;
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

// Builds the string Qt actually wraps: identical to `text` unless `format.hyphenate`
// asks for soft hyphens at the patterns' break points (Hyphenator). `toDisplay` maps
// a paragraph-local original index to its index in the returned string, one entry
// longer than `text`; `fromDisplay` reads the other way, one entry longer than it.
QString buildDisplay(const QString &text, const ParagraphFormat &format, std::vector<int> &toDisplay, std::vector<int> &fromDisplay)
{
    toDisplay.assign(size_t(text.size()) + 1, 0);
    QString display;
    if (!format.hyphenate) {
        for (int i = 0; i <= text.size(); ++i)
            toDisplay[size_t(i)] = i;
        display = text;
    } else {
        int i = 0;
        while (i < text.size()) {
            if (text.at(i).isLetter()) {
                const int wordStart = i;
                while (i < text.size() && text.at(i).isLetter())
                    ++i;
                const QString word = text.mid(wordStart, i - wordStart);
                const std::vector<int> breaks = word.size() >= format.hyphenMinWord
                    ? Hyphenator::breakPoints(word, format.hyphenMinBefore, format.hyphenMinAfter)
                    : std::vector<int>{};
                for (int k = 0; k < word.size(); ++k) {
                    toDisplay[size_t(wordStart + k)] = display.size();
                    display += word.at(k);
                    if (std::find(breaks.begin(), breaks.end(), k + 1) != breaks.end())
                        display += QChar(0x00AD);
                }
            } else {
                toDisplay[size_t(i)] = display.size();
                display += text.at(i);
                ++i;
            }
        }
        toDisplay[size_t(text.size())] = display.size();
    }
    fromDisplay.assign(size_t(display.size()) + 1, 0);
    for (int i = 0; i <= text.size(); ++i)
        fromDisplay[size_t(toDisplay[size_t(i)])] = i;
    return display;
}

// The widest of `limit`'s free sub-intervals once `exclusions` meeting [top, bottom]
// are cut out; nullopt when nothing at least `minWidth` wide is left.
std::optional<std::pair<double, double>> freeInterval(const std::vector<QRectF> &exclusions, double loLimit, double hiLimit, double top,
                                                       double bottom, double minWidth)
{
    std::vector<std::pair<double, double>> blocked;
    for (const QRectF &exclusion : exclusions) {
        if (exclusion.bottom() <= top || exclusion.top() >= bottom)
            continue;
        const double lo = std::max(loLimit, exclusion.left()), hi = std::min(hiLimit, exclusion.right());
        if (hi > lo)
            blocked.emplace_back(lo, hi);
    }
    std::sort(blocked.begin(), blocked.end());
    std::vector<std::pair<double, double>> free;
    double cursor = loLimit;
    for (const auto &[lo, hi] : blocked) {
        if (lo > cursor)
            free.emplace_back(cursor, lo);
        cursor = std::max(cursor, hi);
    }
    if (cursor < hiLimit)
        free.emplace_back(cursor, hiLimit);
    std::optional<std::pair<double, double>> best;
    for (const auto &candidate : free) {
        if (candidate.second - candidate.first < minWidth)
            continue;
        if (!best || candidate.second - candidate.first > best->second - best->first)
            best = candidate;
    }
    return best;
}
}

TextLayout::TextLayout(const TextContent &text)
    : m_text(text.flow.story ? *text.flow.story : text), m_flow(text.flow), m_pathMode(text.onPath.has_value())
{
    if (m_pathMode) {
        // Flipping reverses the path and reads the other way, so the visible start stays put.
        const VectorPath &directed = text.onPath->flipped ? reversed(text.onPath->path) : text.onPath->path;
        m_pathGeometry = directed.painterPath();
        m_pathLength = m_pathGeometry.length();
        m_pathClosed = !text.onPath->path.contours.empty() && text.onPath->path.contours.front().closed;
        const double effectiveStart = text.onPath->flipped ? 1 - text.onPath->start : text.onPath->start;
        m_pathStartLength = effectiveStart * m_pathLength;
    }
    m_formats.push_back(m_text.character());
    m_formatOf.assign(size_t(m_text.text.size()), 0);
    for (const TextRun &run : m_text.runs) {
        auto found = std::find(m_formats.begin(), m_formats.end(), run.format);
        if (found == m_formats.end())
            found = m_formats.insert(m_formats.end(), run.format);
        const int index = int(found - m_formats.begin());
        for (int at = std::max(0, run.start); at < std::min(run.start + run.length, int(m_formatOf.size())); ++at)
            m_formatOf[size_t(at)] = index;
    }
    // Laid out in whole pixels; sizes that miss the object's pixel grid get a finer one.
    m_scale = m_text.size / std::max(1.0, double(std::lround(m_text.size)));
    for (const CharacterFormat &format : m_formats) {
        const double pixels = format.size / m_scale;
        if (std::abs(pixels - std::round(pixels)) > 1e-6) {
            m_scale = m_text.size / std::max(1.0, double(std::lround(m_text.size * 16)));
            break;
        }
    }
    const double perPoint = 1 / m_scale;
    for (const CharacterFormat &format : m_formats) {
        const QFontMetricsF metrics(format.font(perPoint, m_text.kerning));
        m_formatAscents.push_back(metrics.ascent() * m_scale);
    }
    const QFont font = m_text.character().font(perPoint, m_text.kerning);
    m_horizontal = m_scale * std::max(1.0, m_text.horizontalScale) / 100;
    const QFontMetricsF metrics(font);
    m_ascent = metrics.ascent() * m_scale;
    m_descent = metrics.descent() * m_scale;
    const bool area = m_text.area.has_value();
    const double areaWidth = area ? std::max(1.0, m_text.area->width()) : 0;
    const bool frameMode = !m_flow.frames.empty();
    int curFrame = 0;
    double frameY = 0;
    bool frameFirstRow = true;
    const QStringList paragraphs = m_text.text.split(QLatin1Char('\n'));
    double baseline = 0;
    int start = 0;
    for (int index = 0; index < paragraphs.size(); ++index) {
        auto paragraph = std::make_unique<Paragraph>();
        paragraph->start = start;
        paragraph->format = m_text.paragraphAt(index);
        const ParagraphFormat &format = paragraph->format;
        const QString display = buildDisplay(paragraphs[index], format, paragraph->toDisplay, paragraph->fromDisplay);
        // Paragraph-level estimates for which frame and band a row would fall in,
        // ahead of knowing exactly what text that row will hold.
        const double paragraphAscent = ascentOf(start, int(paragraphs[index].size()));
        const double paragraphLeading = leadingOf(format, start, int(paragraphs[index].size()));
        QTextLayout &layout = paragraph->layout;
        layout.setText(display);
        layout.setFont(font);
        QTextOption option;
        option.setUseDesignMetrics(true);
        option.setWrapMode(area ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        option.setAlignment(area ? qtAlignment(format.alignment) : Qt::AlignLeft);
        layout.setTextOption(option);
        layout.setFormats(characterFormats(start, int(paragraphs[index].size()), paragraph->toDisplay));
        if (index > 0)
            baseline += m_paragraphs.back()->format.spaceAfter + format.spaceBefore;
        layout.beginLayout();
        for (;;) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            const bool firstLine = paragraph->lines.empty();
            const double indent = format.leftIndent + (firstLine ? format.firstLineIndent : 0);
            double rowAvailable;
            int rowFrame = 0;
            if (frameMode) {
                const double estimate = std::max(1.0, frameFirstRow ? paragraphAscent : paragraphLeading);
                std::optional<std::pair<double, double>> interval;
                int guard = 0;
                for (;;) {
                    const TextFrame &box = m_flow.frames[size_t(curFrame)];
                    // Only consider this band if the whole row would actually fit under it;
                    // otherwise it belongs to a lower band, or the next frame.
                    const bool fitsVertically = frameY + estimate <= box.size.height() + 0.01;
                    if (fitsVertically) {
                        interval = freeInterval(box.exclusions, indent, box.size.width() - format.rightIndent, frameY,
                                                 frameY + estimate, m_text.size * 2);
                        if (interval)
                            break;
                    }
                    if (fitsVertically && ++guard < 500) {
                        frameY += estimate;
                        continue;
                    }
                    if (curFrame + 1 < int(m_flow.frames.size())) {
                        ++curFrame;
                        frameY = 0;
                        frameFirstRow = true;
                        guard = 0;
                        continue;
                    }
                    // Every frame is full: keep going past the last one so the
                    // text still shows; the hidden check below marks the overflow.
                    interval.emplace(indent, box.size.width() - format.rightIndent);
                    break;
                }
                line.setLineWidth(std::max(1.0, (interval->second - interval->first) / m_horizontal));
                line.setPosition(QPointF(interval->first / m_horizontal, 0));
                rowAvailable = interval->second - interval->first;
                rowFrame = curFrame;
            } else if (area) {
                line.setLineWidth(std::max(1.0, (areaWidth - indent - format.rightIndent) / m_horizontal));
                line.setPosition(QPointF(indent / m_horizontal, 0));
                rowAvailable = areaWidth - indent - format.rightIndent;
            } else {
                line.setLineWidth(1e7);
                line.setPosition(QPointF(indent / m_horizontal, 0));
                rowAvailable = 0;
            }
            Line placed;
            const int displayEnd = line.textStart() + line.textLength();
            placed.start = start + paragraph->fromDisplay[size_t(std::clamp(line.textStart(), 0, int(paragraph->fromDisplay.size()) - 1))];
            const int originalEnd = start + paragraph->fromDisplay[size_t(std::clamp(displayEnd, 0, int(paragraph->fromDisplay.size()) - 1))];
            placed.length = originalEnd - placed.start;
            placed.hyphenated = displayEnd > 0 && displayEnd <= display.size() && display.at(displayEnd - 1) == QChar(0x00AD);
            placed.frame = rowFrame;
            placed.available = rowAvailable;
            // Area type's first baseline sits its line's ascent below the top; later ones a leading apart.
            if (frameMode)
                baseline = frameFirstRow ? frameY + ascentOf(placed.start, placed.length) : baseline + leadingOf(format, placed.start, placed.length);
            else if (!m_lines.empty())
                baseline += leadingOf(format, placed.start, placed.length);
            else if (area)
                baseline += ascentOf(placed.start, placed.length);
            placed.paragraph = index;
            placed.index = int(paragraph->lines.size());
            placed.baseline = baseline;
            placed.ascent = line.ascent() * m_scale;
            placed.descent = line.descent() * m_scale;
            placed.lastInParagraph = false;
            if (frameMode) {
                frameY = baseline + placed.descent;
                frameFirstRow = false;
            }
            m_lines.push_back(placed);
            paragraph->lines.push_back(line);
        }
        layout.endLayout();
        if (!m_lines.empty())
            m_lines.back().lastInParagraph = true;
        start += int(paragraphs[index].size()) + 1;
        m_paragraphs.push_back(std::move(paragraph));
    }
    double pathCursor = 0;
    for (Line &line : m_lines) {
        const Paragraph &paragraph = *m_paragraphs[size_t(line.paragraph)];
        const TextAlignment alignment = paragraph.format.alignment;
        const QString &content = paragraph.layout.text();
        int end = line.start + line.length - paragraph.start;
        while (end > line.start - paragraph.start && isSpace(content.at(paragraph.toDisplay[size_t(end - 1)])))
            --end;
        const double width = rawX(line, end + paragraph.start) - rawX(line, line.start);
        // Point type hangs from the origin as its alignment says.
        if (!area && (alignment == TextAlignment::center || alignment == TextAlignment::right))
            line.offset = alignment == TextAlignment::center ? -rawWidth(line) / 2 : -rawWidth(line);
        // A justified paragraph's last line can sit centred or flush right.
        if (area && line.lastInParagraph && (alignment == TextAlignment::justifyCenter || alignment == TextAlignment::justifyRight)) {
            const double available = line.available - (line.index == 0 ? paragraph.format.firstLineIndent : 0);
            line.offset = std::max(0.0, available - width) * (alignment == TextAlignment::justifyCenter ? 0.5 : 1);
        }
        line.left = rawX(line, line.start);
        line.right = std::max(line.left, rawX(line, end + paragraph.start));
        line.right += shift(line, line.right);
        if (m_pathMode) {
            line.pathOffset = pathCursor;
            pathCursor += rawWidth(line);
            const double maxLength = m_pathClosed ? 2 * m_pathLength : m_pathLength;
            if (m_pathStartLength + line.pathOffset > maxLength + 0.01) {
                line.hidden = true;
                m_overflows = true;
            } else if (m_pathStartLength + line.pathOffset + rawWidth(line) > maxLength + 0.01) {
                m_overflows = true;
            }
        } else if (!m_flow.frames.empty()) {
            if (line.baseline + line.descent > m_flow.frames[size_t(line.frame)].size.height() + 0.01) {
                line.hidden = true;
                m_overflows = true;
            }
        } else if (area && m_text.area->height() > 0 && line.baseline + line.descent > m_text.area->height() + 0.01) {
            line.hidden = true;
            m_overflows = true;
        }
    }
}

TextLayout::~TextLayout() = default;

// Runs and manual kerns as QTextLayout formats, for one paragraph's characters;
// `toDisplay` moves the boundaries from original to display-string offsets.
QList<QTextLayout::FormatRange> TextLayout::characterFormats(int start, int length, const std::vector<int> &toDisplay) const
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
            ranges.append({toDisplay[size_t(local)], toDisplay[size_t(end)] - toDisplay[size_t(local)], made});
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
    const int local = std::clamp(position - paragraph.start, 0, int(paragraph.toDisplay.size()) - 1);
    return paragraph.lines[size_t(line.index)].cursorToX(paragraph.toDisplay[size_t(local)]) * m_horizontal + line.offset;
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
    const int from = paragraph.toDisplay[size_t(line.start - paragraph.start)];
    int to = line.start - paragraph.start + line.length;
    to = paragraph.toDisplay[size_t(std::clamp(to, 0, int(paragraph.toDisplay.size()) - 1))];
    while (to > from && isSpace(content.at(to - 1)))
        --to;
    if (to - from < 2)
        return 0;
    const double available = line.available - (line.index == 0 ? format.firstLineIndent : 0);
    const double extra = available - (paragraph.lines[size_t(line.index)].cursorToX(to) * m_horizontal + line.offset
                                       - (paragraph.lines[size_t(line.index)].cursorToX(from) * m_horizontal + line.offset));
    if (extra <= 0)
        return 0;
    std::vector<int> stops;
    for (int index = from; index < to; ++index) {
        if (isSpace(content.at(index)))
            stops.push_back(index + 1);
    }
    if (stops.empty()) {
        // One long word only spreads when justify-all asks for its last line.
        if (!line.lastInParagraph)
            return 0;
        for (int index = from + 1; index < to; ++index)
            stops.push_back(index);
    }
    const double xAtDisplay = (x - line.offset) / m_horizontal;
    int passed = 0;
    for (const int stop : stops) {
        if (paragraph.lines[size_t(line.index)].cursorToX(stop) <= xAtDisplay + 1e-6 / m_horizontal)
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

QPointF TextLayout::warpToFrame(QPointF local, int fromFrame, int toFrame) const
{
    if (fromFrame == toFrame || m_flow.frames.empty())
        return local;
    const QPointF document = m_flow.frames[size_t(std::clamp(fromFrame, 0, int(m_flow.frames.size()) - 1))].transform.map(local);
    return m_flow.frames[size_t(std::clamp(toFrame, 0, int(m_flow.frames.size()) - 1))].transform.inverted().map(document);
}

QPointF TextLayout::pathPointAndTangent(double length, QPointF *tangent) const
{
    if (m_pathLength <= 1e-6) {
        if (tangent)
            *tangent = QPointF(1, 0);
        return m_pathGeometry.pointAtPercent(0);
    }
    // A closed path wraps once, so a length past the end reads from its start again;
    // an open one just clamps, since there is nothing past either end to read.
    double wrapped = length;
    if (m_pathClosed) {
        wrapped = std::fmod(length, m_pathLength);
        if (wrapped < 0)
            wrapped += m_pathLength;
    } else {
        wrapped = std::clamp(length, 0.0, m_pathLength);
    }
    const double t = std::clamp(wrapped / m_pathLength, 0.0, 1.0);
    const QPointF point = m_pathGeometry.pointAtPercent(t);
    if (tangent) {
        const double eps = 0.001;
        const double t0 = std::clamp(t - eps, 0.0, 1.0), t1 = std::clamp(t + eps, 0.0, 1.0);
        QPointF direction = m_pathGeometry.pointAtPercent(t1) - m_pathGeometry.pointAtPercent(t0);
        const double length2 = std::hypot(direction.x(), direction.y());
        *tangent = length2 > 1e-9 ? direction / length2 : QPointF(1, 0);
    }
    return point;
}

QPointF TextLayout::warp(QPointF local, int line) const
{
    if (line < 0 || line >= int(m_lines.size()))
        return local;
    if (m_pathMode) {
        const double length = m_pathStartLength + m_lines[size_t(line)].pathOffset + local.x();
        QPointF tangent;
        const QPointF point = pathPointAndTangent(length, &tangent);
        return QPointF(point.x() - tangent.y() * local.y(), point.y() + tangent.x() * local.y());
    }
    return warpToFrame(local, m_flow.frame, m_lines[size_t(line)].frame);
}

int TextLayout::positionAt(QPointF local) const
{
    if (m_lines.empty())
        return 0;
    if (m_pathMode) {
        // The nearest caret point along the path, compared in path (warped) space.
        int best = 0;
        double distance = std::numeric_limits<double>::infinity();
        for (size_t index = 0; index < m_lines.size(); ++index) {
            const Line &line = m_lines[index];
            if (line.hidden)
                continue;
            const int last = line.lastInParagraph ? line.start + line.length : std::max(line.start, line.start + line.length - 1);
            for (int position = line.start; position <= last; ++position) {
                const QPointF warped = warp(QPointF(xAt(position, int(index)), 0), int(index));
                const double gap = std::hypot(warped.x() - local.x(), warped.y() - local.y());
                if (gap < distance) {
                    distance = gap;
                    best = position;
                }
            }
        }
        return best;
    }
    int chosenFrame = m_flow.frame;
    QPointF search = local;
    if (!m_flow.frames.empty()) {
        for (size_t candidate = 0; candidate < m_flow.frames.size(); ++candidate) {
            const QPointF mapped = warpToFrame(local, m_flow.frame, int(candidate));
            const QRectF box(QPointF(0, 0), m_flow.frames[candidate].size);
            if (box.adjusted(-1, -1, 1, 1).contains(mapped)) {
                chosenFrame = int(candidate);
                search = mapped;
                break;
            }
        }
    }
    size_t chosen = 0;
    bool found = false;
    for (size_t index = 0; index < m_lines.size(); ++index) {
        if (m_lines[index].hidden || (!m_flow.frames.empty() && m_lines[index].frame != chosenFrame))
            continue;
        chosen = index;
        found = true;
        if (search.y() <= m_lines[index].baseline + m_lines[index].descent)
            break;
    }
    if (!found)
        return int(m_text.text.size());
    const Line &line = m_lines[chosen];
    // A wrapped line's end is the next line's start.
    const int last = line.lastInParagraph ? line.start + line.length : std::max(line.start, line.start + line.length - 1);
    int best = line.start;
    double distance = std::numeric_limits<double>::infinity();
    for (int position = line.start; position <= last; ++position) {
        const double x = rawX(line, position);
        const double gap = std::abs(x + shift(line, x) - search.x());
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
        if (!m_pathMode && !m_flow.frames.empty() && line.frame != m_flow.frame)
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
            const int displayFrom = paragraph.toDisplay[size_t(from - paragraph.start)];
            const int displayLength = paragraph.toDisplay[size_t(to - paragraph.start)] - displayFrom;
            for (const QGlyphRun &run : qline.glyphRuns(displayFrom, displayLength)) {
                const QRawFont rawFont = run.rawFont();
                const QList<quint32> glyphs = run.glyphIndexes();
                const QList<QPointF> positions = run.positions();
                for (qsizetype glyph = 0; glyph < glyphs.size(); ++glyph) {
                    const QPointF at = positions[glyph];
                    const double x = at.x() * m_horizontal + line.offset;
                    if (m_pathMode) {
                        // advance and the raw glyph share one coordinate system (pre-scale);
                        // mid and dy are in output units, so the glyph is scaled before either applies.
                        const double advance = rawFont.advancesForGlyphIndexes(QList<quint32>{glyphs[glyph]}).value(0).x();
                        const double mid = x + advance * m_horizontal / 2;
                        const double dy = (at.y() - qline.ascent()) * vertical - character.baselineShift;
                        const double length = m_pathStartLength + line.pathOffset + mid;
                        const double maxLength = m_pathClosed ? 2 * m_pathLength : m_pathLength;
                        if (length > maxLength + 0.01)
                            continue;
                        QPointF tangent;
                        const QPointF point = pathPointAndTangent(length, &tangent);
                        const QTransform place(tangent.x(), tangent.y(), -tangent.y(), tangent.x(), point.x(), point.y());
                        QPainterPath glyphShape = QTransform::fromScale(m_horizontal, vertical).map(rawFont.pathForGlyph(glyphs[glyph]));
                        glyphShape = QTransform::fromTranslate(-advance * m_horizontal / 2, -dy).map(glyphShape);
                        pieces[into].second.addPath(place.map(glyphShape));
                        continue;
                    }
                    const QTransform place(m_horizontal, 0, 0, vertical, x + shift(line, x), baseline + (at.y() - qline.ascent()) * vertical);
                    pieces[into].second.addPath(place.map(rawFont.pathForGlyph(glyphs[glyph])));
                }
            }
            if (!m_pathMode && (character.underline || character.strikethrough)) {
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
        if (!m_pathMode && !m_flow.frames.empty() && line.frame != m_flow.frame)
            continue;
        const QTextLine &qline = m_paragraphs[size_t(line.paragraph)]->lines[size_t(line.index)];
        for (const QGlyphRun &run : qline.glyphRuns())
            count += int(run.glyphIndexes().size());
    }
    return count;
}

QRectF TextLayout::frame() const
{
    if (!m_flow.frames.empty()) {
        QSizeF size = m_flow.frames[size_t(std::clamp(m_flow.frame, 0, int(m_flow.frames.size()) - 1))].size;
        // A grow box's frame stands in as a very tall one while wrapping; its shown
        // height still follows its content, same as a plain grow box would.
        if (size.height() >= 1e5)
            size.setHeight(std::max(m_lines.empty() ? 0.0 : m_lines.back().baseline + m_lines.back().descent, m_ascent + m_descent));
        return QRectF(QPointF(0, 0), size);
    }
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
