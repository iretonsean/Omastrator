#include "Document/EditorSession.h"
#include "Document/TextLayout.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace {
// Held keys repeat faster than this; slower presses are steps of their own.
constexpr qint64 coalesceWindow = 700;

void keepInRange(TextContent &text)
{
    text.size = std::clamp(text.size, 0.1, 1296.0);
    if (text.leading)
        text.leading = std::clamp(*text.leading, 0.0, 5000.0);
    text.horizontalScale = std::clamp(text.horizontalScale, 1.0, 10000.0);
    text.verticalScale = std::clamp(text.verticalScale, 1.0, 10000.0);
    text.tracking = std::clamp(text.tracking, -1000.0, 10000.0);
    std::erase_if(text.kerns, [](const auto &kern) { return kern.second == 0; });
    if (text.area)
        text.area = QSizeF(std::max(1.0, text.area->width()), std::max(0.0, text.area->height()));
}

// Applies a type edit to each stretch of `range` on its own (all of the text when unset),
// so relative steps keep each run's value and absolute ones reach every run.
void restyle(TextContent &text, const std::function<void(TextContent &)> &change, std::optional<std::pair<int, int>> range)
{
    const TextContent before = text;
    const auto probe = [&](const CharacterFormat &character, const ParagraphFormat &paragraph) {
        TextContent seen = before;
        seen.runs.clear();
        seen.paragraphFormats.clear();
        seen.character() = character;
        seen.paragraph() = paragraph;
        change(seen);
        keepInRange(seen);
        return seen;
    };
    const TextContent own = probe(before.character(), before.paragraph());
    if (!range) {
        text.character() = own.character();
        text.paragraph() = own.paragraph();
        for (TextRun &run : text.runs)
            run.format = probe(run.format, before.paragraph()).character();
        for (auto &[index, format] : text.paragraphFormats)
            format = probe(before.character(), format).paragraph();
    } else {
        const auto [from, to] = *range;
        text.formatCharacters(from, to, [&](CharacterFormat &format) { format = probe(format, before.paragraph()).character(); });
        text.formatParagraphs(text.paragraphOf(from), text.paragraphOf(std::max(from, to - 1)),
                              [&](ParagraphFormat &format) { format = probe(before.character(), format).paragraph(); });
    }
    // The object's own fields: kerning, kerns, scale and the area box.
    if (own.kerning != before.kerning)
        text.kerning = own.kerning;
    if (own.kerns != before.kerns)
        text.kerns = own.kerns;
    if (own.horizontalScale != before.horizontalScale)
        text.horizontalScale = own.horizontalScale;
    if (own.verticalScale != before.verticalScale)
        text.verticalScale = own.verticalScale;
    if (own.area != before.area)
        text.area = own.area;
    text.normalize();
}
}

std::vector<QUuid> EditorSession::selectedTexts() const
{
    std::vector<QUuid> texts;
    if (!m_document)
        return texts;
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *object = m_document->find(id);
        if (object && object->kind == ObjectKind::text)
            texts.push_back(id);
    }
    return texts;
}

TextContent EditorSession::shownText() const
{
    return shownTexts().front();
}

std::optional<std::pair<int, int>> EditorSession::rangeIn(const QUuid &id) const
{
    if (!m_textRange || m_textRange->id != id || !m_document)
        return std::nullopt;
    const VectorObject *object = m_document->find(id);
    if (!object)
        return std::nullopt;
    const int length = int(object->text.text.size());
    const int from = std::clamp(std::min(m_textRange->from, m_textRange->to), 0, length);
    const int to = std::clamp(std::max(m_textRange->from, m_textRange->to), 0, length);
    if (from == to)
        return std::nullopt;
    return std::pair{from, to};
}

void EditorSession::setTextRange(std::optional<TextRange> range)
{
    const bool same = range.has_value() == m_textRange.has_value()
        && (!range || (range->id == m_textRange->id && range->from == m_textRange->from && range->to == m_textRange->to));
    if (same)
        return;
    m_textRange = range;
    notify(false);
}

std::vector<TextContent> EditorSession::shownTexts() const
{
    std::vector<TextContent> shown;
    for (const QUuid &id : selectedTexts()) {
        const TextContent &text = m_document->find(id)->text;
        const auto range = rangeIn(id);
        for (TextContent &facet : text.facets(range ? range->first : 0, range ? range->second : int(text.text.size())))
            shown.push_back(std::move(facet));
    }
    if (shown.empty())
        shown.push_back(defaultText);
    return shown;
}

bool EditorSession::fillTextRange(const Paint &fill)
{
    if (!m_textRange || fill.kind != PaintKind::solid)
        return false;
    const QUuid id = m_textRange->id;
    const auto range = rangeIn(id);
    if (!range || m_document->isEffectivelyLocked(id))
        return false;
    edit(QStringLiteral("Fill"), [&](VectorDocument &document) {
        document.find(id)->text.formatCharacters(range->first, range->second, [&fill](CharacterFormat &format) { format.fill = fill.color; });
    });
    return true;
}

