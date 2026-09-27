#include "System/TokenFiles.h"
#include "System/TokenFilesParts.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>

using namespace TokenFileParts;

namespace {
const QString extension = QStringLiteral("io.github.iretonsean.omastrator");

struct Raw {
    QString type;
    QJsonValue value;
    QJsonObject modes;
};

std::optional<double> dimension(const QJsonValue &value)
{
    if (value.isDouble())
        return value.toDouble();
    if (value.isString())
        return TokenFiles::parseLength(value.toString());
    const QJsonObject object = value.toObject();
    if (!object.contains("value"))
        return std::nullopt;
    const double number = object["value"].toDouble();
    return object["unit"].toString() == QLatin1String("rem") ? number * 16 : number;
}

std::optional<QColor> colourOf(const QJsonValue &value)
{
    if (value.isString())
        return TokenFiles::parseColor(value.toString());
    const QJsonObject object = value.toObject();
    if (object.contains("hex") && !object.contains("components"))
        return TokenFiles::parseColor(object["hex"].toString());
    const QJsonArray components = object["components"].toArray();
    if (components.size() != 3)
        return object.contains("hex") ? TokenFiles::parseColor(object["hex"].toString()) : std::nullopt;
    if (object["colorSpace"].toString(QStringLiteral("srgb")) != QLatin1String("srgb"))
        return object.contains("hex") ? TokenFiles::parseColor(object["hex"].toString()) : std::nullopt;
    return QColor::fromRgbF(float(components[0].toDouble()), float(components[1].toDouble()), float(components[2].toDouble()),
                            float(object["alpha"].toDouble(1)));
}

// A token's value as `kind` from its W3C form, nullopt when it isn't one.
std::optional<TokenValue> valueOf(TokenKind kind, const QJsonValue &value)
{
    TokenValue result;
    switch (kind) {
    case TokenKind::color:
        if (const auto colour = colourOf(value)) {
            result.color = *colour;
            return result;
        }
        return std::nullopt;
    case TokenKind::spacing:
    case TokenKind::radius:
        if (const auto points = dimension(value)) {
            result.number = *points;
            return result;
        }
        return std::nullopt;
    case TokenKind::type: {
        const QJsonObject object = value.toObject();
        const QJsonValue family = object["fontFamily"];
        result.type.family = familyOf(family.isArray() ? family.toArray().first().toString() : family.toString());
        result.type.size = dimension(object["fontSize"]).value_or(16);
        result.type.weight = object["fontWeight"].isDouble() ? object["fontWeight"].toInt() : weightOf(object["fontWeight"].toString(QStringLiteral("400")));
        const QJsonValue lineHeight = object["lineHeight"];
        if (lineHeight.isDouble())
            result.type.lineHeight = lineHeight.toDouble() * result.type.size;
        else if (const auto points = dimension(lineHeight))
            result.type.lineHeight = *points;
        if (const auto spacing = dimension(object["letterSpacing"]); spacing && result.type.size > 0)
            result.type.tracking = std::round(*spacing / result.type.size * 1000 * 100) / 100;
        return result;
    }
    case TokenKind::shadow: {
        const QJsonObject object = value.isArray() ? value.toArray().first().toObject() : value.toObject();
        if (value.isString()) {
            if (const auto shadow = ShadowValue::fromCss(value.toString())) {
                result.shadow = *shadow;
                return result;
            }
            return std::nullopt;
        }
        result.shadow.color = colourOf(object["color"]).value_or(QColor(0, 0, 0, 64));
        result.shadow.x = dimension(object["offsetX"]).value_or(0);
        result.shadow.y = dimension(object["offsetY"]).value_or(0);
        result.shadow.blur = dimension(object["blur"]).value_or(0);
        result.shadow.spread = dimension(object["spread"]).value_or(0);
        result.shadow.inset = object["inset"].toBool();
        return result;
    }
    }
    return std::nullopt;
}

std::optional<TokenKind> kindFor(const QString &type, const QString &path, const QJsonValue &value)
{
    if (type == QLatin1String("color"))
        return TokenKind::color;
    if (type == QLatin1String("typography"))
        return TokenKind::type;
    if (type == QLatin1String("shadow"))
        return TokenKind::shadow;
    const QString lower = path.toLower();
    const bool round = lower.contains(QLatin1String("radius")) || lower.contains(QLatin1String("rounded")) || lower.contains(QLatin1String("corner"));
    if (type == QLatin1String("dimension") || type == QLatin1String("spacing") || type == QLatin1String("sizing") || type == QLatin1String("borderRadius"))
        return round || type == QLatin1String("borderRadius") ? TokenKind::radius : TokenKind::spacing;
    if (!type.isEmpty())
        return std::nullopt;
    // Untyped: the value says.
    if (value.isString() && TokenFiles::parseColor(value.toString()))
        return TokenKind::color;
    if (dimension(value))
        return round ? TokenKind::radius : TokenKind::spacing;
    return std::nullopt;
}

void collect(const QJsonObject &group, const QString &path, const QString &inherited, std::map<QString, Raw> &raw)
{
    const QString type = group["$type"].toString(inherited);
    for (auto entry = group.begin(); entry != group.end(); ++entry) {
        if (entry.key().startsWith(QLatin1Char('$')) || !entry.value().isObject())
            continue;
        const QJsonObject child = entry.value().toObject();
        const QString name = path.isEmpty() ? entry.key() : path + QLatin1Char('/') + entry.key();
        if (child.contains("$value"))
            raw[name] = {child["$type"].toString(type), child["$value"],
                         child["$extensions"].toObject()[extension].toObject()["modes"].toObject()};
        else
            collect(child, name, type, raw);
    }
}

// "{color.brand.500}" → the value it names.
QJsonValue dereference(const QJsonValue &value, const std::map<QString, Raw> &raw, QString *type, int depth = 0)
{
    static const QRegularExpression alias(QStringLiteral(R"(^\{([^}]+)\}$)"));
    const auto match = alias.match(value.toString());
    if (!value.isString() || !match.hasMatch() || depth > 8)
        return value;
    const auto found = raw.find(match.captured(1).replace(QLatin1Char('.'), QLatin1Char('/')));
    if (found == raw.end())
        return QJsonValue();
    if (type && type->isEmpty())
        *type = found->second.type;
    return dereference(found->second.value, raw, type, depth + 1);
}

QJsonObject dimensionJson(double points, bool asString)
{
    Q_UNUSED(asString);
    return QJsonObject{{"value", std::round(points * 1000) / 1000}, {"unit", "px"}};
}

QJsonValue w3cValue(TokenKind kind, const TokenValue &value, bool strings)
{
    const auto length = [&](double points) -> QJsonValue {
        if (strings)
            return numeral(points) + QStringLiteral("px");
        return dimensionJson(points, false);
    };
    switch (kind) {
    case TokenKind::color: {
        if (strings)
            return TokenFiles::cssColor(value.color);
        QJsonObject colour{{"colorSpace", "srgb"},
                           {"components", QJsonArray{std::round(value.color.redF() * 10000) / 10000, std::round(value.color.greenF() * 10000) / 10000,
                                                     std::round(value.color.blueF() * 10000) / 10000}},
                           {"hex", value.color.name(QColor::HexRgb)}};
        if (value.color.alpha() != 255)
            colour["alpha"] = std::round(value.color.alphaF() * 1000) / 1000;
        return colour;
    }
    case TokenKind::spacing:
    case TokenKind::radius:
        return length(value.number);
    case TokenKind::type: {
        QJsonObject type{{"fontFamily", value.type.family}, {"fontSize", length(value.type.size)}, {"fontWeight", value.type.weight}};
        if (value.type.lineHeight && value.type.size > 0)
            type["lineHeight"] = std::round(*value.type.lineHeight / value.type.size * 1000) / 1000;
        if (value.type.tracking != 0)
            type["letterSpacing"] = length(value.type.tracking / 1000 * value.type.size);
        return type;
    }
    case TokenKind::shadow:
        return QJsonObject{{"color", strings ? QJsonValue(TokenFiles::cssColor(value.shadow.color)) : w3cValue(TokenKind::color, TokenValue{value.shadow.color, 0, {}, {}}, false)},
                           {"offsetX", length(value.shadow.x)}, {"offsetY", length(value.shadow.y)}, {"blur", length(value.shadow.blur)},
                           {"spread", length(value.shadow.spread)}, {"inset", value.shadow.inset}};
    }
    return {};
}

QString w3cType(TokenKind kind)
{
    switch (kind) {
    case TokenKind::color:
        return QStringLiteral("color");
    case TokenKind::spacing:
    case TokenKind::radius:
        return QStringLiteral("dimension");
    case TokenKind::type:
        return QStringLiteral("typography");
    case TokenKind::shadow:
        return QStringLiteral("shadow");
    }
    return {};
}

bool usesStrings(const QJsonObject &group)
{
    for (auto entry = group.begin(); entry != group.end(); ++entry) {
        if (!entry.value().isObject())
            continue;
        const QJsonObject child = entry.value().toObject();
        if (child.contains("$value")) {
            const QString type = child["$type"].toString();
            if ((type == QLatin1String("dimension") || type == QLatin1String("color")) && child["$value"].isString())
                return true;
            if ((type == QLatin1String("dimension") || type == QLatin1String("color")) && child["$value"].isObject())
                return false;
            continue;
        }
        if (usesStrings(child))
            return true;
    }
    return false;
}

QJsonObject placed(QJsonObject group, const QStringList &path, const QJsonObject &token)
{
    if (path.size() == 1) {
        QJsonObject existing = group[path.front()].toObject();
        for (auto key = token.begin(); key != token.end(); ++key)
            existing[key.key()] = key.value();
        group[path.front()] = existing;
        return group;
    }
    group[path.front()] = placed(group[path.front()].toObject(), path.mid(1), token);
    return group;
}

// A small reader for JavaScript object literals: anything that isn't a literal reads as null.
class Literal {
public:
    explicit Literal(const QString &text) : m_text(text) {}
    qsizetype at = 0;

