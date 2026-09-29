#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

// The widths a Browser View offers as buttons (docs/BROWSER-VIEW.md, section 7), read from the page's own stylesheets.
namespace Breakpoints {
// What a site that isn't yours, or one with no media queries, gets.
QList<int> defaults();
// A script for Runtime.evaluate that answers a JSON string for fromScan: the width media queries of every readable
// stylesheet, the Tailwind --breakpoint-* variables, and the root font size.
QString scanScript();
// `scan` is {rootFontSize, media: ["(min-width: 768px)"], vars: {"--breakpoint-xl": "80rem"}}. At most five widths, 320 to 2560,
// the most used ones, ascending; the defaults when it holds none.
QList<int> fromScan(const QJsonObject &scan);
}
