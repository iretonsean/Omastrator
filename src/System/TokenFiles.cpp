#include "System/TokenFiles.h"
#include "System/TokenFilesParts.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>

namespace TokenFileParts {
QString slug(const QString &text)
{
    QString result;
    for (const QChar c : text.trimmed().toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_'))
            result += c;
        else if (!result.endsWith(QLatin1Char('-')))
            result += QLatin1Char('-');
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result;
}

QString prefixOf(TokenKind kind)
{
    switch (kind) {
    case TokenKind::color:
        return QStringLiteral("color");
    case TokenKind::spacing:
        return QStringLiteral("spacing");
    case TokenKind::radius:
        return QStringLiteral("radius");
    case TokenKind::shadow:
        return QStringLiteral("shadow");
    case TokenKind::type:
        return QStringLiteral("text");
    case TokenKind::duration:
        return QStringLiteral("duration");
    case TokenKind::easing:
        return QStringLiteral("ease");
    }
    return {};
}

bool isAlias(TokenKind kind, const QString &segment)
{
    static const std::map<TokenKind, QStringList> aliases{
        {TokenKind::color, {"color", "colors", "colour", "colours"}},
        {TokenKind::spacing, {"spacing", "space", "spaces", "size", "sizes", "gap"}},
        {TokenKind::radius, {"radius", "radii", "rounded", "border-radius", "borderradius", "corner"}},
        {TokenKind::shadow, {"shadow", "shadows", "box-shadow", "boxshadow", "elevation"}},
        {TokenKind::type, {"text", "type", "typography", "font", "fonts", "font-size", "fontsize"}},
        {TokenKind::duration, {"duration", "durations"}},
        {TokenKind::easing, {"ease", "easing", "easings"}},
    };
    return aliases.at(kind).contains(segment.toLower());
}

// The name without a leading group that just repeats the kind ("color/brand" → "brand").
QString restOf(const DesignToken &token)
{
    QStringList parts = token.name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() > 1 && isAlias(token.kind, parts.front()))
        parts.removeFirst();
    return slug(parts.join(QLatin1Char('-')));
}

QString numeral(double value)
{
    return QString::number(std::round(value * 1000) / 1000);
}

QString length(double points, const QString &like)
{
    if (like.trimmed().endsWith(QLatin1String("rem")))
        return numeral(points / 16) + QStringLiteral("rem");
    return numeral(points) + QStringLiteral("px");
}

std::vector<Declaration> scan(const QString &css, std::vector<Block> *blocks)
{
    std::vector<Declaration> declarations;
    struct Open {
        QString selector;
        qsizetype at;
    };
    std::vector<Open> stack;
    qsizetype segment = 0;
    const auto contextOf = [&]() {
        bool theme = false, dark = false, root = false;
        for (const Open &open : stack) {
            const QString selector = open.selector.simplified();
            if (selector.startsWith(QLatin1String("@theme")))
                theme = true;
            if (selector.contains(QLatin1String("prefers-color-scheme: dark")) || selector.contains(QLatin1String("prefers-color-scheme:dark"))
                || selector.contains(QLatin1String(".dark")) || selector.contains(QRegularExpression(QStringLiteral("data-(theme|mode)=[\"']?dark"))))
                dark = true;
            if (selector.contains(QLatin1String(":root")) || selector == QLatin1String("html") || selector.contains(QLatin1String(":host")))
                root = true;
        }
        return theme ? QStringLiteral("theme") : dark ? QStringLiteral("dark") : root ? QStringLiteral("root") : QStringLiteral("other");
    };
    const auto declaration = [&](qsizetype end) {
        const QString text = css.mid(segment, end - segment);
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        const QString name = colon < 0 ? QString() : text.left(colon).trimmed();
        if (colon < 0 || !name.startsWith(QLatin1String("--")) || stack.empty())
            return;
        qsizetype start = segment + colon + 1;
        qsizetype stop = end;
        while (start < stop && css[start].isSpace())
            ++start;
        while (stop > start && css[stop - 1].isSpace())
            --stop;
        declarations.push_back({name, css.mid(start, stop - start), start, stop, contextOf()});
    };
    for (qsizetype at = 0; at < css.size(); ++at) {
        const QChar c = css[at];
        if (c == QLatin1Char('/') && at + 1 < css.size() && css[at + 1] == QLatin1Char('*')) {
            const qsizetype end = css.indexOf(QStringLiteral("*/"), at + 2);
            at = end < 0 ? css.size() : end + 1;
            if (segment <= at && css.mid(segment, at + 1 - segment).trimmed().startsWith(QLatin1String("/*")))
                segment = at + 1;
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const qsizetype end = css.indexOf(c, at + 1);
            at = end < 0 ? css.size() : end;
            continue;
        }
        if (c == QLatin1Char('{')) {
            stack.push_back({css.mid(segment, at - segment).trimmed(), at});
            segment = at + 1;
        } else if (c == QLatin1Char(';')) {
            declaration(at);
            segment = at + 1;
        } else if (c == QLatin1Char('}')) {
            declaration(at);
            if (!stack.empty()) {
                const QString context = contextOf();
                if (blocks)
                    blocks->push_back({stack.back().selector, context, stack.back().at, at});
                stack.pop_back();
            }
            segment = at + 1;
        }
    }
    return declarations;
}

// Values set by var(--other) take the other's value.
QString resolved(const QString &value, const std::map<QString, QString> &variables, int depth)
{
    static const QRegularExpression reference(QStringLiteral(R"(^var\(\s*(--[\w-]+)\s*(?:,\s*(.*))?\)$)"));
    const auto match = reference.match(value.trimmed());
    if (!match.hasMatch() || depth > 8)
        return value;
    const auto found = variables.find(match.captured(1));
    if (found == variables.end())
        return match.captured(2);
    return resolved(found->second, variables, depth + 1);
}

QString familyOf(const QString &stack)
{
    QString first = stack.split(QLatin1Char(',')).value(0).trimmed();
    if (first.size() >= 2 && (first.front() == QLatin1Char('"') || first.front() == QLatin1Char('\'')))
        first = first.mid(1, first.size() - 2);
    if (first == QLatin1String("ui-sans-serif") || first == QLatin1String("system-ui") || first == QLatin1String("sans-serif") || first.isEmpty())
        return QStringLiteral("Sans Serif");
    if (first == QLatin1String("ui-serif") || first == QLatin1String("serif"))
        return QStringLiteral("Serif");
    if (first == QLatin1String("ui-monospace") || first == QLatin1String("monospace"))
        return QStringLiteral("Monospace");
    return first;
}

int weightOf(const QString &value)
{
    static const std::map<QString, int> names{{"thin", 100}, {"extralight", 200}, {"light", 300}, {"normal", 400}, {"regular", 400},
                                              {"medium", 500}, {"semibold", 600}, {"bold", 700}, {"extrabold", 800}, {"black", 900}};
    bool ok = false;
    const int number = value.trimmed().toInt(&ok);
    if (ok)
        return std::clamp(number, 1, 1000);
    const auto found = names.find(value.trimmed().toLower().remove(QLatin1Char('-')));
    return found == names.end() ? 400 : found->second;
}

// A value read from a custom property, in the kind its name or value says.
std::optional<DesignToken> tokenFor(const QString &variable, const QString &value, TokenFiles::Read &read)
{
    const QString bare = variable.mid(2);
    const QString head = bare.section(QLatin1Char('-'), 0, 0);
    const QString tail = bare.section(QLatin1Char('-'), 1);
    const auto named = [&](TokenKind kind) {
        if (isAlias(kind, head) && !tail.isEmpty())
            return prefixOf(kind) + QLatin1Char('/') + tail;
        if (isAlias(kind, bare))
            return prefixOf(kind);
        return prefixOf(kind) + QLatin1Char('/') + bare;
    };
    // Motion: --duration-*, --stagger-* (a duration in a `stagger` group) and --ease-* (Tailwind's own namespace for easings).
    if (head == QLatin1String("duration") || head == QLatin1String("stagger")) {
        if (const auto ms = TokenFiles::parseTime(value))
            return DesignToken::number(TokenKind::duration,
                                       head == QLatin1String("stagger") ? QStringLiteral("stagger/") + (tail.isEmpty() ? QStringLiteral("default") : tail) : named(TokenKind::duration), *ms);
    }
    if ((head == QLatin1String("ease") || head == QLatin1String("easing")) && TokenFiles::isEasing(value))
        return DesignToken::easing(named(TokenKind::easing), value);
    if (const auto colour = TokenFiles::parseColor(value))
        return DesignToken::color(named(TokenKind::color), *colour);
    if (bare.contains(QLatin1String("shadow"))) {
        if (const auto shadow = ShadowValue::fromCss(value))
            return DesignToken::shadowToken(named(TokenKind::shadow), *shadow);
    }
    if (const auto points = TokenFiles::parseLength(value)) {
        if (bare.contains(QLatin1String("radius")) || bare.contains(QLatin1String("rounded")))
            return DesignToken::number(TokenKind::radius, named(TokenKind::radius), *points);
        if (head == QLatin1String("font") || head == QLatin1String("leading") || head == QLatin1String("tracking")
            || bare.contains(QLatin1String("line-height")) || bare.contains(QLatin1String("letter-spacing")))
            return std::nullopt;
        return DesignToken::number(TokenKind::spacing, named(TokenKind::spacing), *points);
    }
    read.skipped.append(variable);
    return std::nullopt;
}

// Type tokens from --text-x and its --text-x--line-height, --letter-spacing, --font-weight, --font-family.
void readType(const std::map<QString, QString> &variables, const QString &family, TokenFiles::Read &read, std::set<QString> &used)
{
    for (const auto &[name, raw] : variables) {
        if (!name.startsWith(QLatin1String("--text-")))
            continue;
        const QString bare = name.mid(2);
        if (bare.contains(QLatin1String("--")))
            continue;
        const QString value = resolved(raw, variables, 0);
        const auto size = TokenFiles::parseLength(value);
        if (!size)
            continue;
        TypeValue type;
        type.family = family;
        type.size = *size;
        const auto sub = [&](const char *key) -> std::optional<QString> {
            const auto found = variables.find(name + QStringLiteral("--") + QLatin1String(key));
            if (found == variables.end())
                return std::nullopt;
            used.insert(found->first);
            return resolved(found->second, variables, 0);
        };
        if (const auto lineHeight = sub("line-height")) {
            if (const auto points = TokenFiles::parseLength(*lineHeight); points && (lineHeight->contains(QLatin1String("px")) || lineHeight->contains(QLatin1String("rem"))))
                type.lineHeight = *points;
            else if (bool ok = false; lineHeight->trimmed().toDouble(&ok) > 0 && ok)
                type.lineHeight = *size * lineHeight->trimmed().toDouble();
            else if (lineHeight->contains(QLatin1String("calc(")))
                type.lineHeight = *size * 1.5;
        }
        if (const auto tracking = sub("letter-spacing")) {
            const QString text = tracking->trimmed();
            if (text.endsWith(QLatin1String("em")) && !text.endsWith(QLatin1String("rem")))
                type.tracking = text.chopped(2).toDouble() * 1000;
            else if (const auto points = TokenFiles::parseLength(text))
                type.tracking = *points / *size * 1000;
        }
        if (const auto weight = sub("font-weight"))
            type.weight = weightOf(*weight);
        if (const auto own = sub("font-family"))
            type.family = familyOf(*own);
        used.insert(name);
        read.tokens.push_back(DesignToken::typography(QStringLiteral("text/") + bare.mid(5), type));
    }
}

TokenFiles::Read readVariables(const QString &css, const QString &context)
{
    TokenFiles::Read read;
    std::map<QString, QString> base, dark;
    for (const Declaration &declaration : scan(css, nullptr)) {
        if (declaration.context == context)
            base[declaration.name] = declaration.value;
        else if (declaration.context == QLatin1String("dark"))
            dark[declaration.name] = declaration.value;
    }
    std::map<QString, QString> all = base;
    for (const auto &[name, value] : dark)
        all.emplace(name, value);
    QString family = QStringLiteral("Sans Serif");
    if (const auto sans = base.find(QStringLiteral("--font-sans")); sans != base.end())
        family = familyOf(resolved(sans->second, base, 0));
    std::set<QString> used;
    readType(base, family, read, used);
    for (const auto &[name, raw] : base) {
        if (used.count(name) || name.startsWith(QLatin1String("--font-")) || name.mid(2).contains(QLatin1String("--")))
            continue;
        if (name == QLatin1String("--spacing") && TokenFiles::parseLength(resolved(raw, base, 0))) {
            read.tokens.push_back(DesignToken::number(TokenKind::spacing, QStringLiteral("spacing"), *TokenFiles::parseLength(resolved(raw, base, 0))));
            continue;
        }
        auto token = tokenFor(name, resolved(raw, base, 0), read);
        if (!token)
            continue;
        if (const auto darker = dark.find(name); darker != dark.end()) {
            if (auto other = tokenFor(name, resolved(darker->second, all, 0), read); other && other->kind == token->kind) {
                token->modes[QStringLiteral("dark")] = other->value;
                if (!read.modes.contains(QStringLiteral("dark")))
                    read.modes = {QStringLiteral("light"), QStringLiteral("dark")};
            }
        }
        read.tokens.push_back(*token);
    }
    return read;
}

// The custom properties a token writes, with their values.
std::vector<std::pair<QString, QString>> variablesFor(const DesignToken &token, const TokenValue &value, bool tailwind,
                                                      const std::map<QString, QString> &existing)
{
    const QString name = TokenFiles::cssVariable(token);
    const auto like = [&](const QString &variable) {
        const auto found = existing.find(variable);
        return found == existing.end() ? QString() : found->second;
    };
    switch (token.kind) {
    case TokenKind::color:
        return {{name, TokenFiles::cssColor(value.color)}};
    case TokenKind::spacing:
    case TokenKind::radius:
        return {{name, length(value.number, like(name))}};
    case TokenKind::shadow:
        return {{name, value.shadow.css()}};
    case TokenKind::duration: {
        // A file that writes seconds keeps writing seconds.
        const QString was = like(name).trimmed();
        if (was.endsWith(QLatin1Char('s')) && !was.endsWith(QLatin1String("ms")))
            return {{name, numeral(value.number / 1000) + QStringLiteral("s")}};
        return {{name, numeral(value.number) + QStringLiteral("ms")}};
    }
    case TokenKind::easing:
        return {{name, value.text}};
    case TokenKind::type: {
        std::vector<std::pair<QString, QString>> list{{name, length(value.type.size, like(name))}};
        if (value.type.lineHeight)
            list.emplace_back(name + QStringLiteral("--line-height"), length(*value.type.lineHeight, like(name + QStringLiteral("--line-height"))));
        if (value.type.tracking != 0)
            list.emplace_back(name + QStringLiteral("--letter-spacing"), numeral(value.type.tracking / 1000) + QStringLiteral("em"));
        list.emplace_back(name + QStringLiteral("--font-weight"), QString::number(value.type.weight));
        if (!tailwind)
            list.emplace_back(name + QStringLiteral("--font-family"), QLatin1Char('"') + value.type.family + QLatin1Char('"'));
        return list;
    }
    }
    return {};
}

QByteArray writeVariables(const std::vector<DesignToken> &tokens, const QByteArray &existing, const QString &context, const QStringList &modes)
{
    QString css = QString::fromUtf8(existing);
    std::vector<Block> blocks;
    const std::vector<Declaration> declarations = scan(css, &blocks);
    std::map<QString, QString> present;
    for (const Declaration &declaration : declarations) {
        if (declaration.context == context)
            present[declaration.name] = declaration.value;
    }
    const bool tailwind = context == QLatin1String("theme");
    const QString darkMode = modes.size() > 1 ? modes.at(1) : QString();
    // Wanted values by context; a variable already written under its short name keeps that name.
    std::vector<std::pair<QString, std::vector<std::pair<QString, QString>>>> wanted{{context, {}}, {QStringLiteral("dark"), {}}};
    for (const DesignToken &token : tokens) {
        for (auto [variable, value] : variablesFor(token, token.value, tailwind, present)) {
            const QString shorter = QStringLiteral("--") + variable.mid(2).section(QLatin1Char('-'), 1);
            if (!present.count(variable) && present.count(shorter) && token.kind != TokenKind::type)
                variable = shorter;
            wanted[0].second.emplace_back(variable, value);
        }
        if (!darkMode.isEmpty() && token.modes.count(darkMode)) {
            for (auto [variable, value] : variablesFor(token, token.modes.at(darkMode), tailwind, present)) {
                const QString shorter = QStringLiteral("--") + variable.mid(2).section(QLatin1Char('-'), 1);
                if (!present.count(variable) && present.count(shorter) && token.kind != TokenKind::type)
                    variable = shorter;
                wanted[1].second.emplace_back(variable, value);
            }
        }
    }
    // Replace in place from the end, so earlier positions hold.
    struct Edit {
        qsizetype from;
        qsizetype to;
        QString text;
    };
    std::vector<Edit> edits;
    for (const auto &[where, list] : wanted) {
        std::vector<std::pair<QString, QString>> added;
        for (const auto &[variable, value] : list) {
            const auto found = std::find_if(declarations.begin(), declarations.end(),
                                            [&](const Declaration &d) { return d.name == variable && d.context == where; });
            if (found != declarations.end()) {
                if (found->value != value)
                    edits.push_back({found->valueStart, found->valueEnd, value});
            } else {
                added.emplace_back(variable, value);
            }
        }
        if (added.empty())
            continue;
        const auto block = std::find_if(blocks.rbegin(), blocks.rend(), [&](const Block &b) {
            return b.context == where && (where != QLatin1String("dark") || b.selector.contains(QLatin1String(":root")) || !b.selector.startsWith(QLatin1Char('@')));
        });
        const bool nested = where == QLatin1String("dark");
        QString lines;
        for (const auto &[variable, value] : added)
            lines += (nested && block == blocks.rend() ? QStringLiteral("    ") : QStringLiteral("  ")) + variable + QStringLiteral(": ") + value + QStringLiteral(";\n");
        if (block != blocks.rend()) {
            // Before the closing brace, on a line of its own.
            qsizetype at = block->close;
            while (at > block->open + 1 && css[at - 1] == QLatin1Char(' '))
                --at;
            const bool newline = at > 0 && css[at - 1] == QLatin1Char('\n');
            edits.push_back({at, at, (newline ? QString() : QStringLiteral("\n")) + lines});
        } else if (where == QLatin1String("theme")) {
            const qsizetype import = css.indexOf(QStringLiteral("@import \"tailwindcss\""));
            const qsizetype after = import < 0 ? css.size() : css.indexOf(QLatin1Char('\n'), import) < 0 ? css.size() : css.indexOf(QLatin1Char('\n'), import) + 1;
            edits.push_back({after, after, QStringLiteral("\n@theme {\n") + lines + QStringLiteral("}\n")});
        } else if (nested) {
            edits.push_back({css.size(), css.size(), QStringLiteral("\n@media (prefers-color-scheme: dark) {\n  :root {\n") + lines + QStringLiteral("  }\n}\n")});
        } else {
            edits.push_back({css.size(), css.size(), QStringLiteral(":root {\n") + lines + QStringLiteral("}\n")});
        }
    }
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.from > b.from; });
    for (const Edit &edit : edits)
        css.replace(edit.from, edit.to - edit.from, edit.text);
    if (!css.isEmpty() && !css.endsWith(QLatin1Char('\n')))
        css += QLatin1Char('\n');
    return css.toUtf8();
}

