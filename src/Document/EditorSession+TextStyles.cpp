#include "Document/EditorSession.h"
#include <QFontDatabase>
#include <algorithm>

namespace {
// The fields a style sets: never the colour, never the style ids themselves.
template <typename Visit> void eachCharacterField(Visit visit)
{
    visit(&CharacterFormat::family);
    visit(&CharacterFormat::style);
    visit(&CharacterFormat::size);
    visit(&CharacterFormat::tracking);
    visit(&CharacterFormat::baselineShift);
    visit(&CharacterFormat::textCase);
    visit(&CharacterFormat::underline);
    visit(&CharacterFormat::strikethrough);
    visit(&CharacterFormat::features);
}

template <typename Visit> void eachParagraphField(Visit visit)
{
    visit(&ParagraphFormat::alignment);
    visit(&ParagraphFormat::leading);
    visit(&ParagraphFormat::leftIndent);
    visit(&ParagraphFormat::rightIndent);
    visit(&ParagraphFormat::firstLineIndent);
    visit(&ParagraphFormat::spaceBefore);
    visit(&ParagraphFormat::spaceAfter);
}

template <typename Format, typename Each> bool same(const Format &a, const Format &b, Each each)
{
    bool equal = true;
    each([&](auto member) { equal = equal && a.*member == b.*member; });
    return equal;
}

template <typename Format, typename Each> void take(Format &target, const Format &from, Each each)
{
    each([&](auto member) { target.*member = from.*member; });
}

// A redefined style: fields still as it was follow it; fields changed by hand stay.
template <typename Format, typename Each> void follow(Format &target, const Format &old, const Format &fresh, Each each)
{
    each([&](auto member) {
        if (target.*member == old.*member)
            target.*member = fresh.*member;
    });
}

bool sameCharacter(const CharacterFormat &a, const CharacterFormat &b)
{
    return same(a, b, [](auto visit) { eachCharacterField(visit); });
}

bool sameParagraph(const ParagraphFormat &a, const ParagraphFormat &b)
{
    return same(a, b, [](auto visit) { eachParagraphField(visit); });
}

using Range = std::optional<std::pair<int, int>>;

// The characters an edit reaches: the range, or every format the text holds.
void eachCharacter(TextContent &text, const Range &range, const std::function<void(CharacterFormat &)> &change)
{
    if (range) {
        text.formatCharacters(range->first, range->second, change);
        return;
    }
    change(text.character());
    for (TextRun &run : text.runs)
        change(run.format);
}

void eachParagraph(TextContent &text, const Range &range, const std::function<void(ParagraphFormat &)> &change)
{
    if (range) {
        text.formatParagraphs(text.paragraphOf(range->first), text.paragraphOf(std::max(range->first, range->second - 1)), change);
        return;
    }
    change(text.paragraph());
    for (auto &[index, format] : text.paragraphFormats)
        change(format);
}

// A range grown to whole paragraphs, which a paragraph style's characters cover.
Range paragraphsOf(const TextContent &text, const Range &range)
{
    if (!range)
        return range;
    const int first = text.paragraphOf(range->first), last = text.paragraphOf(std::max(range->first, range->second - 1));
    return std::pair{text.paragraphStart(first), text.paragraphStart(last + 1)};
}

void applyStyle(TextContent &text, const Range &range, const TextStyle &style)
{
    if (style.kind == TextStyleKind::character) {
        eachCharacter(text, range, [&style](CharacterFormat &format) {
            take(format, style.character, [](auto visit) { eachCharacterField(visit); });
            format.characterStyle = style.id;
        });
    } else {
        eachParagraph(text, range, [&style](ParagraphFormat &format) {
            take(format, style.paragraph, [](auto visit) { eachParagraphField(visit); });
            format.paragraphStyle = style.id;
        });
        // Characters with a style of their own keep it.
        eachCharacter(text, paragraphsOf(text, range), [&style](CharacterFormat &format) {
            if (format.characterStyle.isNull())
                take(format, style.character, [](auto visit) { eachCharacterField(visit); });
        });
    }
    text.normalize();
}

void redefine(TextContent &text, const TextStyle &old, const TextStyle &fresh)
{
    const auto followCharacter = [&](CharacterFormat &format) {
        follow(format, old.character, fresh.character, [](auto visit) { eachCharacterField(visit); });
    };
    if (old.kind == TextStyleKind::character) {
        eachCharacter(text, std::nullopt, [&](CharacterFormat &format) {
            if (format.characterStyle == old.id)
                followCharacter(format);
        });
        text.normalize();
        return;
    }
    std::vector<int> styled;
    for (int paragraph = 0; paragraph < text.paragraphCount(); ++paragraph) {
        if (text.paragraphAt(paragraph).paragraphStyle == old.id)
            styled.push_back(paragraph);
    }
    const auto unstyled = [&](CharacterFormat &format) {
        if (format.characterStyle.isNull())
            followCharacter(format);
    };
    if (int(styled.size()) == text.paragraphCount()) {
        eachCharacter(text, std::nullopt, unstyled);
    } else {
        for (const int paragraph : styled)
            text.formatCharacters(text.paragraphStart(paragraph), text.paragraphStart(paragraph + 1), unstyled);
    }
    eachParagraph(text, std::nullopt, [&](ParagraphFormat &format) {
        if (format.paragraphStyle == old.id)
            follow(format, old.paragraph, fresh.paragraph, [](auto visit) { eachParagraphField(visit); });
    });
    text.normalize();
}

// A style's look taken from what's shown, without colour or the ids it came with.
TextStyle styleFrom(const TextContent &shown, TextStyle style)
{
    take(style.character, shown.character(), [](auto visit) { eachCharacterField(visit); });
    if (style.kind == TextStyleKind::paragraph)
        take(style.paragraph, shown.paragraph(), [](auto visit) { eachParagraphField(visit); });
    return style;
}
}

