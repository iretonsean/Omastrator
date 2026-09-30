#pragma once
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <utility>
#include <vector>

// A page's design tokens, which Live edits snap to (docs/OS-SUITE.md): the
// Tailwind theme when the page has one, then the CSS custom properties in its
// stylesheets, then the Omarchy theme's colours.
struct Token {
    // duration, easing and stagger are motion's (docs/MOTION.md, section 6): a duration and a stagger are in ms, an easing is CSS text.
    enum class Group { color, spacing, fontSize, fontWeight, radius, duration, easing, stagger };
    enum class Source { tailwind, css, omarchy };
    Group group;
    Source source;
    // "--color-sky-500", "p-3", "Omarchy accent".
    QString name;
    // What the page's CSS says, as the page resolved it: "#0ea5e9", "12px", "700".
    QString value;
    // The Tailwind class suffix ("sky-500", "3", "lg"), empty for other sources.
    QString suffix;
    QColor color;
    double number = 0;
};

class TokenSet {
public:
    // `scan` is what the overlay's scan() returns: {vars: {name: {kind, value, rgb?, px?}}, rootFontSize}.
    static TokenSet fromScan(const QJsonObject &scan, const std::vector<std::pair<QString, QColor>> &omarchy = {});

    struct Resolution {
        QString property;
        // The CSS value to apply: "12px", "#0ea5e9".
        QString value;
        // The token snapped to, or empty when the value is kept as given.
        QString token;
        // A Tailwind class swap on the element, when it had a class for this property.
        QString removeClass;
        QString addClass;
    };
    // Snaps `value` for `property` on an element with `classes`.
    Resolution resolve(const QString &property, const QString &value, const QStringList &classes) const;

    const std::vector<Token> &tokens() const { return m_tokens; }
    bool hasTailwind() const { return m_spacingPx > 0 || m_tailwind; }
    double spacingPx() const { return m_spacingPx; }
    // For the overlay's chips: {colors: [{name, value}], spacing: [...], fontSizes, fontWeights, radii, durations, easings, staggers}.
    QJsonObject toJson() const;

    static std::optional<Token::Group> groupOf(const QString &property);
    // Tailwind's class prefix for `property`: "p-", "bg-", "rounded-".
    static QString tailwindPrefix(const QString &property);
    // "12px", "0.75rem" (with the root font size), "3" → pixels.
    static std::optional<double> pixels(const QString &value, double rootFontSize = 16);
    // "480ms", "0.48s" → milliseconds.
    static std::optional<double> milliseconds(const QString &value);

private:
    std::vector<Token> m_tokens;
    double m_spacingPx = 0;
    bool m_tailwind = false;
    double m_rootFontSize = 16;
};
