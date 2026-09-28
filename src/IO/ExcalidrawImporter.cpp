#include "IO/ExcalidrawImporter.h"
#include "Document/PathOperations.h"
#include "Logging.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>
#include <algorithm>

namespace {

constexpr qint64 maximumBytes = qint64(64) << 20;

QColor parseColor(const QString &value)
{
    if (value.isEmpty() || value == QLatin1String("transparent"))
        return QColor();
    const QColor color(value);
    return color.isValid() ? color : QColor(Qt::black);
}

Paint paintFrom(const QString &css)
{
    const QColor color = parseColor(css);
    return color.isValid() ? Paint::solid(color) : Paint::none();
}

Arrowhead mapArrowhead(const QString &value, QStringList &warnings)
{
    if (value == QLatin1String("arrow"))
        return Arrowhead::arrow;
    if (value == QLatin1String("triangle") || value == QLatin1String("triangle_outline"))
        return Arrowhead::triangle;
    if (value == QLatin1String("circle") || value == QLatin1String("circle_outline") || value == QLatin1String("dot"))
        return Arrowhead::circle;
    if (value == QLatin1String("bar"))
        return Arrowhead::bar;
    if (value == QLatin1String("diamond") || value == QLatin1String("diamond_outline")) {
        warnings << QStringLiteral("Diamond arrowheads became square, the closest match.");
        return Arrowhead::square;
    }
    return Arrowhead::none;
}

std::vector<double> dashesFor(const QString &style)
{
    if (style == QLatin1String("dashed"))
        return {6, 4};
    if (style == QLatin1String("dotted"))
        return {1, 3};
    return {};
}

// Excalidraw's own current and legacy default fonts (see docs/import/quick-wins.md);
// unrecognized or missing fonts surface through the app's usual missing-font flow.
QString fontFamilyFor(int id)
{
    static const QHash<int, QString> families{
        {1, QStringLiteral("Virgil")},       {2, QStringLiteral("Helvetica")}, {3, QStringLiteral("Cascadia Code")},
        {5, QStringLiteral("Excalifont")},   {6, QStringLiteral("Nunito")},    {7, QStringLiteral("Lilita One")},
        {8, QStringLiteral("Comic Shanns Mono")}, {9, QStringLiteral("Liberation Sans")}, {10, QStringLiteral("Assistant")}};
    return families.value(id, QStringLiteral("Sans Serif"));
}

VectorPath diamondPath(double w, double h)
{
    QPainterPath p;
    p.moveTo(w / 2, 0);
    p.lineTo(w, h / 2);
    p.lineTo(w / 2, h);
    p.lineTo(0, h / 2);
    p.closeSubpath();
    return VectorPath::fromPainterPath(p);
}

std::vector<QPointF> pointsOf(const QJsonObject &json)
{
    std::vector<QPointF> points;
    for (const QJsonValue &value : json.value(QStringLiteral("points")).toArray()) {
        const QJsonArray pair = value.toArray();
        if (pair.size() >= 2)
            points.emplace_back(pair[0].toDouble(), pair[1].toDouble());
    }
    return points;
}

QRectF boundsOf(const std::vector<QPointF> &points)
{
    if (points.empty())
        return QRectF(0, 0, 0, 0);
    QRectF bounds(points.front(), QSizeF(0, 0));
    for (const QPointF &point : points)
        bounds = bounds.united(QRectF(point, QSizeF(0, 0)));
    return bounds;
}

// Rotates `localBounds` about its own centre, then places that centre at
// (originX, originY) + the centre's offset: Excalidraw's x,y is always the
// unrotated bounding box's corner, and angle is radians about its middle.
QTransform placementFor(double originX, double originY, const QRectF &localBounds, double angleRadians)
{
    const QPointF center = localBounds.center();
    QTransform t;
    t.translate(originX + center.x(), originY + center.y());
    t.rotate(qRadiansToDegrees(angleRadians));
    t.translate(-center.x(), -center.y());
    return t;
}

struct Element {
    QJsonObject json;
    QString id, type;
    double x = 0, y = 0, w = 0, h = 0, angle = 0;
};

class Builder {
public:
    explicit Builder(const QString &layerName) : layerName(layerName) {}
    QStringList warnings;