    QJsonValue value()
    {
        skip();
        if (at >= m_text.size())
            return {};
        const QChar c = m_text[at];
        if (c == QLatin1Char('{'))
            return object();
        if (c == QLatin1Char('['))
            return array();
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`'))
            return string();
        if (c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('.')) {
            const qsizetype start = at;
            while (at < m_text.size() && (m_text[at].isDigit() || m_text[at] == QLatin1Char('.') || m_text[at] == QLatin1Char('-')))
                ++at;
            return m_text.mid(start, at - start).toDouble();
        }
        // An identifier, a call or a member: not a literal.
        const QString word = identifier();
        skip();
        while (at < m_text.size() && (m_text[at] == QLatin1Char('.') || m_text[at] == QLatin1Char('('))) {
            if (m_text[at] == QLatin1Char('(')) {
                balanced(QLatin1Char('('), QLatin1Char(')'));
            } else {
                ++at;
                identifier();
            }
            skip();
        }
        if (word == QLatin1String("true"))
            return true;
        if (word == QLatin1String("false"))
            return false;
        return {};
    }

private:
    void skip()
    {
        while (at < m_text.size()) {
            if (m_text[at].isSpace()) {
                ++at;
            } else if (m_text.mid(at, 2) == QLatin1String("//")) {
                const qsizetype end = m_text.indexOf(QLatin1Char('\n'), at);
                at = end < 0 ? m_text.size() : end;
            } else if (m_text.mid(at, 2) == QLatin1String("/*")) {
                const qsizetype end = m_text.indexOf(QStringLiteral("*/"), at);
                at = end < 0 ? m_text.size() : end + 2;
            } else {
                break;
            }
        }
    }

    QString identifier()
    {
        const qsizetype start = at;
        while (at < m_text.size() && (m_text[at].isLetterOrNumber() || m_text[at] == QLatin1Char('_') || m_text[at] == QLatin1Char('$')))
            ++at;
        if (at == start && at < m_text.size())
            ++at;
        return m_text.mid(start, at - start);
    }

    QString string()
    {
        const QChar quote = m_text[at++];
        QString result;
        while (at < m_text.size() && m_text[at] != quote) {
            if (m_text[at] == QLatin1Char('\\') && at + 1 < m_text.size())
                ++at;
            result += m_text[at++];
        }
        ++at;
        return result;
    }

    void balanced(QChar open, QChar close)
    {
        int depth = 0;
        for (; at < m_text.size(); ++at) {
            if (m_text[at] == open)
                ++depth;
            else if (m_text[at] == close && --depth == 0) {
                ++at;
                return;
            }
        }
    }

    QJsonObject object()
    {
        QJsonObject result;
        ++at;
        while (true) {
            skip();
            if (at >= m_text.size())
                return result;
            if (m_text[at] == QLatin1Char('}')) {
                ++at;
                return result;
            }
            if (m_text.mid(at, 3) == QLatin1String("...")) {
                at += 3;
                value();
            } else {
                QString key;
                if (m_text[at] == QLatin1Char('"') || m_text[at] == QLatin1Char('\''))
                    key = string();
                else if (m_text[at] == QLatin1Char('['))
                    balanced(QLatin1Char('['), QLatin1Char(']'));
                else
                    key = identifier();
                skip();
                if (at < m_text.size() && m_text[at] == QLatin1Char(':')) {
                    ++at;
                    const QJsonValue item = value();
                    if (!key.isEmpty())
                        result[key] = item;
                }
            }
            skip();
            if (at < m_text.size() && m_text[at] == QLatin1Char(','))
                ++at;
            else if (at < m_text.size() && m_text[at] != QLatin1Char('}'))
                ++at;
        }
    }

    QJsonArray array()
    {
        QJsonArray result;
        ++at;
        while (true) {
            skip();
            if (at >= m_text.size())
                return result;
            if (m_text[at] == QLatin1Char(']')) {
                ++at;
                return result;
            }
            result.append(value());
            skip();
            if (at < m_text.size() && m_text[at] == QLatin1Char(','))
                ++at;
            else if (at < m_text.size() && m_text[at] != QLatin1Char(']'))
                ++at;
        }
    }

    const QString m_text;
};

void flattenColours(const QJsonObject &colours, const QString &prefix, TokenFiles::Read &read)
{
    for (auto entry = colours.begin(); entry != colours.end(); ++entry) {
        const QString name = entry.key() == QLatin1String("DEFAULT") ? prefix : prefix.isEmpty() ? entry.key() : prefix + QLatin1Char('-') + entry.key();
        if (entry.value().isObject())
            flattenColours(entry.value().toObject(), name, read);
        else if (const auto colour = TokenFiles::parseColor(entry.value().toString()))
            read.tokens.push_back(DesignToken::color(QStringLiteral("color/") + name, *colour));
        else if (entry.value().isString())
            read.skipped.append(QStringLiteral("colors.") + name);
    }
}

void readTheme(const QJsonObject &theme, TokenFiles::Read &read)
{
    flattenColours(theme["colors"].toObject(), QString(), read);
    const auto numbers = [&](const char *key, TokenKind kind, const QString &prefix) {
        const QJsonObject group = theme[QLatin1String(key)].toObject();
        for (auto entry = group.begin(); entry != group.end(); ++entry) {
            const QString name = entry.key() == QLatin1String("DEFAULT") ? prefix : prefix + QLatin1Char('/') + entry.key();
            if (const auto points = TokenFiles::parseLength(entry.value().toString()))
                read.tokens.push_back(DesignToken::number(kind, name, *points));
        }
    };
    numbers("spacing", TokenKind::spacing, QStringLiteral("spacing"));
    numbers("borderRadius", TokenKind::radius, QStringLiteral("radius"));
    QString family = QStringLiteral("Sans Serif");
    const QJsonValue sans = theme["fontFamily"].toObject()["sans"];
    if (sans.isArray() && !sans.toArray().isEmpty())
        family = familyOf(sans.toArray().first().toString());
    else if (sans.isString())
        family = familyOf(sans.toString());
    const QJsonObject sizes = theme["fontSize"].toObject();
    for (auto entry = sizes.begin(); entry != sizes.end(); ++entry) {
        TypeValue type;
        type.family = family;
        const QJsonValue value = entry.value();
        const QString size = value.isArray() ? value.toArray().at(0).toString() : value.toString();
        const auto points = TokenFiles::parseLength(size);
        if (!points)
            continue;
        type.size = *points;
        if (value.isArray() && value.toArray().size() > 1) {
            const QJsonValue extra = value.toArray().at(1);
            const QString lineHeight = extra.isObject() ? extra.toObject()["lineHeight"].toString() : extra.toString();
            if (const auto line = TokenFiles::parseLength(lineHeight); line && !lineHeight.trimmed().contains(QRegularExpression(QStringLiteral("^[0-9.]+$"))))
                type.lineHeight = *line;
            else if (lineHeight.toDouble() > 0)
                type.lineHeight = lineHeight.toDouble() * type.size;
            if (extra.isObject() && extra.toObject().contains("fontWeight"))
                type.weight = weightOf(extra.toObject()["fontWeight"].toVariant().toString());
        }
        read.tokens.push_back(DesignToken::typography(QStringLiteral("text/") + entry.key(), type));
    }
    const QJsonObject shadows = theme["boxShadow"].toObject();
    for (auto entry = shadows.begin(); entry != shadows.end(); ++entry) {
        if (const auto shadow = ShadowValue::fromCss(entry.value().toString()))
            read.tokens.push_back(DesignToken::shadowToken(entry.key() == QLatin1String("DEFAULT") ? QStringLiteral("shadow") : QStringLiteral("shadow/") + entry.key(), *shadow));
    }
}
}

namespace TokenFiles {
Read readW3c(const QByteArray &json)
{
    Read read;
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        read.skipped.append(QStringLiteral("the file isn't JSON"));
        return read;
    }
    std::map<QString, Raw> raw;
    collect(document.object(), QString(), QString(), raw);
    for (const auto &[name, entry] : raw) {
        QString type = entry.type;
        const QJsonValue value = dereference(entry.value, raw, &type);
        const auto kind = kindFor(type, name, value);
        const auto parsed = kind ? valueOf(*kind, value) : std::nullopt;
        if (!parsed) {
            read.skipped.append(name);
            continue;
        }
        DesignToken token{DesignTokens::newId(), name, *kind, *parsed, {}, {}};
        for (auto mode = entry.modes.begin(); mode != entry.modes.end(); ++mode) {
            if (const auto other = valueOf(*kind, dereference(mode.value(), raw, nullptr))) {
                token.modes[mode.key()] = *other;
                if (read.modes.isEmpty())
                    read.modes.append(QStringLiteral("light"));
                if (!read.modes.contains(mode.key()))
                    read.modes.append(mode.key());
            }
        }
        read.tokens.push_back(token);
    }
    return read;
}

QByteArray writeW3c(const std::vector<DesignToken> &tokens, const QByteArray &existing)
{
    QJsonObject root = QJsonDocument::fromJson(existing).object();
    const bool strings = !root.isEmpty() && usesStrings(root);
    for (const DesignToken &token : tokens) {
        QJsonObject entry{{"$type", w3cType(token.kind)}, {"$value", w3cValue(token.kind, token.value, strings)}};
        if (!token.description.isEmpty())
            entry["$description"] = token.description;
        if (!token.modes.empty()) {
            QJsonObject modes;
            for (const auto &[mode, value] : token.modes)
                modes[mode] = w3cValue(token.kind, value, strings);
            entry["$extensions"] = QJsonObject{{extension, QJsonObject{{"modes", modes}}}};
        }
        root = placed(root, token.name.split(QLatin1Char('/'), Qt::SkipEmptyParts), entry);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Read readTailwindConfig(const QByteArray &javascript)
{
    Read read;
    const QString text = QString::fromUtf8(javascript);
    static const QRegularExpression themeKey(QStringLiteral(R"(\btheme\s*:\s*\{)"));
    const auto match = themeKey.match(text);
    if (!match.hasMatch()) {
        read.skipped.append(QStringLiteral("no theme in the config"));
        return read;
    }
    Literal literal(text);
    literal.at = match.capturedEnd() - 1;
    QJsonObject theme = literal.value().toObject();
    QJsonObject extend = theme["extend"].toObject();
    // Sizes in `extend` take the theme's own family.
    if (!extend.contains("fontFamily"))
        extend["fontFamily"] = theme["fontFamily"];
    theme.remove("extend");
    readTheme(theme, read);
    Read extended;
    readTheme(extend, extended);
    DesignTokens::merge(read.tokens, extended.tokens);
    read.skipped += extended.skipped;
    return read;
}
}
