#include "Live/Tokens.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <optional>

namespace {
// Tailwind's spacing steps, in units of --spacing.
constexpr double spacingSteps[] = {0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 72, 80, 96};
// Beyond this CIE76 distance a picked colour is the user's own, not a token.
constexpr double colorThreshold = 18;

struct Lab {
    double l, a, b;
};

Lab lab(const QColor &color)
{
    auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const double r = linear(color.redF()), g = linear(color.greenF()), b = linear(color.blueF());
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116; };
    return {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
}

double distance(const QColor &left, const QColor &right)
{
    const Lab a = lab(left), b = lab(right);
    return std::hypot(a.l - b.l, a.a - b.a, a.b - b.b);
}

QString number(double value)
{
    return QString::number(value, 'g', 6);
}

// A step as Tailwind writes it: 3, 2.5, 0.
QString stepSuffix(double step)
{
    return std::fmod(step, 1.0) == 0 ? QString::number(int(step)) : QString::number(step, 'f', 1);
}
}

std::optional<Token::Group> TokenSet::groupOf(const QString &property)
{
    if (property == QLatin1String("color") || property == QLatin1String("background-color"))
        return Token::Group::color;
    if (property.startsWith(QLatin1String("padding")) || property.startsWith(QLatin1String("margin")) || property == QLatin1String("width")
        || property == QLatin1String("height") || property == QLatin1String("gap"))
        return Token::Group::spacing;
    if (property == QLatin1String("font-size"))
        return Token::Group::fontSize;
    if (property == QLatin1String("font-weight"))
        return Token::Group::fontWeight;
    if (property == QLatin1String("border-radius"))
        return Token::Group::radius;
    return std::nullopt;
}

QString TokenSet::tailwindPrefix(const QString &property)
{
    static const QHash<QString, QString> prefixes{
        {"color", "text-"}, {"background-color", "bg-"}, {"padding", "p-"}, {"padding-top", "pt-"}, {"padding-right", "pr-"},
        {"padding-bottom", "pb-"}, {"padding-left", "pl-"}, {"margin", "m-"}, {"margin-top", "mt-"}, {"margin-right", "mr-"},
        {"margin-bottom", "mb-"}, {"margin-left", "ml-"}, {"width", "w-"}, {"height", "h-"}, {"gap", "gap-"},
        {"font-size", "text-"}, {"font-weight", "font-"}, {"border-radius", "rounded-"}};
    return prefixes.value(property);
}

std::optional<double> TokenSet::pixels(const QString &value, double rootFontSize)
{
    static const QRegularExpression length(QStringLiteral(R"(^\s*(-?\d*\.?\d+)\s*(px|rem|em)?\s*$)"));
    const auto match = length.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    const double amount = match.captured(1).toDouble();
    return match.captured(2) == QLatin1String("rem") || match.captured(2) == QLatin1String("em") ? amount * rootFontSize : amount;
}

TokenSet TokenSet::fromScan(const QJsonObject &scan, const std::vector<std::pair<QString, QColor>> &omarchy)
{
    TokenSet set;
    set.m_rootFontSize = scan["rootFontSize"].toDouble(16);
    const QJsonObject vars = scan["vars"].toObject();
    static const QRegularExpression colorVar(QStringLiteral("^--color-(.+)$"));
    static const QRegularExpression textVar(QStringLiteral("^--text-([a-z0-9]+)$"));
    static const QRegularExpression weightVar(QStringLiteral("^--font-weight-(.+)$"));
    static const QRegularExpression radiusVar(QStringLiteral("^--radius-(.+)$"));
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        const QString name = it.key();
        const QJsonObject resolved = it.value().toObject();
        const QString kind = resolved["kind"].toString();
        if (name == QLatin1String("--spacing") && kind == QLatin1String("length")) {
            set.m_spacingPx = resolved["px"].toDouble();
            continue;
        }
        Token token{Token::Group::color, Token::Source::css, name, resolved["value"].toString(), {}, {}, 0};
        if (kind == QLatin1String("color")) {
            token.color = QColor::fromString(resolved["rgb"].toString());
            if (!token.color.isValid())
                continue;
            token.value = token.color.name();
            if (const auto match = colorVar.match(name); match.hasMatch()) {
                token.source = Token::Source::tailwind;
                token.suffix = match.captured(1);
            }
        } else if (kind == QLatin1String("length")) {
            token.number = resolved["px"].toDouble();
            token.value = number(token.number) + QStringLiteral("px");
            if (const auto match = textVar.match(name); match.hasMatch()) {
                token.group = Token::Group::fontSize;
                token.source = Token::Source::tailwind;
                token.suffix = match.captured(1);
            } else if (const auto radius = radiusVar.match(name); radius.hasMatch()) {
                token.group = Token::Group::radius;
                token.source = Token::Source::tailwind;
                token.suffix = radius.captured(1);
            } else if (name.contains(QLatin1String("radius"))) {
                token.group = Token::Group::radius;
            } else if (name.contains(QLatin1String("font-size")) || name.contains(QLatin1String("text"))) {
                token.group = Token::Group::fontSize;
            } else {
                token.group = Token::Group::spacing;
            }
        } else if (kind == QLatin1String("number")) {
            if (const auto match = weightVar.match(name); match.hasMatch()) {
                token.group = Token::Group::fontWeight;
                token.source = Token::Source::tailwind;
                token.suffix = match.captured(1);
                token.number = resolved["value"].toString().toDouble();
            } else {
                continue;
            }
        } else {
            continue;
        }
        if (token.source == Token::Source::tailwind)
            set.m_tailwind = true;
        set.m_tokens.push_back(token);
    }
    if (set.m_spacingPx > 0) {
        for (const double step : spacingSteps) {
            const double px = step * set.m_spacingPx;
            set.m_tokens.push_back({Token::Group::spacing, Token::Source::tailwind, QStringLiteral("spacing ") + stepSuffix(step),
                                    number(px) + QStringLiteral("px"), stepSuffix(step), {}, px});
        }
    }
    for (const auto &[name, color] : omarchy)
        set.m_tokens.push_back({Token::Group::color, Token::Source::omarchy, QStringLiteral("Omarchy ") + name, color.name(), {}, color, 0});
    return set;
}