void restyleText(TextContent &text, const TextStyle &old, const TextStyle &fresh)
{
    redefine(text, old, fresh);
}

const TextStyle *EditorSession::textStyle(const QUuid &id) const
{
    if (!m_document || id.isNull())
        return nullptr;
    for (const TextStyle &style : m_document->textStyles) {
        if (style.id == id)
            return &style;
    }
    return nullptr;
}

QUuid EditorSession::newTextStyle(TextStyleKind kind, const QString &name)
{
    if (!m_document)
        return {};
    TextStyle style;
    style.kind = kind;
    style.name = name;
    if (style.name.isEmpty()) {
        const QString base = kind == TextStyleKind::character ? QStringLiteral("Character Style %1") : QStringLiteral("Paragraph Style %1");
        int number = 1;
        const auto taken = [&](const QString &candidate) {
            return std::any_of(m_document->textStyles.begin(), m_document->textStyles.end(), [&](const TextStyle &each) { return each.name == candidate; });
        };
        while (taken(base.arg(number)))
            ++number;
        style.name = base.arg(number);
    }
    style = styleFrom(shownText(), style);
    const std::vector<QUuid> texts = selectedTexts();
    edit(kind == TextStyleKind::character ? QStringLiteral("New Character Style") : QStringLiteral("New Paragraph Style"),
         [&](VectorDocument &document) {
             document.textStyles.push_back(style);
             for (const QUuid &id : texts) {
                 if (!document.isEffectivelyLocked(id))
                     applyStyle(document.find(id)->text, rangeIn(id), style);
             }
         });
    return style.id;
}

void EditorSession::applyTextStyle(const QUuid &id)
{
    const TextStyle *found = textStyle(id);
    const std::vector<QUuid> texts = selectedTexts();
    if (!found || texts.empty())
        return;
    const TextStyle style = *found;
    VectorDocument next = *m_document;
    for (const QUuid &text : texts) {
        if (!next.isEffectivelyLocked(text))
            applyStyle(next.find(text)->text, rangeIn(text), style);
    }
    if (!(next == *m_document))
        commitTextEdit(std::move(next), QStringLiteral("Apply Style"), false);
}