    VectorDocument build(const QJsonArray &elements, const QJsonObject &filesIn)
    {
        files = filesIn;
        std::vector<Element> ordered;
        QRectF bounds;
        for (const QJsonValue &value : elements) {
            const QJsonObject object = value.toObject();
            if (object.value(QStringLiteral("isDeleted")).toBool())
                continue;
            Element element;
            element.json = object;
            element.id = object.value(QStringLiteral("id")).toString();
            element.type = object.value(QStringLiteral("type")).toString();
            element.x = object.value(QStringLiteral("x")).toDouble();
            element.y = object.value(QStringLiteral("y")).toDouble();
            element.w = object.value(QStringLiteral("width")).toDouble();
            element.h = object.value(QStringLiteral("height")).toDouble();
            element.angle = object.value(QStringLiteral("angle")).toDouble();
            const QRectF box(element.x, element.y, element.w, element.h);
            bounds = bounds.isNull() ? box : bounds.united(box);
            ordered.push_back(std::move(element));
        }
        if (bounds.isNull())
            bounds = QRectF(0, 0, 100, 100);
        // Excalidraw's canvas has no fixed origin; give the drawing a small margin instead.
        shiftX = -bounds.left() + 40;
        shiftY = -bounds.top() + 40;
        document.size = QSizeF(bounds.width() + 80, bounds.height() + 80);

        VectorObject layer;
        layer.name = layerName;
        layer.kind = ObjectKind::layer;
        layer.parentID.reset();
        layer.layerColor = nextLayerColor(0);
        document.objects.push_back(layer);
        const QUuid layerID = document.objects.back().id;

        // Frames first, so elements that reference frameId can find them.
        for (const Element &element : ordered) {
            if (element.type == QLatin1String("frame"))
                frames.insert(element.id, buildFrame(element, layerID));
        }
        for (const Element &element : ordered) {
            if (element.type == QLatin1String("frame"))
                continue;
            const QString frameId = element.json.value(QStringLiteral("frameId")).toString();
            const auto frame = frames.constFind(frameId);
            // Grouping inside a frame isn't tracked apart from the frame itself; a
            // small simplification (see docs/import/quick-wins.md).
            place(element, frame != frames.cend() ? *frame : groupParent(element, layerID));
        }
        warnings.removeDuplicates();
        return std::move(document);
    }

private:
    QString layerName;
    QJsonObject files;
    VectorDocument document;
    QHash<QString, QUuid> frames;
    std::vector<std::pair<QString, QUuid>> groupStack;
    double shiftX = 0, shiftY = 0;

