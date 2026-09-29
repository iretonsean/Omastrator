#include "Live/Breakpoints.h"
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {
constexpr int lowest = 320;
constexpr int highest = 2560;
constexpr int most = 5;

double pixels(double value, const QString &unit, double rootSize)
{
    return unit == QLatin1String("px") ? value : value * rootSize;
}

// A value like "48rem" or "768px"; false for anything else (calc, var, a bare number).
bool lengthOf(const QString &text, double rootSize, double *out)
{
    static const QRegularExpression length(QStringLiteral(R"(^\s*([0-9]*\.?[0-9]+)\s*(px|rem|em)\s*$)"));
    const QRegularExpressionMatch match = length.match(text);
    if (!match.hasMatch())
        return false;
    *out = pixels(match.captured(1).toDouble(), match.captured(2), rootSize);
    return true;
}
}

QList<int> Breakpoints::defaults()
{
    return {390, 768, 1280, 1440};
}

QString Breakpoints::scanScript()
{
    return QStringLiteral(R"JS((() => {
  const media = [];
  const vars = {};
  const walk = (rules, depth) => {
    for (const rule of rules) {
      if (rule.media && rule.media.mediaText !== undefined && rule.cssRules) media.push(rule.media.mediaText);
      else if (rule.conditionText !== undefined && rule.type === 4) media.push(rule.conditionText);
      if (rule.style && /^(:root|:host|html)\b/.test(rule.selectorText || "")) {
        for (const name of rule.style) {
          if (name.startsWith("--breakpoint-")) vars[name] = rule.style.getPropertyValue(name).trim();
        }
      }
      if (rule.cssRules && depth < 6) walk(rule.cssRules, depth + 1);
    }
  };
  for (const sheet of document.styleSheets) {
    try { walk(sheet.cssRules, 0); } catch (error) { /* a stylesheet from another origin */ }
  }
  const rootFontSize = parseFloat(getComputedStyle(document.documentElement).fontSize) || 16;
  return JSON.stringify({ rootFontSize, media, vars });
})())JS");
}

QList<int> Breakpoints::fromScan(const QJsonObject &scan)
{
    double rootSize = scan["rootFontSize"].toDouble(16);
    if (rootSize < 4 || rootSize > 64)
        rootSize = 16;
    QHash<int, int> uses;
    const auto count = [&](double width) {
        const int rounded = int(std::lround(width));
        if (rounded >= lowest && rounded <= highest)
            ++uses[rounded];
    };
    static const QRegularExpression legacy(QStringLiteral(R"((?:min|max)-width\s*:\s*([0-9]*\.?[0-9]+)\s*(px|rem|em))"));
    // Range syntax: "width >= 40rem", "width < 48rem", "40rem <= width".
    static const QRegularExpression before(QStringLiteral(R"(\bwidth\s*(?:>=|<=|>|<|=)\s*([0-9]*\.?[0-9]+)\s*(px|rem|em))"));
    static const QRegularExpression after(QStringLiteral(R"(([0-9]*\.?[0-9]+)\s*(px|rem|em)\s*(?:>=|<=|>|<)\s*width\b)"));
    for (const QJsonValue &query : scan["media"].toArray()) {
        const QString text = query.toString();
        for (const QRegularExpression *pattern : {&legacy, &before}) {
            for (auto it = pattern->globalMatch(text); it.hasNext();) {
                const QRegularExpressionMatch match = it.next();
                count(pixels(match.captured(1).toDouble(), match.captured(2), rootSize));
            }
        }
        for (auto it = after.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch match = it.next();
            count(pixels(match.captured(1).toDouble(), match.captured(2), rootSize));
        }
    }
    const QJsonObject vars = scan["vars"].toObject();
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        double width = 0;
        if (lengthOf(it.value().toString(), rootSize, &width))
            count(width);
    }
    if (uses.isEmpty())
        return defaults();
    QList<int> widths = uses.keys();
    std::sort(widths.begin(), widths.end(), [&](int a, int b) { return uses[a] != uses[b] ? uses[a] > uses[b] : a < b; });
    widths = widths.mid(0, most);
    std::sort(widths.begin(), widths.end());
    return widths;
}
