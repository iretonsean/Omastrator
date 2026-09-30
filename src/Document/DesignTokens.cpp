#include "Document/DesignTokens.h"
#include "Document/EditorSession.h"
#include "Document/VectorDocument.h"
#include <QFontDatabase>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
const std::array<std::pair<TokenKind, const char *>, 7> kindNames{{
    {TokenKind::color, "color"}, {TokenKind::type, "type"}, {TokenKind::spacing, "spacing"},
    {TokenKind::radius, "radius"}, {TokenKind::shadow, "shadow"}, {TokenKind::duration, "duration"}, {TokenKind::easing, "easing"},
}};

QString hex(const QColor &color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QString numeral(double value)
{
    return QString::number(std::round(value * 1000) / 1000);
}

bool near(double a, double b)
{
    return std::abs(a - b) < 0.001;
}

const DesignToken *tokenOf(const VectorDocument &document, const QString &id, TokenKind kind)
{
    const DesignToken *token = DesignTokens::find(document.tokens, id);
    return token && token->kind == kind ? token : nullptr;
}

void paintFromToken(Paint &paint, const VectorDocument &document)
{
    if (paint.token.isEmpty())
        return;
    const DesignToken *token = tokenOf(document, paint.token, TokenKind::color);
    if (!token) {
        paint.token.clear();
        return;
    }
    paint.kind = PaintKind::solid;
    paint.color = token->valueIn(document.tokenMode).color;
}

bool paintStale(const Paint &paint, const VectorDocument &document)
{
    if (paint.token.isEmpty())
        return false;
    const DesignToken *token = tokenOf(document, paint.token, TokenKind::color);
    return !token || paint.kind != PaintKind::solid || paint.color != token->valueIn(document.tokenMode).color;
}

// Children of `group`, spaced `gap` apart along the axis from the first one, in their order on it.
void spaceChildren(VectorDocument &document, const QUuid &group, double gap, bool horizontal)
{
    std::vector<std::pair<QUuid, QRectF>> children;
    for (const QUuid &child : document.children(group))
        children.emplace_back(child, document.bounds(child));
    std::stable_sort(children.begin(), children.end(), [&](const auto &a, const auto &b) {
        return horizontal ? a.second.left() < b.second.left() : a.second.top() < b.second.top();
    });
    if (children.size() < 2)
        return;
    double next = horizontal ? children.front().second.right() + gap : children.front().second.bottom() + gap;
    for (size_t index = 1; index < children.size(); ++index) {
        const QRectF bounds = children[index].second;
        const double offset = next - (horizontal ? bounds.left() : bounds.top());
        if (!near(offset, 0))
            document.transform(children[index].first, horizontal ? QTransform::fromTranslate(offset, 0) : QTransform::fromTranslate(0, offset));
        next += (horizontal ? bounds.width() : bounds.height()) + gap;
    }
}

bool gapHolds(const VectorDocument &document, const QUuid &group, double gap, bool horizontal)
{
    std::vector<QRectF> boxes;
    for (const QUuid &child : document.children(group))
        boxes.push_back(document.bounds(child));
    std::stable_sort(boxes.begin(), boxes.end(), [&](const QRectF &a, const QRectF &b) { return horizontal ? a.left() < b.left() : a.top() < b.top(); });
    for (size_t index = 1; index < boxes.size(); ++index) {
        const double actual = horizontal ? boxes[index].left() - boxes[index - 1].right() : boxes[index].top() - boxes[index - 1].bottom();
        if (std::abs(actual - gap) > 0.01)
            return false;
    }
    return true;
}

QJsonObject encodeValue(TokenKind kind, const TokenValue &value)
{
    switch (kind) {
    case TokenKind::color:
        return {{"color", value.color.name(QColor::HexArgb)}};
    case TokenKind::spacing:
    case TokenKind::radius:
    case TokenKind::duration:
        return {{"number", value.number}};
    case TokenKind::easing:
        return {{"text", value.text}};
    case TokenKind::type: {
        QJsonObject type{{"family", value.type.family}, {"weight", value.type.weight}, {"size", value.type.size},
                         {"tracking", value.type.tracking}};
        if (value.type.lineHeight)
            type["lineHeight"] = *value.type.lineHeight;
        return {{"type", type}};
    }
    case TokenKind::shadow:
        return {{"shadow", QJsonObject{{"color", value.shadow.color.name(QColor::HexArgb)}, {"x", value.shadow.x}, {"y", value.shadow.y},
                                       {"blur", value.shadow.blur}, {"spread", value.shadow.spread}, {"inset", value.shadow.inset}}}};
    }
    return {};
}

TokenValue decodeValue(const QJsonObject &json)
{
    TokenValue value;
    value.color = QColor::fromString(json["color"].toString());
    value.number = json["number"].toDouble();
    value.text = json["text"].toString();
    const QJsonObject type = json["type"].toObject();
    value.type.family = type["family"].toString(value.type.family);
    value.type.weight = std::clamp(type["weight"].toInt(400), 1, 1000);
    value.type.size = std::max(0.1, type["size"].toDouble(16));
    if (type.contains("lineHeight"))
        value.type.lineHeight = type["lineHeight"].toDouble();
    value.type.tracking = type["tracking"].toDouble();
    const QJsonObject shadow = json["shadow"].toObject();
    if (!shadow.isEmpty()) {
        value.shadow.color = QColor::fromString(shadow["color"].toString());
        value.shadow.x = shadow["x"].toDouble();
        value.shadow.y = shadow["y"].toDouble();
        value.shadow.blur = shadow["blur"].toDouble();
        value.shadow.spread = shadow["spread"].toDouble();
        value.shadow.inset = shadow["inset"].toBool();
    }
    return value;
}
}