    QUuid add(VectorObject object, const QUuid &parent)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent);
        return id;
    }

    QUuid buildFrame(const Element &element, const QUuid &layerID)
    {
        const QString name = element.json.value(QStringLiteral("name")).toString();
        VectorObject frame = VectorObject::frame(QRectF(element.x + shiftX, element.y + shiftY, element.w, element.h),
                                                  name.isEmpty() ? QStringLiteral("Frame") : name);
        return add(std::move(frame), layerID);
    }

    // groupIds runs innermost to outermost; reversed, it's the nesting order top
    // to bottom. Reuses whatever prefix of currently-open groups still matches.
    QUuid groupParent(const Element &element, const QUuid &layerID)
    {
        QStringList chain;
        for (const QJsonValue &value : element.json.value(QStringLiteral("groupIds")).toArray())
            chain.prepend(value.toString());
        int common = 0;
        while (common < chain.size() && common < int(groupStack.size()) && groupStack[size_t(common)].first == chain[common])
            ++common;
        groupStack.resize(size_t(common));
        QUuid parent = groupStack.empty() ? layerID : groupStack.back().second;
        for (int i = common; i < chain.size(); ++i) {
            VectorObject group;
            group.kind = ObjectKind::group;
            group.name = QStringLiteral("Group");
            const QUuid id = add(std::move(group), parent);
            groupStack.push_back({chain[i], id});
            parent = id;
        }
        return parent;
    }

    void applyStroke(const Element &element, VectorObject &object)
    {
        object.stroke.paint = paintFrom(element.json.value(QStringLiteral("strokeColor")).toString());
        object.stroke.width = std::max(0.0, element.json.value(QStringLiteral("strokeWidth")).toDouble(1));
        object.stroke.dashes = dashesFor(element.json.value(QStringLiteral("strokeStyle")).toString());
    }

    void applyAppearance(const Element &element, VectorObject &object)
    {
        const QString background = element.json.value(QStringLiteral("backgroundColor")).toString();
        const QString fillStyleValue = element.json.value(QStringLiteral("fillStyle")).toString();
        object.fill = paintFrom(background);
        if (object.fill.isVisible() && !fillStyleValue.isEmpty() && fillStyleValue != QLatin1String("solid"))
            warnings << QStringLiteral("Sketchy fill textures (hachure, cross-hatch) became solid fills.");
        applyStroke(element, object);
        object.opacity = std::clamp(element.json.value(QStringLiteral("opacity")).toDouble(100) / 100.0, 0.0, 1.0);
        object.isLocked = element.json.value(QStringLiteral("locked")).toBool();
    }

    void place(const Element &element, const QUuid &parent)
    {
        if (element.type == QLatin1String("rectangle"))
            placeRectangle(element, parent);
        else if (element.type == QLatin1String("ellipse"))
            placeGeometry(element, parent, Shapes::ellipse(QRectF(0, 0, element.w, element.h)), QStringLiteral("Ellipse"));
        else if (element.type == QLatin1String("diamond"))
            placeGeometry(element, parent, diamondPath(element.w, element.h), QStringLiteral("Diamond"));
        else if (element.type == QLatin1String("line"))
            placeLinear(element, parent, false);
        else if (element.type == QLatin1String("arrow"))
            placeLinear(element, parent, true);
        else if (element.type == QLatin1String("freedraw"))
            placeFreedraw(element, parent);
        else if (element.type == QLatin1String("text"))
            placeText(element, parent);
        else if (element.type == QLatin1String("image"))
            placeImage(element, parent);
        else if (!element.type.isEmpty())
            warnings << QStringLiteral("Some elements had no equivalent and were left out.");
    }

    void placeRectangle(const Element &element, const QUuid &parent)
    {
        const double shortSide = std::min(element.w, element.h);
        const double radius = element.json.value(QStringLiteral("roundness")).isObject() ? std::min(shortSide * 0.25, 32.0) : 0.0;
        LiveRectangle shape;
        shape.rect = QRectF(0, 0, element.w, element.h);
        shape.placement = placementFor(element.x + shiftX, element.y + shiftY, shape.rect, element.angle);
        shape.radii = {radius, radius, radius, radius};
        VectorObject object;
        object.kind = ObjectKind::path;
        object.shape = shape;
        object.path = shape.path();
        object.name = QStringLiteral("Rectangle");
        applyAppearance(element, object);
        add(std::move(object), parent);
    }

    void placeGeometry(const Element &element, const QUuid &parent, const VectorPath &local, const QString &name)
    {
        VectorObject object;
        object.kind = ObjectKind::path;
        object.path = local.transformed(placementFor(element.x + shiftX, element.y + shiftY, QRectF(0, 0, element.w, element.h), element.angle));
        object.name = name;
        applyAppearance(element, object);
        add(std::move(object), parent);
    }

    void placeLinear(const Element &element, const QUuid &parent, bool isArrow)
    {
        const std::vector<QPointF> points = pointsOf(element.json);
        if (points.size() < 2)
            return;
        QPainterPath p(points.front());
        for (size_t i = 1; i < points.size(); ++i)
            p.lineTo(points[i]);
        VectorObject object;
        object.kind = ObjectKind::path;
        object.path = VectorPath::fromPainterPath(p).transformed(
            placementFor(element.x + shiftX, element.y + shiftY, boundsOf(points), element.angle));
        object.fill = Paint::none();
        applyStroke(element, object);
        if (isArrow) {
            object.stroke.startArrow = mapArrowhead(element.json.value(QStringLiteral("startArrowhead")).toString(), warnings);
            object.stroke.endArrow = mapArrowhead(element.json.value(QStringLiteral("endArrowhead")).toString(QStringLiteral("arrow")), warnings);
        }
        object.opacity = std::clamp(element.json.value(QStringLiteral("opacity")).toDouble(100) / 100.0, 0.0, 1.0);
        object.isLocked = element.json.value(QStringLiteral("locked")).toBool();
        object.name = isArrow ? QStringLiteral("Arrow") : QStringLiteral("Line");
        add(std::move(object), parent);
    }

    void placeFreedraw(const Element &element, const QUuid &parent)
    {
        const std::vector<QPointF> points = pointsOf(element.json);
        if (points.size() < 2)
            return;
        VectorObject object;
        object.kind = ObjectKind::path;
        // Import clean geometry, not the hand-drawn wobble (OPEN.md): a light
        // simplify tolerance smooths the freehand trace without flattening it.
        object.path = fitFreehand(points, 1.0, false)
                          .transformed(placementFor(element.x + shiftX, element.y + shiftY, boundsOf(points), element.angle));
        object.fill = Paint::none();
        applyStroke(element, object);
        object.opacity = std::clamp(element.json.value(QStringLiteral("opacity")).toDouble(100) / 100.0, 0.0, 1.0);
        object.isLocked = element.json.value(QStringLiteral("locked")).toBool();
        object.name = QStringLiteral("Drawing");
        add(std::move(object), parent);
    }

    void placeText(const Element &element, const QUuid &parent)
    {
        const QString text = element.json.value(QStringLiteral("text")).toString();
        if (text.isEmpty())
            return;
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text.text = text;
        object.text.family = fontFamilyFor(int(element.json.value(QStringLiteral("fontFamily")).toDouble(2)));
        object.text.size = element.json.value(QStringLiteral("fontSize")).toDouble(20);
        object.text.leading = object.text.size * element.json.value(QStringLiteral("lineHeight")).toDouble(1.25);
        const QString align = element.json.value(QStringLiteral("textAlign")).toString();
        object.text.alignment = align == QLatin1String("center")  ? TextAlignment::center
                                 : align == QLatin1String("right") ? TextAlignment::right
                                                                    : TextAlignment::left;
        const QString color = element.json.value(QStringLiteral("strokeColor")).toString();
        object.fill = paintFrom(color.isEmpty() ? QStringLiteral("#1e1e1e") : color);
        object.stroke.paint = Paint::none();
        object.opacity = std::clamp(element.json.value(QStringLiteral("opacity")).toDouble(100) / 100.0, 0.0, 1.0);
        object.isLocked = element.json.value(QStringLiteral("locked")).toBool();
        // TextContent's own origin is the first baseline, not the box's top; an
        // approximate ascent (0.8 em, common for UI fonts) closes most of the gap.
        object.transform = QTransform::fromTranslate(0, object.text.size * 0.8)
                            * placementFor(element.x + shiftX, element.y + shiftY, QRectF(0, 0, element.w, element.h), element.angle);
        object.name = text.left(40);
        add(std::move(object), parent);
    }

    void placeImage(const Element &element, const QUuid &parent)
    {
        const QString fileId = element.json.value(QStringLiteral("fileId")).toString();
        const QJsonObject entry = files.value(fileId).toObject();
        const QString dataUrl = entry.value(QStringLiteral("dataURL")).toString();
        const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
        QImage image;
        if (comma < 0 || !image.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()))) {
            warnings << QStringLiteral("An embedded image could not be read.");
            return;
        }
        VectorObject object;
        object.kind = ObjectKind::image;
        object.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        object.fill = Paint::none();
        object.stroke.paint = Paint::none();
        const double sx = image.width() > 0 ? element.w / image.width() : 1.0;
        const double sy = image.height() > 0 ? element.h / image.height() : 1.0;
        object.transform = QTransform::fromScale(sx, sy)
                            * placementFor(element.x + shiftX, element.y + shiftY, QRectF(0, 0, element.w, element.h), element.angle);
        object.opacity = std::clamp(element.json.value(QStringLiteral("opacity")).toDouble(100) / 100.0, 0.0, 1.0);
        object.isLocked = element.json.value(QStringLiteral("locked")).toBool();
        object.name = QStringLiteral("Image");
        add(std::move(object), parent);
    }
};