double channel(double linear)
{
    const double clamped = std::clamp(linear, 0.0, 1.0);
    return clamped <= 0.0031308 ? 12.92 * clamped : 1.055 * std::pow(clamped, 1 / 2.4) - 0.055;
}

QColor fromOklab(double l, double a, double b, double alpha)
{
    const double l_ = l + 0.3963377774 * a + 0.2158037573 * b;
    const double m_ = l - 0.1055613458 * a - 0.0638541728 * b;
    const double s_ = l - 0.0894841775 * a - 1.2914855480 * b;
    const double L = l_ * l_ * l_, M = m_ * m_ * m_, S = s_ * s_ * s_;
    const double r = 4.0767416621 * L - 3.3077115913 * M + 0.2309699292 * S;
    const double g = -1.2684380046 * L + 2.6097574011 * M - 0.3413193965 * S;
    const double bl = -0.0041960863 * L - 0.7034186147 * M + 1.7076147010 * S;
    return QColor::fromRgbF(float(channel(r)), float(channel(g)), float(channel(bl)), float(std::clamp(alpha, 0.0, 1.0)));
}
}

using namespace TokenFileParts;

namespace TokenFiles {
QString cssVariable(const DesignToken &token)
{
    // A stagger is a duration named in a `stagger` group, and keeps that name: --stagger-words.
    if (token.kind == TokenKind::duration) {
        const QStringList parts = token.name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() > 1 && parts.front().compare(QLatin1String("stagger"), Qt::CaseInsensitive) == 0)
            return QStringLiteral("--stagger-") + slug(parts.mid(1).join(QLatin1Char('-')));
    }
    const QString rest = restOf(token);
    if (rest.isEmpty() || (token.kind == TokenKind::spacing && isAlias(TokenKind::spacing, token.name)))
        return QStringLiteral("--") + prefixOf(token.kind);
    return QStringLiteral("--") + prefixOf(token.kind) + QLatin1Char('-') + rest;
}

