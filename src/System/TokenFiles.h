#pragma once
#include "Document/DesignTokens.h"
#include <QByteArray>
#include <QColor>
#include <QString>
#include <array>
#include <optional>
#include <vector>

// Design tokens in a project's own files (docs/DESIGN-SYSTEMS.md): W3C design
// tokens (tokens.json), Tailwind v4's @theme, a Tailwind v3 config (read only)
// and plain CSS custom properties. Writers change values in place and keep
// everything else in the file as it was.
namespace TokenFiles {
struct Read {
    std::vector<DesignToken> tokens;
    // Modes found beside the base values ("dark").
    QStringList modes;
    // What couldn't be read, for the dialog.
    QStringList skipped;
};

// W3C Design Tokens: nested groups, `$value`/`$type` (inherited from groups), aliases in braces,
// string or object colours and dimensions, typography and shadow composites. Modes live in
// `$extensions["io.github.iretonsean.omastrator"].modes`.
Read readW3c(const QByteArray &json);
// Merged into `existing` (kept key order and extra keys; new files use the 2025 object forms).
QByteArray writeW3c(const std::vector<DesignToken> &tokens, const QByteArray &existing = {});

// Tailwind v4: `@theme { --color-*, --spacing(-*), --radius-*, --shadow-*, --text-*(--line-height…), --font-* }`.
Read readTailwind(const QByteArray &css);
QByteArray writeTailwind(const std::vector<DesignToken> &tokens, const QByteArray &existing = {});
// Tailwind v3: `theme` and `theme.extend` of tailwind.config.js, as far as they are literals.
Read readTailwindConfig(const QByteArray &javascript);

// CSS custom properties in `:root`, with a dark mode from `.dark`, `[data-theme="dark"]` or
// `@media (prefers-color-scheme: dark)`.
Read readCss(const QByteArray &css);
// `modes`: the document's modes; values for a second mode go in the dark block.
QByteArray writeCss(const std::vector<DesignToken> &tokens, const QByteArray &existing = {}, const QStringList &modes = {});

// "--color-brand-500" for "color/brand/500".
QString cssVariable(const DesignToken &token);
// A CSS colour: hex, rgb(), hsl(), oklch(), oklab() and names.
std::optional<QColor> parseColor(const QString &css);
QString cssColor(const QColor &color);
// "16px", "1rem", "0.5em", "12" → points; nullopt for anything else.
std::optional<double> parseLength(const QString &css);
// "480ms", "0.48s" → milliseconds; nullopt for anything else (a bare number has no unit, so it is not a time).
std::optional<double> parseTime(const QString &css);
// Whether `css` reads as an easing: cubic-bezier(), steps(), linear(), or a keyword such as ease-out.
bool isEasing(const QString &css);
// "cubic-bezier(0.16, 1, 0.3, 1)" as its four numbers, or nothing for a keyword and the rest.
std::optional<std::array<double, 4>> cubicBezier(const QString &css);
QString cubicBezierText(const std::array<double, 4> &points);
}
