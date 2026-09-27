#include "Document/VectorDocument.h"
#include <algorithm>

namespace {
// Each character's format: runs are few and texts short, so edits work on this and compress back.
std::vector<CharacterFormat> perCharacter(const TextContent &text)
{
    std::vector<CharacterFormat> formats(size_t(text.text.size()), text.character());
    for (const TextRun &run : text.runs) {
        const int end = std::min(run.start + run.length, int(formats.size()));
        for (int index = std::max(0, run.start); index < end; ++index)
            formats[size_t(index)] = run.format;
    }
    return formats;
}

void setRuns(TextContent &text, const std::vector<CharacterFormat> &formats)
{
    text.runs.clear();
    for (size_t index = 0; index < formats.size(); ++index) {
        if (formats[index] == text.character())
            continue;
        TextRun *last = text.runs.empty() ? nullptr : &text.runs.back();
        if (last && last->start + last->length == int(index) && last->format == formats[index])
            ++last->length;
        else
            text.runs.push_back({int(index), 1, formats[index]});
    }
}
}

CharacterFormat TextContent::formatAt(int index) const
{
    if (runs.empty() || text.isEmpty())
        return character();
    index = std::clamp(index, 0, int(text.size()) - 1);
    for (const TextRun &run : runs) {
        if (index < run.start)
            break;
        if (index < run.start + run.length)
            return run.format;
    }
    return character();
}

int TextContent::paragraphCount() const
{
    return int(text.count(QLatin1Char('\n'))) + 1;
}

int TextContent::paragraphOf(int position) const
{
    return int(QStringView(text).left(std::clamp(position, 0, int(text.size()))).count(QLatin1Char('\n')));
}

int TextContent::paragraphStart(int paragraph) const
{
    int start = 0;
    for (int index = 0; index < paragraph; ++index) {
        const qsizetype next = text.indexOf(QLatin1Char('\n'), start);
        if (next < 0)
            return int(text.size());
        start = int(next) + 1;
    }
    return start;
}

ParagraphFormat TextContent::paragraphAt(int paragraph) const
{
    const auto found = paragraphFormats.find(paragraph);
    return found == paragraphFormats.end() ? this->paragraph() : found->second;
}

void TextContent::replace(int from, int to, const QString &with)
{
    from = std::clamp(from, 0, int(text.size()));
    to = std::clamp(to, from, int(text.size()));
    const CharacterFormat typed = to > from ? formatAt(from) : from > 0 ? formatAt(from - 1) : formatAt(0);
    const int first = paragraphOf(from);
    const int removed = int(QStringView(text).mid(from, to - from).count(QLatin1Char('\n')));
    const int added = int(with.count(QLatin1Char('\n')));
    // A merged paragraph keeps the first one's format; split ones take the format they split from.
    std::map<int, ParagraphFormat> moved;
    for (const auto &[index, format] : paragraphFormats) {
        if (index <= first)
            moved[index] = format;
        else if (index > first + removed)
            moved[index - removed + added] = format;
    }
    if (const auto split = paragraphFormats.find(first); split != paragraphFormats.end()) {
        for (int index = 1; index <= added; ++index)
            moved[first + index] = split->second;
    }
    paragraphFormats = std::move(moved);
    std::vector<CharacterFormat> formats = perCharacter(*this);
    formats.erase(formats.begin() + from, formats.begin() + to);
    formats.insert(formats.begin() + from, size_t(with.size()), typed);
    replaceKerns(from, to, int(with.size()));
    text.replace(from, to - from, with);
    setRuns(*this, formats);
    normalize();
}

void TextContent::formatCharacters(int from, int to, const std::function<void(CharacterFormat &)> &change)
{
    from = std::clamp(from, 0, int(text.size()));
    to = std::clamp(to, from, int(text.size()));
    std::vector<CharacterFormat> formats = perCharacter(*this);
    // Once per stretch of one format, not once per character.
    for (int start = from; start < to;) {
        int end = start + 1;
        while (end < to && formats[size_t(end)] == formats[size_t(start)])
            ++end;
        CharacterFormat changed = formats[size_t(start)];
        change(changed);
        std::fill(formats.begin() + start, formats.begin() + end, changed);
        start = end;
    }
    setRuns(*this, formats);
}

void TextContent::formatParagraphs(int first, int last, const std::function<void(ParagraphFormat &)> &change)
{
    first = std::max(0, first);
    last = std::min(last, paragraphCount() - 1);
    for (int index = first; index <= last; ++index) {
        ParagraphFormat format = paragraphAt(index);
        change(format);
        paragraphFormats[index] = format;
    }
    normalize();
}

void TextContent::normalize()
{
    if (!runs.empty())
        setRuns(*this, perCharacter(*this));
    const int count = paragraphCount();
    std::erase_if(paragraphFormats, [&](const auto &entry) { return entry.first < 0 || entry.first >= count || entry.second == paragraph(); });
}

std::vector<TextContent> TextContent::facets(int from, int to) const
{
    TextContent plain = *this;
    plain.runs.clear();
    plain.paragraphFormats.clear();
    std::vector<TextContent> seen;
    const auto add = [&](const CharacterFormat &format, int paragraph) {
        TextContent facet = plain;
        facet.character() = format;
        facet.paragraph() = paragraphAt(paragraph);
        if (std::find(seen.begin(), seen.end(), facet) == seen.end())
            seen.push_back(std::move(facet));
    };
    from = std::clamp(from, 0, int(text.size()));
    to = std::clamp(to, from, int(text.size()));
    if (from == to) {
        add(formatAt(from), paragraphOf(from));
        return seen;
    }
    int paragraph = paragraphOf(from);
    for (int index = from; index < to; ++index) {
        add(formatAt(index), paragraph);
        if (text[index] == QLatin1Char('\n'))
            ++paragraph;
    }
    return seen;
}

QStringList TextContent::families() const
{
    QStringList used{family};
    for (const TextRun &run : runs) {
        if (!used.contains(run.format.family))
            used << run.format.family;
    }
    return used;
}
