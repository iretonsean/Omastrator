#include "Anywhere/Lift.h"
#include "Anywhere/LiftScript.h"
#include "Document/PathOperations.h"
#include "IO/SvgImporter.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainterPath>
#include <QUrl>
#include <cmath>

// Lift for pages: the DOM's answer (LiftScript.h) as shapes, text, images
// and imported SVG, grouped as the page nests them.

namespace {
QRectF rectOf(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return QRectF(a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble(), a.at(3).toDouble());
}

QColor colorOf(const QJsonValue &value)
{
    // #rrggbbaa from the page.
    const QString text = value.toString();
    if (text.size() == 9) {
        QColor color = QColor::fromString(text.left(7));
        color.setAlpha(text.mid(7, 2).toInt(nullptr, 16));
        return color;
    }
    return QColor::fromString(text);
}

// A rounded rectangle with CSS's corners (each [rx, ry]), top left first.
QPainterPath rounded(const QRectF &r, const std::array<QSizeF, 4> &radii)
{
    QPainterPath path;
    const QSizeF tl = radii[0], tr = radii[1], br = radii[2], bl = radii[3];
    path.moveTo(r.left() + tl.width(), r.top());
    path.lineTo(r.right() - tr.width(), r.top());
    if (!tr.isEmpty())
        path.arcTo(QRectF(r.right() - 2 * tr.width(), r.top(), 2 * tr.width(), 2 * tr.height()), 90, -90);
    path.lineTo(r.right(), r.bottom() - br.height());
    if (!br.isEmpty())
        path.arcTo(QRectF(r.right() - 2 * br.width(), r.bottom() - 2 * br.height(), 2 * br.width(), 2 * br.height()), 0, -90);
    path.lineTo(r.left() + bl.width(), r.bottom());
    if (!bl.isEmpty())
        path.arcTo(QRectF(r.left(), r.bottom() - 2 * bl.height(), 2 * bl.width(), 2 * bl.height()), 270, -90);
    path.lineTo(r.left(), r.top() + tl.height());
    if (!tl.isEmpty())
        path.arcTo(QRectF(r.left(), r.top(), 2 * tl.width(), 2 * tl.height()), 180, -90);
    path.closeSubpath();
    return path;
}

std::array<QSizeF, 4> radiiOf(const QJsonValue &value)
{
    std::array<QSizeF, 4> radii{};
    const QJsonArray list = value.toArray();
    for (int i = 0; i < 4 && i < list.size(); ++i) {
        const QJsonArray pair = list.at(i).toArray();
        radii[size_t(i)] = QSizeF(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    return radii;
}

std::array<QSizeF, 4> grown(std::array<QSizeF, 4> radii, double by)
{
    for (QSizeF &r : radii)
        r = r.isEmpty() ? r : QSizeF(std::max(0.0, r.width() + by), std::max(0.0, r.height() + by));
    return radii;
}

VectorObject pathObject(const QString &name, const VectorPath &path)
{
    VectorObject object;
    object.kind = ObjectKind::path;
    object.name = name;
    object.path = path;
    object.stroke = StrokeStyle();
    object.stroke.paint = Paint::none();
    object.stroke.width = 0;
    return object;
}

// A box's shape: a live rectangle when its corners are round, else the path.
VectorObject boxObject(const QString &name, const QRectF &rect, const std::array<QSizeF, 4> &radii)
{
    bool circular = true;
    for (const QSizeF &r : radii)
        circular = circular && std::abs(r.width() - r.height()) < 0.5;
    if (circular) {
        LiveRectangle shape;
        shape.rect = rect;
        for (int i = 0; i < 4; ++i)
            shape.radii[size_t(i)] = radii[size_t(i)].width();
        VectorObject object = pathObject(name, shape.path());
        object.shape = shape;
        return object;
    }
    return pathObject(name, VectorPath::fromPainterPath(rounded(rect, radii)));
}

std::vector<GradientStop> stopsOf(const QJsonValue &value)
{
    std::vector<GradientStop> stops;
    for (const QJsonValue &each : value.toArray())
        stops.push_back({each.toArray().at(0).toDouble(), colorOf(each.toArray().at(1))});
    return stops;
}

// A CSS gradient as a paint over `size`: its ends are fractions of the box.
std::optional<Paint> gradientPaint(const QJsonObject &fill, QSizeF size)
{
    Paint paint;
    paint.stops = stopsOf(fill["stops"]);
    if (paint.stops.size() < 2 || size.isEmpty())
        return std::nullopt;
    const double w = size.width(), h = size.height();
    if (fill["kind"].toString() == QLatin1String("linear")) {
        const double angle = qDegreesToRadians(fill["angle"].toDouble(180));
        const QPointF direction(std::sin(angle), -std::cos(angle));
        const double length = std::abs(w * std::sin(angle)) + std::abs(h * std::cos(angle));
        const QPointF centre(w / 2, h / 2);
        const QPointF from = centre - direction * length / 2, to = centre + direction * length / 2;
        paint.kind = PaintKind::linearGradient;
        paint.start = QPointF(from.x() / w, from.y() / h);
        paint.end = QPointF(to.x() / w, to.y() / h);
        return paint;
    }
    // Radial: a circle out to the farthest corner, as CSS's default.
    const QPointF centre(fill["cx"].toDouble(0.5) * w, fill["cy"].toDouble(0.5) * h);
    double radius = 0;
    for (const QPointF corner : {QPointF(0, 0), QPointF(w, 0), QPointF(w, h), QPointF(0, h)})
        radius = std::max(radius, QLineF(centre, corner).length());
    paint.kind = PaintKind::radialGradient;
    paint.start = QPointF(centre.x() / w, centre.y() / h);
    paint.end = QPointF((centre.x() + radius) / w, centre.y() / h);
    return paint;
}

// "Inter, system-ui, sans-serif": the first family this machine has, as Chromium picked it here too.
QString familyOf(const QString &list)
{
    const QStringList families = list.split(QLatin1Char(','));
    QString first;
    for (QString family : families) {
        family = family.trimmed().remove(QLatin1Char('"')).remove(QLatin1Char('\''));
        if (family.isEmpty())
            continue;
        if (first.isEmpty())
            first = family;
        static const QHash<QString, QString> generic{{"sans-serif", "Sans Serif"}, {"serif", "Serif"}, {"monospace", "Monospace"},
                                                     {"system-ui", "Sans Serif"}, {"ui-sans-serif", "Sans Serif"},
                                                     {"ui-monospace", "Monospace"}, {"cursive", "Sans Serif"}, {"-apple-system", ""}};
        if (generic.contains(family)) {
            if (!generic.value(family).isEmpty())
                return generic.value(family);
            continue;
        }
        if (QFontDatabase::hasFamily(family))
            return family;
    }
    return first.isEmpty() ? QStringLiteral("Sans Serif") : first;
}

CharacterFormat formatOf(const QJsonObject &style)
{
    CharacterFormat format;
    format.family = familyOf(style["f"].toString());
    format.style = TextContent::styleFor(format.family, style["w"].toInt(400), style["it"].toBool());
    format.size = std::max(1.0, style["sz"].toDouble(16));
    format.tracking = style["ls"].toDouble() / format.size * 1000;
    format.textCase = style["up"].toBool() ? TextCase::allCaps : TextCase::normal;
    format.underline = style["u"].toBool();
    format.strikethrough = style["st"].toBool();
    format.fill = colorOf(style["c"]);
    return format;
}

// Where a word's baseline sits in its box: the font's ascent share of the box, as the browser drew it.
double baselineIn(const QRectF &word, const CharacterFormat &format)
{
    const QFontMetricsF metrics(format.font(1));
    const double full = metrics.ascent() + metrics.descent();
    return word.top() + (full > 0 ? word.height() * metrics.ascent() / full : word.height() * 0.8);
}

// The element's lines as one text object: each line a paragraph, placed where the page broke it.
std::optional<VectorObject> textObject(const QJsonObject &text, const QJsonArray &styles, const QString &name)
{
    const QJsonArray lines = text["lines"].toArray();
    if (lines.isEmpty())
        return std::nullopt;
    VectorObject object;
    object.kind = ObjectKind::text;
    object.name = name;
    object.stroke = StrokeStyle();
    object.stroke.paint = Paint::none();
    object.stroke.width = 0;
    TextContent &content = object.text;
    const QJsonObject firstRun = lines.first().toObject()["runs"].toArray().first().toObject();
    const CharacterFormat base = formatOf(styles.at(firstRun["s"].toInt()).toObject());
    content.character() = base;
    content.fill.reset();
    object.fill = Paint::solid(base.fill.value_or(Qt::black));
    const QString align = text["align"].toString();
    const TextAlignment alignment = align == QLatin1String("center") ? TextAlignment::center
                                    : (align == QLatin1String("right") || align == QLatin1String("end")) ? TextAlignment::right
                                                                                                          : TextAlignment::left;
    double originX = 0;
    std::vector<double> baselines, lefts;
    QString all;
    for (int index = 0; index < lines.size(); ++index) {
        const QJsonObject line = lines.at(index).toObject();
        const QRectF box(line["x"].toDouble(), line["y"].toDouble(), line["w"].toDouble(), line["h"].toDouble());
        if (index > 0)
            all += QLatin1Char('\n');
        const QJsonArray runs = line["runs"].toArray();
        double baseline = box.top() + box.height() * 0.8;
        for (int r = 0; r < runs.size(); ++r) {
            const QJsonObject run = runs.at(r).toObject();
            const CharacterFormat format = formatOf(styles.at(run["s"].toInt()).toObject());
            if (r == 0 && run.contains("r"))
                baseline = baselineIn(rectOf(run["r"]), format);
            const int start = int(all.size());
            all += run["t"].toString();
            CharacterFormat own = format;
            if (own.fill == base.fill)
                own.fill.reset();
            CharacterFormat plain = base;
            plain.fill.reset();
            if (!(own == plain))
                content.runs.push_back({start, int(all.size()) - start, own});
        }
        baselines.push_back(baseline);
        lefts.push_back(alignment == TextAlignment::center ? box.center().x() : alignment == TextAlignment::right ? box.right() : box.left());
    }
    content.text = all;
    content.normalize();
    if (alignment == TextAlignment::left) {
        originX = *std::min_element(lefts.begin(), lefts.end());
    } else {
        double sum = 0;
        for (double x : lefts)
            sum += x;
        originX = sum / double(lefts.size());
    }
    content.alignment = alignment;
    for (int index = 0; index < int(baselines.size()); ++index) {
        ParagraphFormat paragraph = content.paragraph();
        if (index > 0)
            paragraph.leading = baselines[size_t(index)] - baselines[size_t(index) - 1];
        // Left-aligned lines keep where the page started each one.
        if (alignment == TextAlignment::left)
            paragraph.leftIndent = lefts[size_t(index)] - originX;
        if (!(paragraph == content.paragraph()))
            content.paragraphFormats[index] = paragraph;
    }
    object.transform = QTransform::fromTranslate(originX, baselines.front());
    return object;
}

// An image drawn into `dest`, cut to what shows inside `clip`, as its own pixels.
std::optional<VectorObject> placedImage(const QImage &image, const QRectF &dest, const QRectF &clip, const QString &name)
{
    const QRectF shown = dest.intersected(clip);
    if (image.isNull() || shown.isEmpty() || dest.isEmpty())
        return std::nullopt;
    const double sx = image.width() / dest.width(), sy = image.height() / dest.height();
    const QRect source = QRectF((shown.left() - dest.left()) * sx, (shown.top() - dest.top()) * sy, shown.width() * sx, shown.height() * sy)
                             .toAlignedRect()
                             .intersected(image.rect());
    if (source.isEmpty())
        return std::nullopt;
    VectorObject object;
    object.kind = ObjectKind::image;
    object.name = name;
    object.image = image.copy(source).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    object.transform = QTransform::fromScale(shown.width() / source.width(), shown.height() / source.height())
                       * QTransform::fromTranslate(shown.left(), shown.top());
    return object;
}

double lengthIn(const QString &value, double whole, double fallback)
{
    if (value.endsWith(QLatin1Char('%')))
        return value.chopped(1).toDouble() / 100 * whole;
    if (value.endsWith(QLatin1String("px")))
        return value.chopped(2).toDouble();
    return fallback;
}

// Where object-fit and object-position (or background-size and -position) draw an image of `natural` size in `box`.
QRectF fitted(QSizeF natural, const QRectF &box, const QString &fit, const QString &position)
{
    QSizeF size = box.size();
    if (!natural.isEmpty()) {
        const double sx = box.width() / natural.width(), sy = box.height() / natural.height();
        if (fit == QLatin1String("cover"))
            size = natural * std::max(sx, sy);
        else if (fit == QLatin1String("contain"))
            size = natural * std::min(sx, sy);
        else if (fit == QLatin1String("none") || fit == QLatin1String("auto"))
            size = natural;
        else if (fit == QLatin1String("scale-down"))
            size = natural * std::min(1.0, std::min(sx, sy));
        else if (fit != QLatin1String("fill")) {
            // background-size: "100px auto", "50% 50%".
            const QStringList parts = fit.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                const double w = lengthIn(parts.value(0), box.width(), -1);
                const double h = lengthIn(parts.value(1, QStringLiteral("auto")), box.height(), -1);
                if (w > 0 && h > 0)
                    size = QSizeF(w, h);
                else if (w > 0)
                    size = QSizeF(w, w * natural.height() / natural.width());
                else if (h > 0)
                    size = QSizeF(h * natural.width() / natural.height(), h);
            }
        }
    }
    const QStringList parts = position.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    auto place = [&](const QString &value, double free) {
        if (value.endsWith(QLatin1Char('%')))
            return value.chopped(1).toDouble() / 100 * free;
        return lengthIn(value, free, free / 2);
    };
    const double x = place(parts.value(0, QStringLiteral("50%")), box.width() - size.width());
    const double y = place(parts.value(1, QStringLiteral("50%")), box.height() - size.height());
    return QRectF(box.topLeft() + QPointF(x, y), size);
}

// The imported SVG's objects, scaled into `rect` (the SVG's own size is the rendered one).
std::vector<VectorObject> svgObjects(const QByteArray &markup, const QRectF &rect, QStringList &notes)
{
    VectorDocument imported;
    try {
        imported = SvgImporter::parse(markup);
    } catch (const FileError &) {
        notes << QStringLiteral("An SVG couldn't be read and was left out.");
        return {};
    }
    std::vector<VectorObject> objects;
    const double sx = imported.size.width() > 0 ? rect.width() / imported.size.width() : 1;
    const double sy = imported.size.height() > 0 ? rect.height() / imported.size.height() : 1;
    VectorDocument placed = imported;
    for (const QUuid &layer : imported.layers()) {
        for (const QUuid &id : imported.children(layer))
            placed.transform(id, QTransform::fromScale(sx, sy) * QTransform::fromTranslate(rect.left(), rect.top()));
    }
    for (const VectorObject &object : placed.objects) {
        if (object.kind != ObjectKind::layer)
            objects.push_back(object);
    }
    // Layers go: their children hang from the group the caller makes.
    for (VectorObject &object : objects) {
        const VectorObject *parent = object.parentID ? placed.find(*object.parentID) : nullptr;
        if (!parent || parent->kind == ObjectKind::layer)
            object.parentID.reset();
    }
    return objects;
}

void insertAll(VectorDocument &art, std::vector<VectorObject> objects, const QUuid &parent)
{
    for (VectorObject &object : objects) {
        const QUuid into = object.parentID.value_or(parent);
        art.insert(std::move(object), into);
    }
}

QRectF regionOf(const QJsonObject &answer)
{
    return answer["region"].isArray() ? rectOf(answer["region"]) : QRectF();
}
}

namespace Lift {
QString webScript(bool element, const QRectF &rect, const Limits &limits)
{
    QJsonObject options{{"mode", element ? "element" : "region"},
                        {"maxElements", limits.maxElements},
                        {"maxCharacters", limits.maxCharacters}};
    if (!rect.isEmpty())
        options["rect"] = QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
    return QStringLiteral("%1(%2)").arg(QLatin1String(LiftScript::source),
                                        QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
}

QStringList imageAddresses(const QJsonObject &answer, const Limits &limits)
{
    QStringList addresses;
    auto add = [&](const QString &url) {
        if (!url.isEmpty() && !addresses.contains(url) && addresses.size() < limits.maxImages)
            addresses << url;
    };
    for (const QJsonValue &value : answer["nodes"].toArray()) {
        const QJsonObject node = value.toObject();
        add(node["img"].toObject()["url"].toString());
        for (const QJsonValue &fill : node["box"].toObject()["fills"].toArray()) {
            if (fill.toObject()["kind"].toString() == QLatin1String("image"))
                add(fill.toObject()["url"].toString());
        }
    }
    return addresses;
}

std::vector<std::pair<int, QRectF>> shotBoxes(const QJsonObject &answer)
{
    std::vector<std::pair<int, QRectF>> boxes;
    const QJsonArray nodes = answer["nodes"].toArray();
    for (int index = 0; index < nodes.size(); ++index) {
        const QJsonObject node = nodes.at(index).toObject();
        if (node["shot"].toBool())
            boxes.emplace_back(index, rectOf(node["r"]));
    }
    return boxes;
}

std::optional<Result> fromDom(const QJsonObject &answer, const Resources &resources, QString *error, const std::function<bool(int, int)> &progress)
{
    auto failed = [&](const QString &message) -> std::optional<Result> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (answer.contains("error"))
        return failed(answer["error"].toString());
    const QJsonArray nodes = answer["nodes"].toArray();
    const QJsonArray styles = answer["styles"].toArray();
    if (nodes.isEmpty())
        return failed(QStringLiteral("There's nothing there to lift."));
    Result result;
    result.method = QStringLiteral("dom");
    result.art = VectorDocument::blank(QSizeF(1, 1));
    result.art.background = Qt::transparent;
    const QUuid layer = result.art.layers().front();
    const QRectF region = regionOf(answer);
    const QJsonObject page = answer["page"].toObject();

    std::vector<QUuid> containers(size_t(nodes.size()));
    std::vector<std::pair<QUuid, QTransform>> transforms;
    for (int index = 0; index < nodes.size(); ++index) {
        if (progress && index % 50 == 0 && !progress(index, int(nodes.size())))
            return failed(QStringLiteral("Cancelled."));
        const QJsonObject node = nodes.at(index).toObject();
        const int parentIndex = node["p"].toInt(-1);
        const QUuid parent = parentIndex >= 0 && parentIndex < index ? containers[size_t(parentIndex)] : layer;
        const QString name = node["name"].toString();
        const QString selector = node["sel"].toString();
        const QRectF rect = rectOf(node["r"]);

        // What the element draws itself, bottom to top: shadows, its box, its border, then its content.
        std::vector<VectorObject> under;
        std::vector<std::vector<VectorObject>> content;
        const QJsonObject box = node["box"].toObject();
        const std::array<QSizeF, 4> radii = radiiOf(box["rad"]);
        if (index == 0 && !page.isEmpty()) {
            VectorObject paper = pathObject(QStringLiteral("Page background"), Shapes::rectangle(rectOf(page["r"])));
            paper.fill = Paint::solid(colorOf(page["c"]));
            under.push_back(paper);
        }
        for (const QJsonValue &value : box["shadows"].toArray()) {
            const QJsonObject shadow = value.toObject();
            // Only a sharp shadow has a shape of its own; blurred and inset ones aren't drawn.
            if (shadow["inset"].toBool() || shadow["blur"].toDouble() > 0.5) {
                if (!result.notes.contains(QStringLiteral("Blurred and inset shadows were left out.")))
                    result.notes << QStringLiteral("Blurred and inset shadows were left out.");
                continue;
            }
            const double spread = shadow["spread"].toDouble();
            const QRectF at = rect.translated(shadow["x"].toDouble(), shadow["y"].toDouble()).adjusted(-spread, -spread, spread, spread);
            VectorObject made = boxObject(QStringLiteral("Shadow"), at, grown(radii, spread));
            made.fill = Paint::solid(colorOf(shadow["c"]));
            under.push_back(made);
        }
        std::vector<Paint> fills;
        for (const QJsonValue &value : box["fills"].toArray()) {
            const QJsonObject fill = value.toObject();
            const QString kind = fill["kind"].toString();
            if (kind == QLatin1String("solid")) {
                fills.push_back(Paint::solid(colorOf(fill["c"])));
            } else if (kind == QLatin1String("linear") || kind == QLatin1String("radial")) {
                if (const auto paint = gradientPaint(fill, rect.size()))
                    fills.push_back(*paint);
            } else if (kind == QLatin1String("image")) {
                // Background pictures sit over the colour, cut to the box.
                const QString url = fill["url"].toString();
                const QImage image = resources.images.value(url);
                if (image.isNull())
                    continue;
                const QRectF dest = fitted(image.size(), rect, fill["size"].toString(), fill["position"].toString());
                if (auto placed = placedImage(image, dest, rect, QStringLiteral("Background image")))
                    content.insert(content.begin(), {*placed});
            }
        }
        if (!fills.empty()) {
            VectorObject made = boxObject(QStringLiteral("Background"), rect, radii);
            made.setFills(fills);
            under.push_back(made);
        }
        const QJsonObject border = box["border"].toObject();
        if (!border.isEmpty()) {
            const QJsonArray widths = border["w"].toArray(), colors = border["c"].toArray(), kinds = border["s"].toArray();
            const double top = widths.at(0).toDouble(), right = widths.at(1).toDouble(), bottom = widths.at(2).toDouble(), left = widths.at(3).toDouble();
            const bool sameWidth = top == right && right == bottom && bottom == left;
            const bool sameColor = colors.at(0) == colors.at(1) && colors.at(1) == colors.at(2) && colors.at(2) == colors.at(3);
            const bool sameKind = kinds.at(0) == kinds.at(1) && kinds.at(1) == kinds.at(2) && kinds.at(2) == kinds.at(3);
            if (sameWidth && sameColor && sameKind) {
                VectorObject made = boxObject(QStringLiteral("Border"), rect, radii);
                made.fill = Paint::none();
                made.stroke = StrokeStyle();
                made.stroke.paint = Paint::solid(colorOf(colors.at(0)));
                made.stroke.width = top;
                made.stroke.alignment = StrokeAlignment::inside;
                const QString kind = kinds.at(0).toString();
                if (kind == QLatin1String("dashed"))
                    made.stroke.dashes = {top * 3, top * 3};
                else if (kind == QLatin1String("dotted")) {
                    made.stroke.dashes = {0, top * 2};
                    made.stroke.cap = Qt::RoundCap;
                }
                under.push_back(made);
            } else if (sameColor) {
                // The ring between the border edge and the padding edge.
                const QRectF inner = rect.adjusted(left, top, -right, -bottom);
                std::array<QSizeF, 4> innerRadii = radii;
                for (int i = 0; i < 4; ++i) {
                    const double side = i == 0 || i == 3 ? left : right, edge = i < 2 ? top : bottom;
                    innerRadii[size_t(i)] = QSizeF(std::max(0.0, radii[size_t(i)].width() - side), std::max(0.0, radii[size_t(i)].height() - edge));
                }
                QPainterPath ring = rounded(rect, radii);
                ring.addPath(rounded(inner, innerRadii));
                ring.setFillRule(Qt::OddEvenFill);
                VectorObject made = pathObject(QStringLiteral("Border"), VectorPath::fromPainterPath(ring));
                made.path.fillRule = Qt::OddEvenFill;
                made.fill = Paint::solid(colorOf(colors.at(0)));
                under.push_back(made);
            } else {
                // A side each, in its own colour.
                const QRectF inner = rect.adjusted(left, top, -right, -bottom);
                const std::array<std::array<QPointF, 4>, 4> sides{{{rect.topLeft(), rect.topRight(), inner.topRight(), inner.topLeft()},
                                                                   {rect.topRight(), rect.bottomRight(), inner.bottomRight(), inner.topRight()},
                                                                   {rect.bottomRight(), rect.bottomLeft(), inner.bottomLeft(), inner.bottomRight()},
                                                                   {rect.bottomLeft(), rect.topLeft(), inner.topLeft(), inner.bottomLeft()}}};
                static const char *names[] = {"Border Top", "Border Right", "Border Bottom", "Border Left"};
                for (int i = 0; i < 4; ++i) {
                    if (widths.at(i).toDouble() <= 0 || colorOf(colors.at(i)).alpha() == 0)
                        continue;
                    QPainterPath side;
                    side.moveTo(sides[size_t(i)][0]);
                    for (int p = 1; p < 4; ++p)
                        side.lineTo(sides[size_t(i)][size_t(p)]);
                    side.closeSubpath();
                    VectorObject made = pathObject(QLatin1String(names[i]), VectorPath::fromPainterPath(side));
                    made.fill = Paint::solid(colorOf(colors.at(i)));
                    under.push_back(made);
                }
            }
        }
        for (const QJsonValue &value : node["marks"].toArray()) {
            const QJsonObject mark = value.toObject();
            const double r = mark["rad"].toDouble();
            VectorObject made = boxObject(QStringLiteral("Highlight"), rectOf(mark["r"]), {QSizeF(r, r), QSizeF(r, r), QSizeF(r, r), QSizeF(r, r)});
            made.fill = Paint::solid(colorOf(mark["c"]));
            content.push_back({made});
        }
        const QJsonObject img = node["img"].toObject();
        if (!img.isEmpty()) {
            const QString url = img["url"].toString();
            const QJsonArray natural = img["natural"].toArray();
            if (resources.svgs.contains(url)) {
                const QRectF dest = fitted(QSizeF(natural.at(0).toDouble(), natural.at(1).toDouble()), rect, img["fit"].toString(), img["position"].toString());
                std::vector<VectorObject> objects = svgObjects(resources.svgs.value(url), dest, result.notes);
                if (!objects.empty())
                    content.push_back(objects);
            } else {
                QImage image = resources.images.value(url);
                if (image.isNull())
                    image = resources.shots.value(index);
                const QRectF dest = image.isNull() || resources.images.value(url).isNull()
                                        ? rect
                                        : fitted(image.size(), rect, img["fit"].toString(), img["position"].toString());
                if (auto placed = placedImage(image, dest, rect, QStringLiteral("Image")))
                    content.push_back({*placed});
                else
                    result.notes << QStringLiteral("An image couldn't be fetched and was left out.");
            }
        }
        if (node["svg"].isString()) {
            std::vector<VectorObject> objects = svgObjects(node["svg"].toString().toUtf8(), rect, result.notes);
            if (!objects.empty())
                content.push_back(objects);
        }
        if (node["shot"].toBool()) {
            if (auto placed = placedImage(resources.shots.value(index), rect, rect, QStringLiteral("Picture")))
                content.push_back({*placed});
        }
        if (node["text"].isObject()) {
            if (auto text = textObject(node["text"].toObject(), styles, QStringLiteral("Text")))
                content.push_back({*text});
        }

        const bool clips = node["clip"].isObject();
        const bool transformed = node["tf"].isArray();
        const double opacity = node["op"].toDouble(1);
        const int kids = node["k"].toInt();
        const size_t parts = under.size() + content.size();
        const bool single = index > 0 && kids == 0 && parts == 1 && !clips && !transformed && (content.empty() || content.front().size() == 1);
        if (single) {
            VectorObject made = under.empty() ? content.front().front() : under.front();
            made.name = name;
            made.liftedFrom = selector;
            made.opacity = opacity;
            made.parentID.reset();
            containers[size_t(index)] = made.id;
            result.art.insert(std::move(made), parent);
            continue;
        }
        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = name;
        group.liftedFrom = selector;
        group.opacity = opacity;
        group.isExpanded = index == 0;
        const QUuid groupId = group.id;
        result.art.insert(std::move(group), parent);
        if (index == 0)
            result.root = groupId;
        for (VectorObject &made : under) {
            made.liftedFrom = selector;
            result.art.insert(std::move(made), groupId);
        }
        QUuid into = groupId;
        if (clips) {
            // overflow: hidden: the padding box, rounded as the page rounds it, clips the content.
            const QJsonObject clip = node["clip"].toObject();
            VectorObject clipGroup;
            clipGroup.kind = ObjectKind::group;
            clipGroup.name = QStringLiteral("Clip Group");
            clipGroup.isClipGroup = true;
            clipGroup.isExpanded = false;
            into = clipGroup.id;
            result.art.insert(std::move(clipGroup), groupId);
            VectorObject mask = pathObject(QStringLiteral("Clipping Path"), VectorPath::fromPainterPath(rounded(rectOf(clip["r"]), radiiOf(clip["rad"]))));
            mask.fill = Paint::none();
            result.art.insert(std::move(mask), into);
        }
        for (std::vector<VectorObject> &objects : content) {
            if (objects.size() == 1) {
                objects.front().liftedFrom = selector;
                objects.front().parentID.reset();
                result.art.insert(std::move(objects.front()), into);
            } else {
                // An SVG: its own group.
                VectorObject svg;
                svg.kind = ObjectKind::group;
                svg.name = img.isEmpty() ? QStringLiteral("SVG") : QStringLiteral("Image");
                svg.liftedFrom = selector;
                svg.isExpanded = false;
                const QUuid svgId = svg.id;
                result.art.insert(std::move(svg), into);
                insertAll(result.art, std::move(objects), svgId);
            }
        }
        containers[size_t(index)] = into;
        if (transformed) {
            const QJsonArray m = node["tf"].toArray();
            transforms.emplace_back(groupId, QTransform(m.at(0).toDouble(), m.at(1).toDouble(), m.at(2).toDouble(), m.at(3).toDouble(), m.at(4).toDouble(),
                                                        m.at(5).toDouble()));
        }
    }
    // Transforms nest: the innermost first, so an outer one carries the inner ones with it.
    for (auto it = transforms.rbegin(); it != transforms.rend(); ++it)
        result.art.transform(it->first, it->second, true);
    // Everything outside a region lift's region is cut away by one clip over the whole.
    if (!region.isEmpty() && !result.root.isNull()) {
        const QRectF bounds = result.art.bounds(result.root, true);
        if (!region.contains(bounds)) {
            VectorObject clipGroup;
            clipGroup.kind = ObjectKind::group;
            clipGroup.name = QStringLiteral("Clip Group");
            clipGroup.isClipGroup = true;
            const QUuid clipId = clipGroup.id;
            result.art.insert(std::move(clipGroup), result.root, std::nullopt);
            result.art.move(clipId, result.root, 0);
            for (const QUuid &child : result.art.children(result.root)) {
                if (child != clipId)
                    result.art.move(child, clipId, int(result.art.children(clipId).size()));
            }
            VectorObject mask = pathObject(QStringLiteral("Clipping Path"), Shapes::rectangle(region));
            mask.fill = Paint::none();
            const QUuid maskId = mask.id;
            result.art.insert(std::move(mask), clipId);
            result.art.move(maskId, clipId, 0);
        }
    }
    if (result.root.isNull())
        return failed(QStringLiteral("There's nothing there to lift."));
    result.name = result.art.find(result.root)->name;
    result.objects = int(result.art.descendants(result.root).size());
    if (answer["truncated"].toBool())
        result.notes << QStringLiteral("Only the first %1 elements were lifted.").arg(answer["elements"].toInt() - 1);
    result.notes.removeDuplicates();
    const QRectF all = result.art.bounds(result.root, true);
    result.art.size = QSizeF(std::max(1.0, all.right()), std::max(1.0, all.bottom()));
    return result;
}
}