void EditorSession::commitTextEdit(VectorDocument next, const QString &name, bool coalesce)
{
    // The amend below skips edit(), so it needs its own gate.
    if (refuseWhenLocked())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (coalesce && now - m_lastTextStep < coalesceWindow) {
        VectorDocument before = std::move(*m_document);
        m_document = std::move(next);
        settle();
        if (m_history.amend(name, m_document, m_selection)) {
            m_lastTextStep = now;
            notify();
            return;
        }
        next = std::move(*m_document);
        m_document = std::move(before);
    }
    edit(name, [&](VectorDocument &document) { document = std::move(next); });
    m_lastTextStep = coalesce ? now : 0;
}

void EditorSession::updateText(const std::function<void(TextContent &)> &change, const QString &name, bool coalesce)
{
    change(defaultText);
    keepInRange(defaultText);
    if (m_interaction)
        commitInteraction();
    const std::vector<QUuid> texts = selectedTexts();
    if (texts.empty()) {
        notify(false);
        return;
    }
    VectorDocument next = *m_document;
    bool changed = false;
    for (const QUuid &id : texts) {
        if (next.isEffectivelyLocked(id))
            continue;
        TextContent &text = next.find(id)->text;
        const TextContent before = text;
        restyle(text, change, rangeIn(id));
        changed = changed || !(before == text);
    }
    if (!changed) {
        notify(false);
        return;
    }
    commitTextEdit(std::move(next), name, coalesce);
}

void EditorSession::stepText(TextStep step, double amount)
{
    static const char *names[] = {"Tracking", "Leading", "Baseline Shift", "Font Size"};
    updateText([step, amount](TextContent &text) {
        switch (step) {
        case TextStep::tracking:
            text.tracking = std::round((text.tracking + amount) * 1000) / 1000;
            break;
        case TextStep::leading:
            text.leading = std::max(0.0, text.effectiveLeading() + amount);
            break;
        case TextStep::baselineShift:
            text.baselineShift += amount;
            break;
        case TextStep::size:
            text.size = std::max(0.1, text.size + amount);
            break;
        }
    }, QString::fromLatin1(names[int(step)]), true);
}

void EditorSession::kernText(const QUuid &id, int index, double amount)
{
    if (!m_document || !m_document->find(id) || m_document->find(id)->kind != ObjectKind::text || m_document->isEffectivelyLocked(id))
        return;
    if (m_interaction)
        commitInteraction();
    VectorDocument next = *m_document;
    TextContent &text = next.find(id)->text;
    if (index <= 0 || index > text.text.size())
        return;
    text.kerns[index] += amount;
    keepInRange(text);
    commitTextEdit(std::move(next), QStringLiteral("Kerning"), true);
}

void EditorSession::convertTextType(bool toArea)
{
    const std::vector<QUuid> texts = selectedTexts();
    if (texts.empty())
        return;
    VectorDocument next = *m_document;
    bool changed = false;
    for (const QUuid &id : texts) {
        VectorObject &object = *next.find(id);
        if (next.isEffectivelyLocked(id) || object.text.area.has_value() == toArea)
            continue;
        TextContent &text = object.text;
        const TextLayout layout(text);
        const double ascent = layout.ascent();
        if (toArea) {
            // Wide enough that no line wraps; placed so no glyph moves.
            double width = 1;
            for (const TextLayout::Line &line : layout.lines())
                width = std::max(width, layout.xAt(line.start + line.length, int(&line - layout.lines().data())) - layout.xAt(line.start, int(&line - layout.lines().data())));
            width = std::ceil(width + text.leftIndent + text.rightIndent + std::max(0.0, text.firstLineIndent) + 1);
            const double x = text.alignment == TextAlignment::center ? -width / 2 : text.alignment == TextAlignment::right ? -width : 0;
            text.area = QSizeF(width, 0);
            object.transform = QTransform::fromTranslate(x, -ascent) * object.transform;
        } else {
            // Soft wraps become line breaks, from the end so earlier indices hold.
            const auto &lines = layout.lines();
            for (auto line = lines.rbegin(); line != lines.rend(); ++line) {
                if (line->lastInParagraph)
                    continue;
                const int at = line->start + line->length;
                text.replace(at, at, QStringLiteral("\n"));
            }
            const double width = text.area->width();
            const double x = text.alignment == TextAlignment::center ? width / 2 : text.alignment == TextAlignment::right ? width : 0;
            text.area.reset();
            object.transform = QTransform::fromTranslate(x, ascent) * object.transform;
        }
        changed = true;
    }
    if (changed)
        edit(toArea ? QStringLiteral("Convert to Area Type") : QStringLiteral("Convert to Point Type"),
             [&](VectorDocument &document) { document = std::move(next); });
}

void EditorSession::setTextArea(const QUuid &id, std::optional<QSizeF> area)
{
    if (!m_document || !m_document->find(id) || m_document->find(id)->kind != ObjectKind::text)
        return;
    VectorObject object = *m_document->find(id);
    object.text.area = area;
    keepInRange(object.text);
    updateObject(object, QStringLiteral("Area Type"));
}