void EditorSession::clearTextStyle(TextStyleKind kind)
{
    const std::vector<QUuid> texts = selectedTexts();
    if (texts.empty())
        return;
    VectorDocument next = *m_document;
    for (const QUuid &id : texts) {
        TextContent &text = next.find(id)->text;
        if (kind == TextStyleKind::character)
            eachCharacter(text, rangeIn(id), [](CharacterFormat &format) { format.characterStyle = QUuid(); });
        else
            eachParagraph(text, rangeIn(id), [](ParagraphFormat &format) { format.paragraphStyle = QUuid(); });
        text.normalize();
    }
    if (!(next == *m_document))
        commitTextEdit(std::move(next), QStringLiteral("Detach Style"), false);
}

void EditorSession::clearTextOverrides(TextStyleKind kind)
{
    const std::vector<QUuid> texts = selectedTexts();
    if (texts.empty())
        return;
    VectorDocument next = *m_document;
    for (const QUuid &id : texts) {
        TextContent &text = next.find(id)->text;
        const Range range = rangeIn(id);
        if (kind == TextStyleKind::character) {
            eachCharacter(text, range, [this](CharacterFormat &format) {
                if (const TextStyle *style = textStyle(format.characterStyle))
                    take(format, style->character, [](auto visit) { eachCharacterField(visit); });
            });
            text.normalize();
            continue;
        }
        // Each paragraph back to its own style, and its unstyled characters with it.
        const int first = range ? text.paragraphOf(range->first) : 0;
        const int last = range ? text.paragraphOf(std::max(range->first, range->second - 1)) : text.paragraphCount() - 1;
        for (int paragraph = first; paragraph <= last; ++paragraph) {
            if (const TextStyle *style = textStyle(text.paragraphAt(paragraph).paragraphStyle))
                applyStyle(text, std::pair{text.paragraphStart(paragraph), text.paragraphStart(paragraph + 1)}, *style);
        }
    }
    if (!(next == *m_document))
        commitTextEdit(std::move(next), QStringLiteral("Clear Overrides"), false);
}

void EditorSession::redefineTextStyle(const QUuid &id)
{
    const TextStyle *found = textStyle(id);
    if (!found)
        return;
    const TextStyle old = *found;
    const TextStyle fresh = styleFrom(shownText(), old);
    if (fresh == old)
        return;
    edit(QStringLiteral("Redefine Style"), [&](VectorDocument &document) {
        for (TextStyle &style : document.textStyles) {
            if (style.id == id)
                style = fresh;
        }
        for (VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::text)
                redefine(object.text, old, fresh);
        }
    });
}

void EditorSession::renameTextStyle(const QUuid &id, const QString &name)
{
    const TextStyle *found = textStyle(id);
    if (!found || name.trimmed().isEmpty() || found->name == name.trimmed())
        return;
    edit(QStringLiteral("Rename Style"), [&](VectorDocument &document) {
        for (TextStyle &style : document.textStyles) {
            if (style.id == id)
                style.name = name.trimmed();
        }
    });
}

void EditorSession::deleteTextStyle(const QUuid &id)
{
    if (!textStyle(id))
        return;
    // What used it keeps its look and loses the name.
    edit(QStringLiteral("Delete Style"), [&](VectorDocument &document) {
        std::erase_if(document.textStyles, [&](const TextStyle &style) { return style.id == id; });
        for (VectorObject &object : document.objects) {
            if (object.kind != ObjectKind::text)
                continue;
            eachCharacter(object.text, std::nullopt, [&](CharacterFormat &format) {
                if (format.characterStyle == id)
                    format.characterStyle = QUuid();
            });
            eachParagraph(object.text, std::nullopt, [&](ParagraphFormat &format) {
                if (format.paragraphStyle == id)
                    format.paragraphStyle = QUuid();
            });
            object.text.normalize();
        }
    });
}

