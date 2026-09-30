#include "Live/MotionWrite.h"
#include "Live/CssRules.h"
#include <QRegularExpression>

namespace MotionWrite {
namespace {
QString simplified(const QString &text)
{
    return text.simplified();
}

// "from" is 0% and "to" is 100%, and a frame may be one of a list ("0%, 50%").
bool isFrame(const QString &prelude, const QString &frame)
{
    const auto alias = [](QString each) {
        each = each.trimmed().toLower();
        return each == QLatin1String("from") ? QStringLiteral("0%") : each == QLatin1String("to") ? QStringLiteral("100%") : each;
    };
    for (const QString &part : prelude.split(QLatin1Char(',')))
        if (alias(part) == alias(frame))
            return true;
    return false;
}

// A value that ends the declaration or the block would break the user's stylesheet: it is not written, and the agent gets the edit.
bool unsafeValue(const QString &value)
{
    return value.contains(QLatin1Char(';')) || value.contains(QLatin1Char('{')) || value.contains(QLatin1Char('}'));
}

// A frame rule that lists more than one offset ("0%, 100% { … }") is not one frame: the page's preview changes the one that was
// asked for, and the code would change both.
bool listsFrames(const CssRules::Rule &rule)
{
    return rule.prelude.contains(QLatin1Char(','));
}

struct Placed {
    QString *text = nullptr;
    CssRules::Rule rule;
};

// A spot to write a value: the first declaration of `property` in `rule`, when it has exactly one.
Result replaceIn(QString &text, const CssRules::Rule &rule, const QString &property, const QString &value)
{
    const auto found = CssRules::declarations(text, rule, property);
    if (found.size() != 1)
        return found.empty() ? Result::none : Result::ambiguous;
    text.replace(found.front().valueStart, found.front().valueEnd - found.front().valueStart, value);
    return Result::written;
}

const QRegularExpression &startMarker()
{
    static const QRegularExpression pattern(QStringLiteral(R"(/\*\s*omastrator:motion\s+([^\s*]+)\s*\*/)"));
    return pattern;
}

const QRegularExpression &endMarker()
{
    static const QRegularExpression pattern(QStringLiteral(R"(/\*\s*omastrator:motion\s+end\s*\*/)"));
    return pattern;
}

const QRegularExpression &reducedPrelude()
{
    static const QRegularExpression pattern(QStringLiteral(R"(^@media\b.*prefers-reduced-motion\s*:\s*reduce)"));
    return pattern;
}
}

Result customProperty(const Files &files, const QString &selector, const QString &property, const QString &value)
{
    if (unsafeValue(value))
        return Result::ambiguous;
    const QString wanted = simplified(selector);
    std::vector<Placed> rules;
    for (QString *text : files)
        for (const CssRules::Rule &rule : CssRules::scan(*text))
            if (!rule.prelude.startsWith(QLatin1Char('@')) && rule.prelude == wanted)
                rules.push_back({text, rule});
    if (rules.empty())
        return Result::none;
    // The rule that names the element occurs once, or nothing here is sure.
    if (rules.size() != 1)
        return Result::ambiguous;
    QString &text = *rules.front().text;
    const CssRules::Rule &rule = rules.front().rule;
    if (value.isEmpty()) {
        const auto found = CssRules::declarations(text, rule, property);
        if (found.size() > 1)
            return Result::ambiguous;
        if (found.empty())
            return Result::written;
        // The declaration and the space before it; a line of its own goes with its newline.
        qsizetype from = found.front().start;
        qsizetype to = found.front().end;
        while (from > rule.open + 1 && (text[from - 1] == QLatin1Char(' ') || text[from - 1] == QLatin1Char('\t')))
            --from;
        const bool lineBefore = from > 0 && text[from - 1] == QLatin1Char('\n');
        const bool lineAfter = to >= text.size() || text[to] == QLatin1Char('\n');
        if (lineBefore && lineAfter && to < text.size())
            ++to;
        text.remove(from, to - from);
        return Result::written;
    }
    const Result replaced = replaceIn(text, rule, property, value);
    if (replaced == Result::none) {
        text = CssRules::append(text, rule, property, value);
        return Result::written;
    }
    return replaced;
}

Result keyframeValue(const Files &files, const QString &name, const QString &frame, const QString &property, const QString &value)
{
    if (unsafeValue(value))
        return Result::ambiguous;
    const QString wanted = QStringLiteral("@keyframes ") + simplified(name);
    std::vector<Placed> blocks;
    for (QString *text : files)
        for (const CssRules::Rule &rule : CssRules::scan(*text))
            if (rule.prelude == wanted)
                blocks.push_back({text, rule});
    if (blocks.empty())
        return Result::none;
    // The name occurs once across the project: a second block of it would leave the page's rule in doubt.
    if (blocks.size() != 1)
        return Result::ambiguous;
    QString &text = *blocks.front().text;
    const std::vector<CssRules::Rule> all = CssRules::scan(text);
    std::vector<CssRules::Rule> matching;
    for (const CssRules::Rule &child : CssRules::children(all, blocks.front().rule))
        if (isFrame(child.prelude, frame))
            matching.push_back(child);
    if (matching.size() != 1)
        return matching.empty() ? Result::none : Result::ambiguous;
    if (listsFrames(matching.front()))
        return Result::ambiguous;
    return replaceIn(text, matching.front(), property, value);
}

Result keyframeBody(const Files &files, const QString &name, const QString &frame, const QString &declarations)
{
    // A whole frame is several declarations, so `;` is expected here; a brace is not.
    if (declarations.contains(QLatin1Char('{')) || declarations.contains(QLatin1Char('}')))
        return Result::ambiguous;
    const QString wanted = QStringLiteral("@keyframes ") + simplified(name);
    std::vector<Placed> blocks;
    for (QString *text : files)
        for (const CssRules::Rule &rule : CssRules::scan(*text))
            if (rule.prelude == wanted)
                blocks.push_back({text, rule});
    if (blocks.empty())
        return Result::none;
    if (blocks.size() != 1)
        return Result::ambiguous;
    QString &text = *blocks.front().text;
    const std::vector<CssRules::Rule> all = CssRules::scan(text);
    std::vector<CssRules::Rule> matching;
    for (const CssRules::Rule &child : CssRules::children(all, blocks.front().rule))
        if (isFrame(child.prelude, frame))
            matching.push_back(child);
    if (matching.size() != 1)
        return matching.empty() ? Result::none : Result::ambiguous;
    if (listsFrames(matching.front()))
        return Result::ambiguous;
    const CssRules::Rule &rule = matching.front();
    const QString old = text.mid(rule.open + 1, rule.close - rule.open - 1);
    // The frame's own easing is not part of what an effect changes: the page keeps it, and so does the code.
    QString kept;
    if (!declarations.contains(QLatin1String("animation-timing-function")))
        for (const CssRules::Declaration &each : CssRules::declarations(text, rule, QStringLiteral("animation-timing-function")))
            kept += QStringLiteral(" animation-timing-function: %1;").arg(text.mid(each.valueStart, each.valueEnd - each.valueStart));
    const QString frameBody = kept.isEmpty() ? declarations : declarations.trimmed() + (declarations.trimmed().endsWith(QLatin1Char(';')) ? QString() : QStringLiteral(";")) + kept;
    QString body;
    if (old.contains(QLatin1Char('\n'))) {
        // The lines keep the indent they had, and the brace its own.
        const qsizetype first = old.indexOf(QLatin1Char('\n')) + 1;
        qsizetype end = first;
        while (end < old.size() && (old[end] == QLatin1Char(' ') || old[end] == QLatin1Char('\t')))
            ++end;
        const QString indent = old.mid(first, end - first);
        const qsizetype lastBreak = old.lastIndexOf(QLatin1Char('\n'));
        const QString outer = old.mid(lastBreak + 1);
        // The last declaration shares its line with the brace: this is not the brace's indent, and it would stay behind.
        if (!outer.trimmed().isEmpty())
            return Result::ambiguous;
        body = QStringLiteral("\n");
        for (const QString &part : frameBody.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            body += indent + part.trimmed() + QStringLiteral(";\n");
        body += outer;
    } else {
        QStringList parts;
        for (const QString &part : frameBody.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            parts << part.trimmed() + QLatin1Char(';');
        body = QLatin1Char(' ') + parts.join(QLatin1Char(' ')) + QLatin1Char(' ');
    }
    text.replace(rule.open + 1, rule.close - rule.open - 1, body);
    return Result::written;
}

std::optional<Span> reducedRule(const QString &text, qsizetype from, qsizetype to)
{
    const QString region = text.mid(from, to - from);
    const std::vector<CssRules::Rule> rules = CssRules::scan(region);
    std::optional<Span> found;
    for (const CssRules::Rule &rule : rules) {
        if (!reducedPrelude().match(rule.prelude).hasMatch())
            continue;
        // Only the top of the block: a rule inside another media rule is not the toggle's.
        if (rule.depth != 0)
            continue;
        if (found)
            return std::nullopt;
        qsizetype start = rule.start;
        while (start > 0 && (region[start - 1] == QLatin1Char(' ') || region[start - 1] == QLatin1Char('\t')))
            --start;
        qsizetype end = rule.close + 1;
        if (end < region.size() && region[end] == QLatin1Char('\n'))
            ++end;
        found = Span{from + start, from + end};
    }
    return found;
}

Result reducedMotion(const Files &files, const QString &block, const QString &removed, const QString &added)
{
    struct Where {
        QString *text;
        qsizetype from;
        qsizetype to;
        qsizetype endMarker;
    };
    std::vector<Where> found;
    for (QString *text : files) {
        for (const QRegularExpressionMatch &match : startMarker().globalMatch(*text)) {
            if (match.captured(1) != block)
                continue;
            const QRegularExpressionMatch end = endMarker().match(*text, match.capturedEnd());
            if (end.hasMatch())
                found.push_back({text, match.capturedEnd(), end.capturedStart(), end.capturedStart()});
        }
    }
    if (found.empty())
        return Result::none;
    if (found.size() != 1)
        return Result::ambiguous;
    QString &text = *found.front().text;
    const auto rule = reducedRule(text, found.front().from, found.front().to);
    if (added.isEmpty()) {
        // Taking it out: it is there exactly once, and is the text that was seen.
        if (!rule)
            return Result::none;
        if (text.mid(rule->start, rule->end - rule->start).trimmed() != removed.trimmed())
            return Result::ambiguous;
        text.remove(rule->start, rule->end - rule->start);
        return Result::written;
    }
    // Putting it back: the block has none, and the text goes on its own line before the end marker.
    if (rule)
        return Result::ambiguous;
    qsizetype at = found.front().endMarker;
    while (at > found.front().from && (text[at - 1] == QLatin1Char(' ') || text[at - 1] == QLatin1Char('\t')))
        --at;
    QString insert = added.trimmed() + QLatin1Char('\n');
    if (at > 0 && text[at - 1] != QLatin1Char('\n'))
        insert.prepend(QLatin1Char('\n'));
    text.insert(at, insert);
    return Result::written;
}
}
