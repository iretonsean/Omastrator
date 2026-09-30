#include "Live/CssRules.h"
#include <QRegularExpression>

namespace CssRules {
namespace {
QString cleaned(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('/') && i + 1 < text.size() && text[i + 1] == QLatin1Char('*')) {
            const qsizetype end = text.indexOf(QStringLiteral("*/"), i + 2);
            i = end < 0 ? text.size() : end + 1;
            out += QLatin1Char(' ');
        } else {
            out += text[i];
        }
    }
    return out.simplified();
}
}

std::vector<Rule> scan(const QString &css)
{
    std::vector<Rule> found;
    struct Open {
        qsizetype brace;
        qsizetype start;
        QString prelude;
    };
    std::vector<Open> stack;
    std::vector<Rule> closed;
    qsizetype boundary = 0;
    for (qsizetype i = 0; i < css.size(); ++i) {
        const QChar c = css[i];
        if (c == QLatin1Char('/') && i + 1 < css.size() && css[i + 1] == QLatin1Char('*')) {
            const qsizetype end = css.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0)
                break;
            i = end + 1;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            for (++i; i < css.size() && css[i] != c; ++i)
                if (css[i] == QLatin1Char('\\'))
                    ++i;
        } else if (c == QLatin1Char('{')) {
            stack.push_back({i, boundary, cleaned(css.mid(boundary, i - boundary))});
            boundary = i + 1;
        } else if (c == QLatin1Char('}')) {
            if (!stack.empty()) {
                const Open open = stack.back();
                stack.pop_back();
                Rule rule;
                rule.prelude = open.prelude;
                rule.open = open.brace;
                rule.close = i;
                // The prelude starts at its first character that isn't space, so cutting the rule leaves the line before it.
                qsizetype start = open.start;
                while (start < open.brace && css[start].isSpace())
                    ++start;
                rule.start = start;
                rule.depth = int(stack.size());
                closed.push_back(rule);
            }
            boundary = i + 1;
        } else if (c == QLatin1Char(';')) {
            boundary = i + 1;
        }
    }
    found = closed;
    std::sort(found.begin(), found.end(), [](const Rule &a, const Rule &b) { return a.open < b.open; });
    return found;
}

std::vector<Declaration> declarations(const QString &css, const Rule &rule, const QString &property)
{
    std::vector<Declaration> found;
    const qsizetype from = rule.open + 1;
    QString body = css.mid(from, rule.close - from);
    // Blocks nested in the rule hold declarations of their own; they are blanked so only this rule's are read.
    int depth = 0;
    for (qsizetype i = 0; i < body.size(); ++i) {
        if (body[i] == QLatin1Char('{')) {
            ++depth;
        } else if (body[i] == QLatin1Char('}')) {
            --depth;
        } else if (depth > 0 && body[i] != QLatin1Char('\n')) {
            body[i] = QLatin1Char(' ');
        }
    }
    const QRegularExpression pattern(QStringLiteral(R"((^|[;{}\s])(%1)\s*:\s*([^;{}]*?)\s*(!important)?\s*(;|$))").arg(QRegularExpression::escape(property)),
                                     QRegularExpression::MultilineOption);
    for (const QRegularExpressionMatch &match : pattern.globalMatch(body)) {
        Declaration each;
        each.valueStart = from + match.capturedStart(3);
        each.valueEnd = from + match.capturedEnd(3);
        each.start = from + match.capturedStart(2);
        each.end = from + match.capturedEnd(0);
        found.push_back(each);
    }
    return found;
}

QString append(const QString &css, const Rule &rule, const QString &property, const QString &value)
{
    QString out = css;
    const QString body = css.mid(rule.open + 1, rule.close - rule.open - 1);
    const QString declaration = property + QStringLiteral(": ") + value + QLatin1Char(';');
    // Where the last declaration ends, so the new one follows it whatever spaces come before the brace.
    qsizetype last = body.size();
    while (last > 0 && body[last - 1].isSpace())
        --last;
    QString text = body.left(last);
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(';')) && !text.endsWith(QLatin1Char('}')))
        text += QLatin1Char(';');
    if (body.contains(QLatin1Char('\n'))) {
        // The indent of the rule's own last line of declarations.
        QString indent = QStringLiteral("  ");
        const qsizetype line = text.lastIndexOf(QLatin1Char('\n'));
        if (line >= 0) {
            qsizetype end = line + 1;
            while (end < text.size() && (text[end] == QLatin1Char(' ') || text[end] == QLatin1Char('\t')))
                ++end;
            indent = text.mid(line + 1, end - line - 1);
        }
        text += QLatin1Char('\n') + indent + declaration + body.mid(last);
    } else {
        text += QLatin1Char(' ') + declaration + (body.mid(last).isEmpty() ? QStringLiteral(" ") : body.mid(last));
    }
    out.replace(rule.open + 1, rule.close - rule.open - 1, text);
    return out;
}

std::vector<Rule> children(const std::vector<Rule> &all, const Rule &rule)
{
    std::vector<Rule> found;
    for (const Rule &each : all)
        if (each.open > rule.open && each.close < rule.close && each.depth == rule.depth + 1)
            found.push_back(each);
    return found;
}
}