std::optional<QUuid> EditorSession::shownTextStyle(TextStyleKind kind, bool *overridden) const
{
    if (overridden)
        *overridden = false;
    const std::vector<TextContent> shown = shownTexts();
    const auto idOf = [kind](const TextContent &text) { return kind == TextStyleKind::character ? text.characterStyle : text.paragraphStyle; };
    const QUuid id = idOf(shown.front());
    for (const TextContent &text : shown) {
        if (idOf(text) != id)
            return std::nullopt;
    }
    const TextStyle *style = textStyle(id);
    if (!style)
        return std::nullopt;
    if (overridden) {
        for (const TextContent &text : shown) {
            const bool differs = kind == TextStyleKind::character
                ? !sameCharacter(text.character(), style->character)
                : !sameParagraph(text.paragraph(), style->paragraph) || (text.characterStyle.isNull() && !sameCharacter(text.character(), style->character));
            *overridden = *overridden || differs;
        }
    }
    return id;
}

bool EditorSession::isFontInstalled(const QString &family)
{
    // fontconfig's generic names always resolve to something.
    static const QStringList generic{QStringLiteral("Sans Serif"), QStringLiteral("Serif"), QStringLiteral("Monospace"),
                                     QStringLiteral("sans-serif"), QStringLiteral("serif"), QStringLiteral("monospace")};
    return generic.contains(family) || QFontDatabase::families().contains(family, Qt::CaseInsensitive);
}

QStringList EditorSession::usedFonts(bool selectionOnly) const
{
    QStringList used;
    if (!m_document)
        return used;
    const auto add = [&used](const TextContent &text) {
        for (const QString &family : text.families()) {
            if (!used.contains(family))
                used << family;
        }
    };
    if (selectionOnly) {
        for (const QUuid &id : selectedTexts())
            add(m_document->find(id)->text);
        return used;
    }
    for (const VectorObject &object : m_document->objects) {
        if (object.kind == ObjectKind::text)
            add(object.text);
    }
    return used;
}

QStringList EditorSession::missingFonts() const
{
    QStringList missing = usedFonts();
    missing.removeIf([](const QString &family) { return isFontInstalled(family); });
    return missing;
}

int EditorSession::replaceFont(const QString &from, const QString &to, bool selectionOnly)
{
    if (!m_document || from == to || to.isEmpty())
        return 0;
    std::vector<QUuid> texts = selectionOnly ? selectedTexts() : std::vector<QUuid>{};
    if (!selectionOnly) {
        for (const VectorObject &object : m_document->objects) {
            if (object.kind == ObjectKind::text)
                texts.push_back(object.id);
        }
    }
    // The nearest face in the new family, as choosing a family in Character does.
    const auto swap = [&](CharacterFormat &format) {
        if (format.family != from)
            return;
        const int weight = format.isBold() ? 700 : 400;
        const bool italic = format.isItalic();
        format.family = to;
        format.style = TextContent::styleFor(to, weight, italic);
    };
    VectorDocument next = *m_document;
    int changed = 0;
    for (const QUuid &id : texts) {
        if (next.isEffectivelyLocked(id))
            continue;
        TextContent &text = next.find(id)->text;
        const TextContent before = text;
        eachCharacter(text, std::nullopt, swap);
        text.normalize();
        changed += before == text ? 0 : 1;
    }
    if (!selectionOnly) {
        for (TextStyle &style : next.textStyles)
            swap(style.character);
    }
    if (next == *m_document)
        return 0;
    commitTextEdit(std::move(next), QStringLiteral("Replace Font"), false);
    return changed;
}

void EditorSession::selectTextsUsing(const QString &family)
{
    if (!m_document)
        return;
    std::vector<QUuid> using_;
    for (const VectorObject &object : m_document->objects) {
        if (object.kind == ObjectKind::text && object.text.families().contains(family) && m_document->isEffectivelyVisible(object.id)
            && !m_document->isEffectivelyLocked(object.id))
            using_.push_back(object.id);
    }
    select(using_);
}