std::optional<QColor> parseColor(const QString &css)
{
    const QString text = css.trimmed().toLower();
    if (text.startsWith(QLatin1Char('#'))) {
        const QString digits = text.mid(1);
        static const QRegularExpression hex(QStringLiteral("^[0-9a-f]+$"));
        if (!hex.match(digits).hasMatch())
            return std::nullopt;
        QString full;
        if (digits.size() == 3 || digits.size() == 4) {
            for (const QChar c : digits)
                full += QString(2, c);
        } else if (digits.size() == 6 || digits.size() == 8) {
            full = digits;
        } else {
            return std::nullopt;
        }
        QColor colour(full.left(2).toInt(nullptr, 16), full.mid(2, 2).toInt(nullptr, 16), full.mid(4, 2).toInt(nullptr, 16));
        if (full.size() == 8)
            colour.setAlpha(full.mid(6, 2).toInt(nullptr, 16));
        return colour;
    }
    static const QRegularExpression function(QStringLiteral(R"(^(rgba?|hsla?|oklch|oklab)\(([^)]*)\)$)"));
    const auto match = function.match(text);
    if (match.hasMatch()) {
        const QString name = match.captured(1);
        QStringList parts = match.captured(2).split(QRegularExpression(QStringLiteral(R"([,\s/]+)")), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            return std::nullopt;
        const auto value = [&](int index, double percentOf) {
            const QString part = parts.at(index);
            if (part == QLatin1String("none"))
                return 0.0;
            if (part.endsWith(QLatin1Char('%')))
                return part.chopped(1).toDouble() / 100 * percentOf;
            return part.endsWith(QLatin1String("deg")) ? part.chopped(3).toDouble() : part.toDouble();
        };
        const double alpha = parts.size() > 3 ? value(3, 1) : 1;
        if (name.startsWith(QLatin1String("rgb")))
            return QColor::fromRgbF(float(std::clamp(value(0, 255) / 255, 0.0, 1.0)), float(std::clamp(value(1, 255) / 255, 0.0, 1.0)),
                                    float(std::clamp(value(2, 255) / 255, 0.0, 1.0)), float(std::clamp(alpha, 0.0, 1.0)));
        if (name.startsWith(QLatin1String("hsl")))
            return QColor::fromHslF(float(std::fmod(value(0, 360) + 360, 360) / 360), float(std::clamp(value(1, 1), 0.0, 1.0)),
                                    float(std::clamp(value(2, 1), 0.0, 1.0)), float(std::clamp(alpha, 0.0, 1.0)));
        const double lightness = value(0, 1);
        if (name == QLatin1String("oklab"))
            return fromOklab(lightness, value(1, 0.4), value(2, 0.4), alpha);
        const double chroma = value(1, 0.4), hue = value(2, 360) * M_PI / 180;
        return fromOklab(lightness, chroma * std::cos(hue), chroma * std::sin(hue), alpha);
    }
    static const QRegularExpression word(QStringLiteral("^[a-z]+$"));
    if (word.match(text).hasMatch() && text != QLatin1String("inherit") && text != QLatin1String("initial") && text != QLatin1String("currentcolor")) {
        const QColor named = QColor::fromString(text);
        if (named.isValid())
            return named;
    }
    return std::nullopt;
}

QString cssColor(const QColor &color)
{
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(numeral(color.alphaF()));
}

std::optional<double> parseTime(const QString &css)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^(-?[0-9]*\.?[0-9]+)\s*(ms|s)$)"));
    const auto match = pattern.match(css.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    const double value = match.captured(1).toDouble();
    return match.captured(2) == QLatin1String("s") ? value * 1000 : value;
}

