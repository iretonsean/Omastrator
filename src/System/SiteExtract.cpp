#include "System/SiteExtract.h"
#include "Document/EditorSession.h"
#include "Live/Browser.h"
#include "System/TokenFiles.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <set>

namespace {
const char *const pageScript = R"JS((() => {
  const count = (map, key, weight = 1) => { if (key !== null && key !== undefined && key !== '') map[key] = (map[key] || 0) + weight; };
  const colors = {}, backgrounds = {}, texts = {}, fonts = {}, spacing = {}, radii = {}, shadows = {}, signatures = {};
  const context = document.createElement('canvas').getContext('2d');
  const normal = (value) => {
    if (!value || value === 'transparent') return null;
    context.fillStyle = '#000'; context.fillStyle = value;
    const out = context.fillStyle;
    if (/^rgba\(.*,\s*0\)$/.test(out)) return null;
    return out;
  };
  const px = (value) => { const n = parseFloat(value); return isFinite(n) ? Math.round(n * 100) / 100 : null; };
  const textOf = (element) => Array.from(element.childNodes).some((node) => node.nodeType === 3 && node.textContent.trim());
  const elements = Array.from(document.body.querySelectorAll('*')).slice(0, 6000);
  for (const element of elements) {
    const box = element.getBoundingClientRect();
    if (box.width === 0 || box.height === 0) continue;
    const style = getComputedStyle(element);
    if (style.visibility === 'hidden' || style.display === 'none') continue;
    const background = normal(style.backgroundColor);
    if (background) { count(backgrounds, background); count(colors, background); }
    if (textOf(element)) {
      const colour = normal(style.color);
      if (colour) { count(texts, colour); count(colors, colour); }
      count(fonts, [style.fontFamily, style.fontSize, style.fontWeight, style.lineHeight, style.letterSpacing].join('|'));
    }
    if (parseFloat(style.borderTopWidth) > 0) { const border = normal(style.borderTopColor); if (border) count(colors, border); }
    for (const property of ['paddingTop', 'paddingRight', 'paddingBottom', 'paddingLeft', 'marginTop', 'marginBottom', 'rowGap', 'columnGap']) {
      const value = px(style[property]);
      if (value && value > 0 && value <= 256) count(spacing, value);
    }
    const radius = px(style.borderTopLeftRadius);
    if (radius && radius > 0) count(radii, radius);
    if (style.boxShadow && style.boxShadow !== 'none') count(shadows, style.boxShadow);
    if (element.classList.length && box.width >= 24 && box.height >= 16 && box.width <= 800 && box.height <= 400) {
      const signature = element.tagName.toLowerCase() + '.' + Array.from(element.classList).sort().join('.');
      (signatures[signature] = signatures[signature] || []).push(element);
    }
  }
  const top = (map, limit) => Object.entries(map).sort((a, b) => b[1] - a[1]).slice(0, limit).map(([value, n]) => ({ value, count: n }));
  const components = Object.entries(signatures)
    .filter(([, list]) => list.length >= 3)
    .sort((a, b) => b[1].length - a[1].length)
    .slice(0, 8)
    .map(([signature, list]) => {
      const element = list[0];
      const box = element.getBoundingClientRect();
      const style = getComputedStyle(element);
      let label = element;
      if (!textOf(element)) label = Array.from(element.querySelectorAll('*')).find(textOf) || element;
      const labelStyle = getComputedStyle(label);
      const labelBox = label.getBoundingClientRect();
      return {
        signature, name: element.classList[0] || element.tagName.toLowerCase(), count: list.length,
        width: box.width, height: box.height, background: normal(style.backgroundColor),
        radius: px(style.borderTopLeftRadius) || 0, borderWidth: px(style.borderTopWidth) || 0, borderColor: normal(style.borderTopColor),
        text: (label.innerText || label.textContent || '').trim().split('\n')[0].slice(0, 40),
        textX: labelBox.left - box.left, textY: labelBox.top - box.top, textHeight: labelBox.height,
        color: normal(labelStyle.color), fontFamily: labelStyle.fontFamily, fontSize: px(labelStyle.fontSize), fontWeight: labelStyle.fontWeight,
      };
    })
    .filter((c) => c.background || c.borderWidth > 0 || c.text);
  return JSON.stringify({
    url: location.href, title: document.title,
    colors: top(colors, 24), backgrounds: top(backgrounds, 8), texts: top(texts, 8), fonts: top(fonts, 16),
    spacing: top(spacing, 12), radii: top(radii, 6), shadows: top(shadows, 4), components,
  });
})())JS";

