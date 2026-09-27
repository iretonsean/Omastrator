#include "Document/DocumentCodec.h"
#include <QBuffer>
#include <QByteArray>
#include <QJsonValue>
#include <cmath>
#include <set>

namespace {
QJsonArray point(QPointF p)
{
    return {p.x(), p.y()};
}

QPointF readPoint(const QJsonValue &value, QPointF fallback = {})
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble())
        return fallback;
    const QPointF p(array[0].toDouble(), array[1].toDouble());
    if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
        throw CodecError("a point is not a finite number");
    return p;
}

QString color(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor readColor(const QJsonValue &value, QColor fallback = Qt::black)
{
    const QColor c = QColor::fromString(value.toString());
    return c.isValid() ? c : fallback;
}

QJsonArray transform(const QTransform &t)
{
    return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}

QTransform readTransform(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    if (a.size() != 6)
        return {};
    return QTransform(a[0].toDouble(1), a[1].toDouble(), a[2].toDouble(), a[3].toDouble(1), a[4].toDouble(), a[5].toDouble());
}

QString png(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QImage readPng(const QJsonValue &value)
{
    QImage image;
    image.loadFromData(QByteArray::fromBase64(value.toString().toLatin1()), "PNG");
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
}

namespace DocumentCodec {
QJsonObject encode(const Paint &paint)
{
    QJsonObject json{{"kind", rawValue(paint.kind)}};
    if (paint.kind == PaintKind::solid)
        json["color"] = color(paint.color);
    if (paint.kind == PaintKind::linearGradient || paint.kind == PaintKind::radialGradient) {
        QJsonArray stops;
        for (const GradientStop &stop : paint.stops)
            stops.append(QJsonObject{{"offset", stop.offset}, {"color", color(stop.color)}});
        json["stops"] = stops;
        json["start"] = point(paint.start);
        json["end"] = point(paint.end);
    }
    return json;
}

Paint decodePaint(const QJsonObject &json)
{
    Paint paint;
    const auto kind = paintKind(json["kind"].toString());
    if (!kind)
        throw CodecError("unknown paint kind");
    paint.kind = *kind;
    paint.color = readColor(json["color"]);
    for (const QJsonValue &value : json["stops"].toArray()) {
        const QJsonObject stop = value.toObject();
        paint.stops.push_back({std::clamp(stop["offset"].toDouble(), 0.0, 1.0), readColor(stop["color"])});
    }
    if (!paint.stops.empty())
        paint.color = paint.stops.front().color;
    paint.start = readPoint(json["start"], paint.start);
    paint.end = readPoint(json["end"], paint.end);
    return paint;
}

QJsonObject encode(const StrokeStyle &stroke)
{
    QJsonArray dashes;
    for (double dash : stroke.dashes)
        dashes.append(dash);
    return {{"paint", encode(stroke.paint)}, {"width", stroke.width}, {"cap", rawValue(stroke.cap)},
            {"join", rawValue(stroke.join)}, {"miterLimit", stroke.miterLimit}, {"dashes", dashes}};
}

StrokeStyle decodeStroke(const QJsonObject &json)
{
    StrokeStyle stroke;
    if (json.contains("paint"))
        stroke.paint = decodePaint(json["paint"].toObject());
    stroke.width = std::max(0.0, json["width"].toDouble(1));
    stroke.cap = penCapStyle(json["cap"].toString());
    stroke.join = penJoinStyle(json["join"].toString());
    stroke.miterLimit = std::max(1.0, json["miterLimit"].toDouble(10));
    for (const QJsonValue &dash : json["dashes"].toArray())
        stroke.dashes.push_back(std::max(0.0, dash.toDouble()));
    return stroke;
}

QJsonObject encode(const VectorPath &path)
{
    QJsonArray contours;
    for (const Contour &contour : path.contours) {
        QJsonArray nodes;
        for (const PathNode &node : contour.nodes) {
            QJsonObject json{{"anchor", point(node.anchor)}};
            if (node.hasIn())
                json["in"] = point(node.in);
            if (node.hasOut())
                json["out"] = point(node.out);
            if (node.smooth)
                json["smooth"] = true;
            nodes.append(json);
        }
        contours.append(QJsonObject{{"closed", contour.closed}, {"nodes", nodes}});
    }
    return {{"fillRule", path.fillRule == Qt::OddEvenFill ? "evenodd" : "nonzero"}, {"contours", contours}};
}

VectorPath decodePath(const QJsonObject &json)
{
    VectorPath path;
    path.fillRule = json["fillRule"].toString() == QLatin1String("evenodd") ? Qt::OddEvenFill : Qt::WindingFill;
    for (const QJsonValue &value : json["contours"].toArray()) {
        Contour contour;
        contour.closed = value["closed"].toBool();
        for (const QJsonValue &nodeValue : value["nodes"].toArray()) {
            const QPointF anchor = readPoint(nodeValue["anchor"]);
            PathNode node(anchor, readPoint(nodeValue["in"], anchor), readPoint(nodeValue["out"], anchor), nodeValue["smooth"].toBool());
            contour.nodes.push_back(node);
        }
        path.contours.push_back(contour);
    }
    return path;
}

QJsonObject encode(const VectorObject &object)
{
    QJsonObject json{{"id", object.id.toString(QUuid::WithoutBraces)}, {"kind", rawValue(object.kind)}, {"name", object.name}};
    if (object.parentID)
        json["parent"] = object.parentID->toString(QUuid::WithoutBraces);
    if (!object.isVisible)
        json["hidden"] = true;
    if (object.isLocked)
        json["locked"] = true;
    if (!object.isExpanded)
        json["collapsed"] = true;
    if (object.opacity != 1)
        json["opacity"] = object.opacity;
    if (object.blendMode != LayerBlendMode::normal)
        json["blendMode"] = rawValue(object.blendMode);
    if (object.layerColor.isValid())
        json["layerColor"] = color(object.layerColor);
    if (object.isClipGroup)
        json["clip"] = true;
    switch (object.kind) {
    case ObjectKind::path:
        json["path"] = encode(object.path);
        break;
    case ObjectKind::text:
        json["text"] = QJsonObject{{"string", object.text.text}, {"family", object.text.family}, {"size", object.text.size},
                                   {"bold", object.text.bold}, {"italic", object.text.italic},
                                   {"alignment", rawValue(object.text.alignment)}, {"leading", object.text.leading},
                                   {"tracking", object.text.tracking}};
        json["transform"] = transform(object.transform);
        break;
    case ObjectKind::image:
        json["image"] = png(object.image);
        json["transform"] = transform(object.transform);
        break;
    default:
        break;
    }
    if (object.hasPaint()) {
        json["fill"] = encode(object.fill);
        json["stroke"] = encode(object.stroke);
    }
    return json;
}

VectorObject decodeObject(const QJsonObject &json)
{
    VectorObject object;
    object.id = QUuid::fromString(json["id"].toString());
    if (object.id.isNull())
        throw CodecError("an object has no id");
    const auto kind = objectKind(json["kind"].toString());
    if (!kind)
        throw CodecError("unknown object kind");
    object.kind = *kind;
    object.name = json["name"].toString();
    if (json.contains("parent")) {
        const QUuid parent = QUuid::fromString(json["parent"].toString());
        if (parent.isNull())
            throw CodecError("an object's parent is not an id");
        object.parentID = parent;
    }
    object.isVisible = !json["hidden"].toBool();
    object.isLocked = json["locked"].toBool();
    object.isExpanded = !json["collapsed"].toBool();
    object.opacity = std::clamp(json["opacity"].toDouble(1), 0.0, 1.0);
    object.blendMode = layerBlendMode(json["blendMode"].toString()).value_or(LayerBlendMode::normal);
    if (json.contains("layerColor"))
        object.layerColor = readColor(json["layerColor"]);
    object.isClipGroup = json["clip"].toBool();
    object.transform = readTransform(json["transform"]);
    if (object.kind == ObjectKind::path)
        object.path = decodePath(json["path"].toObject());
    if (object.kind == ObjectKind::text) {
        const QJsonObject text = json["text"].toObject();
        object.text.text = text["string"].toString();
        object.text.family = text["family"].toString(object.text.family);
        object.text.size = std::max(0.1, text["size"].toDouble(object.text.size));
        object.text.bold = text["bold"].toBool();
        object.text.italic = text["italic"].toBool();
        object.text.alignment = textAlignment(text["alignment"].toString()).value_or(TextAlignment::left);
        object.text.leading = text["leading"].toDouble(object.text.leading);
        object.text.tracking = text["tracking"].toDouble();
    }
    if (object.kind == ObjectKind::image) {
        object.image = readPng(json["image"]);
        if (object.image.isNull())
            throw CodecError("a placed image could not be decoded");
    }
    if (json.contains("fill"))
        object.fill = decodePaint(json["fill"].toObject());
    if (json.contains("stroke"))
        object.stroke = decodeStroke(json["stroke"].toObject());
    return object;
}

QJsonArray encode(const std::vector<VectorObject> &objects)
{
    QJsonArray array;
    for (const VectorObject &object : objects)
        array.append(encode(object));
    return array;
}

std::vector<VectorObject> decodeObjects(const QJsonArray &json)
{
    std::vector<VectorObject> objects;
    for (const QJsonValue &value : json)
        objects.push_back(decodeObject(value.toObject()));
    return objects;
}

QJsonObject encode(const VectorDocument &document)
{
    return {{"format", "omaillustrator"}, {"version", version},
            {"width", document.size.width()}, {"height", document.size.height()},
            {"background", color(document.background)}, {"objects", encode(document.objects)}};
}

VectorDocument decode(const QJsonObject &json)
{
    if (json["format"].toString() != QLatin1String("omaillustrator"))
        throw CodecError("not an OmaIllustrator document");
    if (json["version"].toInt() < 1 || json["version"].toInt() > version)
        throw CodecError("made by a newer OmaIllustrator");
    VectorDocument document;
    document.size = {json["width"].toDouble(), json["height"].toDouble()};
    if (!(document.size.width() > 0 && document.size.height() > 0) || document.size.width() > 1e6 || document.size.height() > 1e6)
        throw CodecError("the artboard size is out of range");
    document.background = readColor(json["background"], Qt::white);
    document.objects = decodeObjects(json["objects"].toArray());
    // Every parent must exist, come first, and be a container; ids are unique.
    std::set<QUuid> seen;
    for (const VectorObject &object : document.objects) {
        if (!seen.insert(object.id).second)
            throw CodecError("two objects share an id");
        if (object.parentID) {
            const VectorObject *parent = nullptr;
            for (const VectorObject &candidate : document.objects) {
                if (candidate.id == *object.parentID)
                    parent = &candidate;
                if (&candidate == &object)
                    break;
            }
            if (!parent || !parent->isContainer())
                throw CodecError("an object's parent is missing");
        } else if (object.kind != ObjectKind::layer) {
            throw CodecError("an object sits outside every layer");
        }
    }
    if (document.layers().empty())
        document = VectorDocument::blank(document.size);
    return document;
}
}