QString rawValue(TokenKind kind)
{
    for (const auto &[each, name] : kindNames) {
        if (each == kind)
            return QLatin1String(name);
    }
    return {};
}

std::optional<TokenKind> tokenKind(const QString &raw)
{
    for (const auto &[each, name] : kindNames) {
        if (raw == QLatin1String(name))
            return each;
    }
    return std::nullopt;
}

QString title(TokenKind kind)
{
    switch (kind) {
    case TokenKind::color:
        return QStringLiteral("Colour");
    case TokenKind::type:
        return QStringLiteral("Type");
    case TokenKind::spacing:
        return QStringLiteral("Spacing");
    case TokenKind::radius:
        return QStringLiteral("Radius");
    case TokenKind::shadow:
        return QStringLiteral("Shadow");
    case TokenKind::duration:
        return QStringLiteral("Duration");
    case TokenKind::easing:
        return QStringLiteral("Easing");
    }
    return {};
}

QString ShadowValue::css() const
{
    const QString colour = color.alpha() == 255 ? color.name(QColor::HexRgb)
                                                : QStringLiteral("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(numeral(color.alphaF()));
    return QStringLiteral("%1%2px %3px %4px %5px %6")
        .arg(inset ? QStringLiteral("inset ") : QString(), numeral(x), numeral(y), numeral(blur), numeral(spread), colour);
}

std::optional<ShadowValue> ShadowValue::fromCss(const QString &css)
{
    QString text = css.trimmed();
    if (text.isEmpty() || text == QLatin1String("none"))
        return std::nullopt;
    // Only the first of several shadows.
    int depth = 0;
    for (qsizetype at = 0; at < text.size(); ++at) {
        if (text[at] == QLatin1Char('('))
            ++depth;
        else if (text[at] == QLatin1Char(')'))
            --depth;
        else if (text[at] == QLatin1Char(',') && depth == 0) {
            text = text.left(at).trimmed();
            break;
        }
    }
    ShadowValue shadow;
    static const QRegularExpression colourPattern(QStringLiteral(R"((rgba?\([^)]*\)|hsla?\([^)]*\)|#[0-9a-fA-F]{3,8}\b|\b[a-z]+\b))"));
    static const QRegularExpression lengthPattern(QStringLiteral(R"((-?[0-9]*\.?[0-9]+)(px|rem|em)?)"));
    if (text.startsWith(QLatin1String("inset"))) {
        shadow.inset = true;
        text = text.mid(5).trimmed();
    } else if (text.endsWith(QLatin1String(" inset"))) {
        shadow.inset = true;
        text.chop(6);
    }
    QString rest = text;
    for (auto match = colourPattern.globalMatch(text); match.hasNext();) {
        const auto found = match.next();
        const QString candidate = found.captured(1);
        if (candidate == QLatin1String("px") || candidate == QLatin1String("rem") || candidate == QLatin1String("em"))
            continue;
        QColor parsed = QColor::fromString(candidate);
        if (!parsed.isValid() && candidate.startsWith(QLatin1String("rgb"))) {
            const QStringList parts = candidate.mid(candidate.indexOf(QLatin1Char('(')) + 1).chopped(1).split(QRegularExpression(QStringLiteral("[,\\s/]+")), Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                parsed = QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
                if (parts.size() > 3)
                    parsed.setAlphaF(parts[3].endsWith(QLatin1Char('%')) ? parts[3].chopped(1).toDouble() / 100 : parts[3].toDouble());
            }
        }
        if (parsed.isValid()) {
            shadow.color = parsed;
            rest.remove(candidate);
            break;
        }
    }
    std::vector<double> lengths;
    for (auto match = lengthPattern.globalMatch(rest); match.hasNext();) {
        const auto found = match.next();
        const double value = found.captured(1).toDouble() * (found.captured(2) == QLatin1String("rem") || found.captured(2) == QLatin1String("em") ? 16 : 1);
        lengths.push_back(value);
    }
    if (lengths.size() < 2)
        return std::nullopt;
    shadow.x = lengths[0];
    shadow.y = lengths[1];
    shadow.blur = lengths.size() > 2 ? lengths[2] : 0;
    shadow.spread = lengths.size() > 3 ? lengths[3] : 0;
    return shadow;
}

QString DesignToken::displayValue(const QString &mode) const
{
    const TokenValue &shown = valueIn(mode);
    switch (kind) {
    case TokenKind::color:
        return hex(shown.color);
    case TokenKind::spacing:
    case TokenKind::radius:
        return numeral(shown.number) + QStringLiteral(" pt");
    case TokenKind::type: {
        QString text = shown.type.family + QLatin1Char(' ') + numeral(shown.type.size);
        if (shown.type.lineHeight)
            text += QLatin1Char('/') + numeral(*shown.type.lineHeight);
        return text + QLatin1Char(' ') + QString::number(shown.type.weight);
    }
    case TokenKind::shadow:
        return shown.shadow.css();
    case TokenKind::duration:
        return numeral(shown.number) + QStringLiteral(" ms");
    case TokenKind::easing:
        // linear() is kept as written and is not read as a curve.
        return shown.text.startsWith(QLatin1String("linear(")) ? QStringLiteral("Custom") : shown.text;
    }
    return {};
}

const TokenValue &DesignToken::valueIn(const QString &mode) const
{
    if (!mode.isEmpty()) {
        if (const auto found = modes.find(mode); found != modes.end())
            return found->second;
    }
    return value;
}

DesignToken DesignToken::color(const QString &name, const QColor &color)
{
    DesignToken token{DesignTokens::newId(), name, TokenKind::color, {}, {}, {}};
    token.value.color = color;
    return token;
}

DesignToken DesignToken::number(TokenKind kind, const QString &name, double value)
{
    DesignToken token{DesignTokens::newId(), name, kind, {}, {}, {}};
    token.value.number = value;
    return token;
}

DesignToken DesignToken::typography(const QString &name, const TypeValue &type)
{
    DesignToken token{DesignTokens::newId(), name, TokenKind::type, {}, {}, {}};
    token.value.type = type;
    return token;
}

DesignToken DesignToken::easing(const QString &name, const QString &text)
{
    DesignToken token{DesignTokens::newId(), name, TokenKind::easing, {}, {}, {}};
    token.value.text = text.trimmed();
    return token;
}

DesignToken DesignToken::shadowToken(const QString &name, const ShadowValue &shadow)
{
    DesignToken token{DesignTokens::newId(), name, TokenKind::shadow, {}, {}, {}};
    token.value.shadow = shadow;
    return token;
}

namespace DesignTokens {
QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

const DesignToken *find(const std::vector<DesignToken> &tokens, const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    const auto found = std::find_if(tokens.begin(), tokens.end(), [&](const DesignToken &token) { return token.id == id; });
    return found == tokens.end() ? nullptr : &*found;
}

const DesignToken *named(const std::vector<DesignToken> &tokens, const QString &name)
{
    const auto found = std::find_if(tokens.begin(), tokens.end(), [&](const DesignToken &token) { return token.name == name; });
    return found == tokens.end() ? nullptr : &*found;
}

void applyType(const TypeValue &type, CharacterFormat &character, ParagraphFormat &paragraph)
{
    character.family = type.family;
    const bool italic = character.isItalic();
    if (QFontDatabase::weight(character.family, character.style) != type.weight || !QFontDatabase::families().contains(type.family))
        character.style = TextContent::styleFor(type.family, type.weight, italic);
    character.size = type.size;
    character.tracking = type.tracking;
    paragraph.leading = type.lineHeight;
}

bool matchesType(const TypeValue &type, const CharacterFormat &character, const ParagraphFormat &paragraph)
{
    return character.family == type.family && near(character.size, type.size) && near(character.tracking, type.tracking)
        && paragraph.leading.has_value() == type.lineHeight.has_value() && (!type.lineHeight || near(*paragraph.leading, *type.lineHeight));
}

void apply(VectorDocument &document)
{
    for (VectorObject &object : document.objects) {
        if (object.hasPaint()) {
            paintFromToken(object.fill, document);
            paintFromToken(object.stroke.paint, document);
            for (Paint &paint : object.extraFills)
                paintFromToken(paint, document);
            for (StrokeStyle &stroke : object.extraStrokes)
                paintFromToken(stroke.paint, document);
        }
        if (object.instance) {
            for (auto &[key, change] : object.instance->overrides) {
                if (change.fill)
                    paintFromToken(*change.fill, document);
                if (change.stroke)
                    paintFromToken(*change.stroke, document);
            }
        }
        for (auto ref = object.tokenRefs.begin(); ref != object.tokenRefs.end();) {
            const QString &key = ref->first;
            const TokenKind wanted = key == TokenRef::type ? TokenKind::type
                : key == TokenRef::radius                   ? TokenKind::radius
                : key == TokenRef::strokeWidth              ? TokenKind::spacing
                                                            : TokenKind::spacing;
            const DesignToken *token = DesignTokens::find(document.tokens, ref->second);
            // A stroke weight may follow a spacing or radius token; anything else wants its kind.
            if (!token || (token->kind != wanted && !(key == TokenRef::strokeWidth && token->kind == TokenKind::radius))) {
                ref = object.tokenRefs.erase(ref);
                continue;
            }
            const TokenValue &value = token->valueIn(document.tokenMode);
            if (key == TokenRef::strokeWidth && object.hasPaint()) {
                object.stroke.width = std::max(0.0, value.number);
            } else if (key == TokenRef::radius && object.shape) {
                LiveRectangle shape = *object.shape;
                shape.radii.fill(std::max(0.0, value.number));
                EditorSession::reshape(object, shape);
            } else if (key == TokenRef::type && object.kind == ObjectKind::text) {
                applyType(value.type, object.text.character(), object.text.paragraph());
                object.text.runs.clear();
                object.text.paragraphFormats.clear();
            }
            ++ref;
        }
    }
    // Gaps last: sizes above may have changed.
    for (const VectorObject &object : std::vector<VectorObject>(document.objects)) {
        for (const QString &key : {TokenRef::gapX, TokenRef::gapY}) {
            const auto ref = object.tokenRefs.find(key);
            if (ref == object.tokenRefs.end())
                continue;
            if (const DesignToken *token = tokenOf(document, ref->second, TokenKind::spacing))
                spaceChildren(document, object.id, token->valueIn(document.tokenMode).number, key == TokenRef::gapX);
        }
    }
    for (TextStyle &style : document.textStyles) {
        if (style.typeToken.isEmpty())
            continue;
        const DesignToken *token = tokenOf(document, style.typeToken, TokenKind::type);
        if (!token) {
            style.typeToken.clear();
            continue;
        }
        TextStyle fresh = style;
        applyType(token->valueIn(document.tokenMode).type, fresh.character, fresh.paragraph);
        if (style.kind == TextStyleKind::character)
            fresh.paragraph = style.paragraph;
        if (fresh == style)
            continue;
        for (VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::text)
                restyleText(object.text, style, fresh);
        }
        style = fresh;
    }
}

void dropStale(VectorDocument &document)
{
    for (VectorObject &object : document.objects) {
        if (object.hasPaint()) {
            const auto drop = [&](Paint &paint) {
                if (paintStale(paint, document))
                    paint.token.clear();
            };
            drop(object.fill);
            drop(object.stroke.paint);
            for (Paint &paint : object.extraFills)
                drop(paint);
            for (StrokeStyle &stroke : object.extraStrokes)
                drop(stroke.paint);
        }
        for (auto ref = object.tokenRefs.begin(); ref != object.tokenRefs.end();) {
            const DesignToken *token = DesignTokens::find(document.tokens, ref->second);
            bool stale = !token;
            if (token) {
                const TokenValue &value = token->valueIn(document.tokenMode);
                if (ref->first == TokenRef::strokeWidth)
                    stale = !object.hasPaint() || !near(object.stroke.width, value.number);
                else if (ref->first == TokenRef::radius)
                    stale = !object.liveShape() || std::any_of(object.shape->radii.begin(), object.shape->radii.end(), [&](double radius) { return !near(radius, value.number); });
                else if (ref->first == TokenRef::type)
                    stale = object.kind != ObjectKind::text || !object.text.runs.empty() || !matchesType(value.type, object.text.character(), object.text.paragraph());
                else if (ref->first == TokenRef::gapX || ref->first == TokenRef::gapY)
                    stale = !gapHolds(document, object.id, value.number, ref->first == TokenRef::gapX);
            }
            ref = stale ? object.tokenRefs.erase(ref) : std::next(ref);
        }
    }
    for (TextStyle &style : document.textStyles) {
        if (style.typeToken.isEmpty())
            continue;
        const DesignToken *token = tokenOf(document, style.typeToken, TokenKind::type);
        ParagraphFormat paragraph = style.paragraph;
        if (token && style.kind == TextStyleKind::character)
            paragraph.leading = token->valueIn(document.tokenMode).type.lineHeight;
        if (!token || !matchesType(token->valueIn(document.tokenMode).type, style.character, paragraph))
            style.typeToken.clear();
    }
}

QJsonObject encode(const DesignToken &token)
{
    QJsonObject json{{"id", token.id}, {"name", token.name}, {"kind", rawValue(token.kind)}, {"value", encodeValue(token.kind, token.value)}};
    if (!token.modes.empty()) {
        QJsonObject modes;
        for (const auto &[mode, value] : token.modes)
            modes[mode] = encodeValue(token.kind, value);
        json["modes"] = modes;
    }
    if (!token.description.isEmpty())
        json["description"] = token.description;
    return json;
}

std::optional<DesignToken> decode(const QJsonObject &json)
{
    const auto kind = tokenKind(json["kind"].toString());
    const QString name = json["name"].toString().trimmed();
    if (!kind || name.isEmpty())
        return std::nullopt;
    DesignToken token;
    token.id = json["id"].toString();
    if (token.id.isEmpty())
        token.id = newId();
    token.name = name;
    token.kind = *kind;
    token.value = decodeValue(json["value"].toObject());
    const QJsonObject modes = json["modes"].toObject();
    for (auto mode = modes.begin(); mode != modes.end(); ++mode)
        token.modes[mode.key()] = decodeValue(mode.value().toObject());
    token.description = json["description"].toString();
    return token;
}

QJsonArray encode(const std::vector<DesignToken> &tokens)
{
    QJsonArray array;
    for (const DesignToken &token : tokens)
        array.append(encode(token));
    return array;
}

std::vector<DesignToken> decode(const QJsonArray &json)
{
    std::vector<DesignToken> tokens;
    for (const QJsonValue &value : json) {
        if (auto token = decode(value.toObject()))
            tokens.push_back(std::move(*token));
    }
    return tokens;
}

int merge(std::vector<DesignToken> &into, const std::vector<DesignToken> &incoming)
{
    int changed = 0;
    for (const DesignToken &token : incoming) {
        auto found = std::find_if(into.begin(), into.end(), [&](const DesignToken &each) { return each.name == token.name && each.kind == token.kind; });
        if (found == into.end()) {
            DesignToken added = token;
            if (added.id.isEmpty() || find(into, added.id))
                added.id = newId();
            into.push_back(added);
            ++changed;
        } else if (found->value != token.value || found->modes != token.modes || (!token.description.isEmpty() && found->description != token.description)) {
            found->value = token.value;
            found->modes = token.modes;
            if (!token.description.isEmpty())
                found->description = token.description;
            ++changed;
        }
    }
    return changed;
}
}
