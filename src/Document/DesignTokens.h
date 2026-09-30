#pragma once
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <map>
#include <optional>
#include <vector>

struct VectorDocument;
struct CharacterFormat;
struct ParagraphFormat;

// A design system's named values (docs/DESIGN-SYSTEMS.md). Paints, text styles,
// corner radii, stroke weights and group gaps can point at one by id, and
// changing the token changes every use.
// Duration is milliseconds (a stagger is a duration in a `stagger` group), easing is CSS text: cubic-bezier(), a keyword or linear().
enum class TokenKind { color, type, spacing, radius, shadow, duration, easing };
QString rawValue(TokenKind kind);
std::optional<TokenKind> tokenKind(const QString &rawValue);
// "Colour", "Type" … for the panel's headings.
QString title(TokenKind kind);

// A type token: sizes in points (a CSS pixel), tracking in 1/1000 em.
struct TypeValue {
    QString family = QStringLiteral("Sans Serif");
    int weight = 400;
    double size = 16;
    // Baseline to baseline in points; nullopt is Auto.
    std::optional<double> lineHeight;
    double tracking = 0;
    friend bool operator==(const TypeValue &, const TypeValue &) = default;
};

struct ShadowValue {
    QColor color = QColor(0, 0, 0, 64);
    double x = 0;
    double y = 2;
    double blur = 4;
    double spread = 0;
    bool inset = false;
    // "0px 2px 4px 0px rgba(0, 0, 0, 0.25)".
    QString css() const;
    static std::optional<ShadowValue> fromCss(const QString &css);
    friend bool operator==(const ShadowValue &, const ShadowValue &) = default;
};

// What a token holds: the field its kind uses. Spacing and radius are `number`, in points; a duration is `number`, in ms.
struct TokenValue {
    QColor color;
    double number = 0;
    TypeValue type;
    ShadowValue shadow;
    // An easing: "cubic-bezier(0.16, 1, 0.3, 1)", "ease-out" or "linear(0, 0.5, 1)".
    QString text;
    friend bool operator==(const TokenValue &, const TokenValue &) = default;
};

struct DesignToken {
    QString id;
    // Slash-separated groups: "color/brand/500", "space/4", "text/body".
    QString name;
    TokenKind kind = TokenKind::color;
    // The first mode's value, and the only one without modes.
    TokenValue value;
    // Values for the other modes, by mode name; a mode left out uses `value`.
    std::map<QString, TokenValue> modes;
    QString description;

    // A colour, "12 pt", "Inter 16/24 600" or a CSS shadow, for lists.
    QString displayValue(const QString &mode = {}) const;
    const TokenValue &valueIn(const QString &mode) const;
    friend bool operator==(const DesignToken &, const DesignToken &) = default;

    static DesignToken color(const QString &name, const QColor &color);
    static DesignToken number(TokenKind kind, const QString &name, double value);
    static DesignToken typography(const QString &name, const TypeValue &type);
    static DesignToken shadowToken(const QString &name, const ShadowValue &shadow);
    static DesignToken easing(const QString &name, const QString &text);
};

// The keys objects bind scalar properties to tokens under (VectorObject::tokenRefs).
namespace TokenRef {
inline const QString strokeWidth = QStringLiteral("strokeWidth");
inline const QString radius = QStringLiteral("radius");
inline const QString type = QStringLiteral("type");
// A group's children spaced by a spacing token, left to right or top to bottom.
inline const QString gapX = QStringLiteral("gapX");
inline const QString gapY = QStringLiteral("gapY");
}

namespace DesignTokens {
// Makes a new token id.
QString newId();
// Resolves every reference in `document` to its token's value in the document's
// mode: paints (and component overrides), stroke weights, live corners, type,
// text styles and gaps. References to tokens that no longer exist are dropped.
void apply(VectorDocument &document);
// References whose value was changed by hand since: they no longer follow their token.
void dropStale(VectorDocument &document);
// The format a type token gives characters and paragraphs.
void applyType(const TypeValue &type, CharacterFormat &character, ParagraphFormat &paragraph);
bool matchesType(const TypeValue &type, const CharacterFormat &character, const ParagraphFormat &paragraph);

// Tokens in Omastrator's own JSON (documents and libraries).
QJsonObject encode(const DesignToken &token);
std::optional<DesignToken> decode(const QJsonObject &json);
QJsonArray encode(const std::vector<DesignToken> &tokens);
std::vector<DesignToken> decode(const QJsonArray &json);

// Merges `incoming` into `into` by name: a name already there takes the new value and keeps its id (so
// every use follows), a new name is added. Returns how many changed or were added.
int merge(std::vector<DesignToken> &into, const std::vector<DesignToken> &incoming);
const DesignToken *find(const std::vector<DesignToken> &tokens, const QString &id);
const DesignToken *named(const std::vector<DesignToken> &tokens, const QString &name);
}