TokenSet::Resolution TokenSet::resolve(const QString &property, const QString &value, const QStringList &classes) const
{
    Resolution resolution{property, value.trimmed(), {}, {}, {}};
    const auto group = groupOf(property);
    if (!group)
        return resolution;
    const Token *best = nullptr;
    if (*group == Token::Group::color) {
        const QColor wanted = QColor::fromString(resolution.value);
        if (!wanted.isValid())
            return resolution;
        // The nearest colour, by source order: Tailwind, then CSS, then Omarchy, each within the threshold.
        for (const Token::Source source : {Token::Source::tailwind, Token::Source::css, Token::Source::omarchy}) {
            double nearest = colorThreshold;
            for (const Token &token : m_tokens) {
                if (token.group != Token::Group::color || token.source != source)
                    continue;
                const double gap = distance(wanted, token.color);
                if (gap <= nearest) {
                    nearest = gap;
                    best = &token;
                }
            }
            if (best)
                break;
        }
    } else if (*group == Token::Group::fontWeight) {
        const double wanted = resolution.value.toDouble();
        double nearest = 1e9;
        for (const Token &token : m_tokens) {
            if (token.group == Token::Group::fontWeight && std::abs(token.number - wanted) < nearest) {
                nearest = std::abs(token.number - wanted);
                best = &token;
            }
        }
    } else {
        const auto wanted = pixels(resolution.value, m_rootFontSize);
        if (!wanted)
            return resolution;
        // Lengths always snap to the scale when there is one: that is what the scale is for.
        double nearest = 1e9;
        for (const Token::Source source : {Token::Source::tailwind, Token::Source::css}) {
            for (const Token &token : m_tokens) {
                if (token.group != *group || token.source != source)
                    continue;
                if (std::abs(token.number - *wanted) < nearest) {
                    nearest = std::abs(token.number - *wanted);
                    best = &token;
                }
            }
            if (best)
                break;
        }
    }
    if (!best)
        return resolution;
    resolution.value = best->value;
    resolution.token = best->name;
    // A Tailwind class for this property on the element becomes the token's class.
    const QString prefix = tailwindPrefix(property);
    if (best->source == Token::Source::tailwind && !best->suffix.isEmpty() && !prefix.isEmpty()) {
        for (const QString &cls : classes) {
            if (!cls.startsWith(prefix) || cls.contains(QLatin1Char(':')))
                continue;
            const QString rest = cls.mid(prefix.size());
            // text- is both colour and size: the class must be of this property's own kind.
            bool sameKind = true;
            if (prefix == QLatin1String("text-")) {
                static const QRegularExpression size(QStringLiteral("^(xs|sm|base|lg|[2-9]?xl)$"));
                sameKind = (*group == Token::Group::fontSize) == size.match(rest).hasMatch();
            }
            if (prefix == QLatin1String("font-")) {
                static const QRegularExpression weight(QStringLiteral("^(thin|extralight|light|normal|medium|semibold|bold|extrabold|black)$"));
                sameKind = weight.match(rest).hasMatch();
            }
            if (sameKind) {
                resolution.removeClass = cls;
                resolution.addClass = prefix + best->suffix;
                break;
            }
        }
    }
    return resolution;
}

QJsonObject TokenSet::toJson() const
{
    static const char *groups[] = {"colors", "spacing", "fontSizes", "fontWeights", "radii"};
    QJsonObject json;
    for (const Token &token : m_tokens) {
        const char *key = groups[int(token.group)];
        QJsonArray list = json[QLatin1String(key)].toArray();
        list.append(QJsonObject{{"name", token.name}, {"value", token.value}});
        json[QLatin1String(key)] = list;
    }
    json["tailwind"] = hasTailwind();
    return json;
}