VectorDocument import(const QByteArray &json, const QString &layerName, QStringList *warnings)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        throw FileError(QStringLiteral("This is not a valid Excalidraw file."));
    const QJsonObject root = doc.object();
    const QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
    if (elements.isEmpty())
        throw FileError(QStringLiteral("This Excalidraw file has no elements to import."));
    Builder builder(layerName);
    VectorDocument document = builder.build(elements, root.value(QStringLiteral("files")).toObject());
    if (warnings)
        *warnings = builder.warnings;
    qCInfo(lcIO) << "parsed Excalidraw into" << document.objects.size() << "objects;" << builder.warnings.size() << "warnings";
    return document;
}

}

namespace ExcalidrawImporter {
VectorDocument parse(const QByteArray &json, QStringList *warnings)
{
    return import(json, QStringLiteral("Layer 1"), warnings);
}

VectorDocument read(const QString &path, QStringList *warnings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw FileError(QStringLiteral("“%1” could not be opened: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    if (file.size() > maximumBytes)
        throw FileError(QStringLiteral("“%1” is too large to import.").arg(QFileInfo(path).fileName()));
    QString name = QFileInfo(path).completeBaseName();
    if (name.isEmpty())
        name = QStringLiteral("Layer 1");
    try {
        return import(file.readAll(), name, warnings);
    } catch (const FileError &error) {
        throw FileError(QStringLiteral("“%1”: %2").arg(QFileInfo(path).fileName(), error.message()));
    }
}

bool canRead(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();
    return (type == QLatin1String("excalidraw") || type == QLatin1String("excalidraw/clipboard")) && root.contains(QStringLiteral("elements"));
}
}