bool isEasing(const QString &css)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^(cubic-bezier\(.+\)|steps\(.+\)|linear\(.+\)|linear|ease|ease-in|ease-out|ease-in-out|step-start|step-end)$)"));
    return pattern.match(css.trimmed()).hasMatch();
}

std::optional<std::array<double, 4>> cubicBezier(const QString &css)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^cubic-bezier\(\s*(-?[0-9]*\.?[0-9]+)\s*,\s*(-?[0-9]*\.?[0-9]+)\s*,\s*(-?[0-9]*\.?[0-9]+)\s*,\s*(-?[0-9]*\.?[0-9]+)\s*\)$)"));
    const auto match = pattern.match(css.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    return std::array<double, 4>{match.captured(1).toDouble(), match.captured(2).toDouble(), match.captured(3).toDouble(), match.captured(4).toDouble()};
}

QString cubicBezierText(const std::array<double, 4> &points)
{
    return QStringLiteral("cubic-bezier(%1, %2, %3, %4)").arg(numeral(points[0]), numeral(points[1]), numeral(points[2]), numeral(points[3]));
}

std::optional<double> parseLength(const QString &css)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^(-?[0-9]*\.?[0-9]+)(px|rem|em|pt)?$)"));
    const auto match = pattern.match(css.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    const double value = match.captured(1).toDouble();
    const QString unit = match.captured(2);
    if (unit == QLatin1String("rem") || unit == QLatin1String("em"))
        return value * 16;
    if (unit == QLatin1String("pt"))
        return value * 96 / 72;
    return value;
}

Read readTailwind(const QByteArray &css)
{
    return readVariables(QString::fromUtf8(css), QStringLiteral("theme"));
}

QByteArray writeTailwind(const std::vector<DesignToken> &tokens, const QByteArray &existing)
{
    QByteArray base = existing;
    if (base.trimmed().isEmpty())
        base = "@import \"tailwindcss\";\n";
    return writeVariables(tokens, base, QStringLiteral("theme"), {});
}

Read readCss(const QByteArray &css)
{
    return readVariables(QString::fromUtf8(css), QStringLiteral("root"));
}

QByteArray writeCss(const std::vector<DesignToken> &tokens, const QByteArray &existing, const QStringList &modes)
{
    return writeVariables(tokens, existing, QStringLiteral("root"), modes);
}
}