QColor colourOf(const QString &value)
{
    return TokenFiles::parseColor(value).value_or(QColor());
}

QString numeral(double value)
{
    return QString::number(std::round(value * 100) / 100);
}

QString familyOf(const QString &stack)
{
    QString first = stack.split(QLatin1Char(',')).value(0).trimmed();
    if (first.size() >= 2 && (first.front() == QLatin1Char('"') || first.front() == QLatin1Char('\'')))
        first = first.mid(1, first.size() - 2);
    if (first.isEmpty() || first == QLatin1String("system-ui") || first == QLatin1String("sans-serif") || first == QLatin1String("ui-sans-serif")
        || first == QLatin1String("-apple-system"))
        return QStringLiteral("Sans Serif");
    return first;
}

QString capitalised(const QString &name)
{
    QString words = name;
    words.replace(QLatin1Char('-'), QLatin1Char(' ')).replace(QLatin1Char('_'), QLatin1Char(' '));
    QStringList parts = words.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString &part : parts)
        part[0] = part[0].toUpper();
    return parts.join(QLatin1Char(' '));
}
}

namespace SiteExtract {
QString script()
{
    return QString::fromUtf8(pageScript);
}

QJsonObject scan(Browser &browser, const QString &sessionId, QString *error)
{
    const QJsonObject result = browser.cdp().callAndWait(QStringLiteral("Runtime.evaluate"),
                                                         {{"expression", script()}, {"returnByValue", true}}, sessionId, error, 20'000);
    if (error && !error->isEmpty())
        return {};
    if (result.contains("exceptionDetails")) {
        if (error)
            *error = QStringLiteral("The page's script failed: %1").arg(result["exceptionDetails"].toObject()["text"].toString());
        return {};
    }
    return QJsonDocument::fromJson(result["result"].toObject()["value"].toString().toUtf8()).object();
}

QStringList Proposal::components() const
{
    QStringList sets;
    for (const VectorObject &object : objects) {
        if (object.component && !sets.contains(object.component->set))
            sets.append(object.component->set);
    }
    return sets;
}

Proposal propose(const QJsonObject &scan, const QString &source)
{
    Proposal proposal;
    proposal.source = source;
    std::vector<DesignToken> &tokens = proposal.tokens;
    // Colours: the commonest background and text colour by role, the most saturated other as the accent.
    std::set<QRgb> used;
    const auto addColour = [&](const QString &name, const QColor &colour) {
        if (!colour.isValid() || used.count(colour.rgba()))
            return;
        used.insert(colour.rgba());
        tokens.push_back(DesignToken::color(name, colour));
    };
    addColour(QStringLiteral("color/background"), colourOf(scan["backgrounds"].toArray().at(0).toObject()["value"].toString()));
    addColour(QStringLiteral("color/text"), colourOf(scan["texts"].toArray().at(0).toObject()["value"].toString()));
    QColor accent;
    for (const QJsonValue &entry : scan["colors"].toArray()) {
        const QColor colour = colourOf(entry.toObject()["value"].toString());
        if (colour.isValid() && !used.count(colour.rgba()) && colour.hsvSaturationF() > 0.35 && (!accent.isValid() || colour.hsvSaturationF() > accent.hsvSaturationF()))
            accent = colour;
    }
    addColour(QStringLiteral("color/accent"), accent);
    int palette = 1;
    for (const QJsonValue &entry : scan["colors"].toArray()) {
        if (palette > 12)
            break;
        const QColor colour = colourOf(entry.toObject()["value"].toString());
        if (colour.isValid() && !used.count(colour.rgba()))
            addColour(QStringLiteral("color/palette-%1").arg(palette++), colour);
    }
    // Type: one token per size, named on a scale around the commonest.
    struct Face {
        TypeValue type;
        int count;
    };
    std::vector<Face> faces;
    for (const QJsonValue &entry : scan["fonts"].toArray()) {
        const QStringList parts = entry.toObject()["value"].toString().split(QLatin1Char('|'));
        const auto size = TokenFiles::parseLength(parts.value(1));
        if (!size || std::any_of(faces.begin(), faces.end(), [&](const Face &f) { return std::abs(f.type.size - *size) < 0.5; }))
            continue;
        TypeValue type;
        type.family = familyOf(parts.value(0));
        type.size = *size;
        type.weight = parts.value(2).toInt() > 0 ? parts.value(2).toInt() : 400;
        if (const auto line = TokenFiles::parseLength(parts.value(3)))
            type.lineHeight = *line;
        if (const auto spacing = TokenFiles::parseLength(parts.value(4)); spacing && *size > 0)
            type.tracking = std::round(*spacing / *size * 1000);
        faces.push_back({type, entry.toObject()["count"].toInt()});
    }
    if (!faces.empty()) {
        const double base = faces.front().type.size;
        std::sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.type.size < b.type.size; });
        const auto baseAt = std::find_if(faces.begin(), faces.end(), [&](const Face &f) { return f.type.size == base; }) - faces.begin();
        static const QStringList below{QStringLiteral("sm"), QStringLiteral("xs"), QStringLiteral("2xs")};
        static const QStringList above{QStringLiteral("lg"), QStringLiteral("xl"), QStringLiteral("2xl"), QStringLiteral("3xl"), QStringLiteral("4xl"),
                                       QStringLiteral("5xl"), QStringLiteral("6xl"), QStringLiteral("7xl")};
        for (qsizetype index = 0; index < qsizetype(faces.size()); ++index) {
            const qsizetype step = index - baseAt;
            const QString name = step == 0 ? QStringLiteral("base")
                : step < 0                ? below.value(-step - 1, QStringLiteral("size-%1").arg(numeral(faces[size_t(index)].type.size)))
                                          : above.value(step - 1, QStringLiteral("size-%1").arg(numeral(faces[size_t(index)].type.size)));
            tokens.push_back(DesignToken::typography(QStringLiteral("text/") + name, faces[size_t(index)].type));
        }
    }
    std::vector<double> spaces;
    for (const QJsonValue &entry : scan["spacing"].toArray())
        spaces.push_back(entry.toObject()["value"].toString().toDouble());
    std::sort(spaces.begin(), spaces.end());
    for (const double space : spaces)
        tokens.push_back(DesignToken::number(TokenKind::spacing, QStringLiteral("spacing/") + numeral(space), space));
    std::vector<double> corners;
    for (const QJsonValue &entry : scan["radii"].toArray())
        corners.push_back(entry.toObject()["value"].toString().toDouble());
    std::sort(corners.begin(), corners.end());
    static const QStringList radiusNames{QStringLiteral("sm"), QStringLiteral("md"), QStringLiteral("lg"), QStringLiteral("xl"), QStringLiteral("2xl"),
                                         QStringLiteral("3xl")};
    for (size_t index = 0; index < corners.size(); ++index)
        tokens.push_back(DesignToken::number(TokenKind::radius, QStringLiteral("radius/") + (corners[index] >= 500 ? QStringLiteral("full") : radiusNames.value(qsizetype(index))),
                                             corners[index]));
    std::vector<ShadowValue> shadows;
    for (const QJsonValue &entry : scan["shadows"].toArray()) {
        if (const auto shadow = ShadowValue::fromCss(entry.toObject()["value"].toString()))
            shadows.push_back(*shadow);
    }
    std::sort(shadows.begin(), shadows.end(), [](const ShadowValue &a, const ShadowValue &b) { return a.blur < b.blur; });
    for (size_t index = 0; index < shadows.size(); ++index)
        tokens.push_back(DesignToken::shadowToken(QStringLiteral("shadow/") + radiusNames.value(qsizetype(index)), shadows[index]));
    // Components: the repeated element as a box with its first line of text, side by side.
    double x = 0;
    QStringList names;
    for (const QJsonValue &value : scan["components"].toArray()) {
        const QJsonObject found = value.toObject();
        QString name = capitalised(found["name"].toString());
        if (name.isEmpty())
            name = QStringLiteral("Component");
        for (int number = 2; names.contains(name); ++number)
            name = capitalised(found["name"].toString()) + QStringLiteral(" %1").arg(number);
        names.append(name);
        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = name;
        group.component = ComponentInfo{name, {}, QTransform()};
        const QRectF box(x, 0, std::max(1.0, found["width"].toDouble()), std::max(1.0, found["height"].toDouble()));
        VectorObject background;
        background.kind = ObjectKind::path;
        background.name = QStringLiteral("Background");
        background.parentID = group.id;
        LiveRectangle shape;
        shape.rect = box;
        shape.radii.fill(std::min(found["radius"].toDouble(), std::min(box.width(), box.height()) / 2));
        EditorSession::reshape(background, shape);
        const QColor fill = colourOf(found["background"].toString());
        background.fill = fill.isValid() ? Paint::solid(fill) : Paint::none();
        if (found["borderWidth"].toDouble() > 0 && colourOf(found["borderColor"].toString()).isValid()) {
            background.stroke.paint = Paint::solid(colourOf(found["borderColor"].toString()));
            background.stroke.width = found["borderWidth"].toDouble();
            background.stroke.alignment = StrokeAlignment::inside;
        } else {
            background.stroke.paint = Paint::none();
        }
        // Bound to the matching colour token, so the proposal already follows the system.
        for (const DesignToken &token : tokens) {
            if (token.kind == TokenKind::color && fill.isValid() && token.value.color == fill)
                background.fill.token = token.id;
        }
        proposal.objects.push_back(group);
        proposal.objects.push_back(background);
        if (!found["text"].toString().isEmpty()) {
            VectorObject label;
            label.kind = ObjectKind::text;
            label.name = QStringLiteral("Label");
            label.parentID = group.id;
            label.text.text = found["text"].toString();
            label.text.family = familyOf(found["fontFamily"].toString());
            label.text.size = std::max(1.0, found["fontSize"].toDouble(16));
            label.text.style = TextContent::styleFor(label.text.family, found["fontWeight"].toString().toInt() > 0 ? found["fontWeight"].toString().toInt() : 400, false);
            const QColor colour = colourOf(found["color"].toString());
            label.fill = Paint::solid(colour.isValid() ? colour : QColor(Qt::black));
            label.stroke.paint = Paint::none();
            for (const DesignToken &token : tokens) {
                if (token.kind == TokenKind::color && token.value.color == label.fill.color)
                    label.fill.token = token.id;
            }
            const double baseline = found["textY"].toDouble() + found["textHeight"].toDouble() * 0.5 + label.text.size * 0.35;
            label.transform = QTransform::fromTranslate(x + found["textX"].toDouble(), baseline);
            proposal.objects.push_back(label);
        }
        x += box.width() + 40;
    }
    return proposal;
}

SyncPlan pullPlan(const Proposal &proposal, const QString &documentName, std::function<QString(const Proposal &)> apply)
{
    SyncPlan plan;
    plan.direction = SyncPlan::Direction::pull;
    plan.title = QStringLiteral("Use Design System from Site");
    if (proposal.tokens.empty() && proposal.objects.empty()) {
        plan.problem = QStringLiteral("Nothing could be read from %1.").arg(proposal.source);
        return plan;
    }
    plan.reads = {proposal.source};
    plan.destination = QStringLiteral("This document, %1. No files are written, nothing is committed, and nothing is sent to the site.").arg(documentName);
    plan.inApp = QStringLiteral("Adds %1 tokens and %2 components (%3) to %4 as one undo step; the components go on a Components layer beside the artboard.")
                     .arg(proposal.tokens.size())
                     .arg(proposal.components().size())
                     .arg(proposal.components().join(QStringLiteral(", ")), documentName);
    plan.apply = [apply, proposal] { return apply(proposal); };
    return plan;
}
}
