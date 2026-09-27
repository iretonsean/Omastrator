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
    if (paint.isHidden)
        json["hidden"] = true;
    if (paint.opacity != 1)
        json["opacity"] = paint.opacity;
    if (paint.blendMode != LayerBlendMode::normal)
        json["blendMode"] = rawValue(paint.blendMode);
    if (!paint.swatchId.isEmpty())
        json["swatch"] = paint.swatchId;
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
    paint.isHidden = json["hidden"].toBool();
    paint.opacity = std::clamp(json["opacity"].toDouble(1), 0.0, 1.0);
    paint.blendMode = layerBlendMode(json["blendMode"].toString()).value_or(LayerBlendMode::normal);
    paint.swatchId = json["swatch"].toString();
    return paint;
}

QJsonObject encode(const StrokeStyle &stroke)
{
    QJsonArray dashes;
    for (double dash : stroke.dashes)
        dashes.append(dash);
    QJsonObject json{{"paint", encode(stroke.paint)}, {"width", stroke.width}, {"cap", rawValue(stroke.cap)},
                     {"join", rawValue(stroke.join)}, {"miterLimit", stroke.miterLimit}, {"dashes", dashes}};
    if (stroke.alignment != StrokeAlignment::center)
        json["align"] = rawValue(stroke.alignment);
    if (stroke.startArrow != Arrowhead::none)
        json["startArrow"] = rawValue(stroke.startArrow);
    if (stroke.endArrow != Arrowhead::none)
        json["endArrow"] = rawValue(stroke.endArrow);
    if (stroke.arrowScale != 100)
        json["arrowScale"] = stroke.arrowScale;
    if (stroke.alignDashes)
        json["alignDashes"] = true;
    return json;
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
    stroke.alignment = strokeAlignment(json["align"].toString());
    stroke.startArrow = arrowhead(json["startArrow"].toString());
    stroke.endArrow = arrowhead(json["endArrow"].toString());
    stroke.arrowScale = std::clamp(json["arrowScale"].toDouble(100), 1.0, 1000.0);
    stroke.alignDashes = json["alignDashes"].toBool();
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

QJsonObject encode(const TextContent &text)
{
    QJsonObject json{{"string", text.text}, {"family", text.family}, {"style", text.style}, {"size", text.size},
                     {"alignment", rawValue(text.alignment)}, {"trackingEm", text.tracking}};
    if (text.leading)
        json["leadingPt"] = *text.leading;
    if (text.kerning != TextKerning::metrics)
        json["kerning"] = rawValue(text.kerning);
    if (!text.kerns.empty()) {
        QJsonObject kerns;
        for (const auto &[at, kern] : text.kerns)
            kerns[QString::number(at)] = kern;
        json["kerns"] = kerns;
    }
    const auto optional = [&json](const char *key, double value, double otherwise) {
        if (value != otherwise)
            json[QLatin1String(key)] = value;
    };
    optional("horizontalScale", text.horizontalScale, 100);
    optional("verticalScale", text.verticalScale, 100);
    optional("baselineShift", text.baselineShift, 0);
    optional("leftIndent", text.leftIndent, 0);
    optional("rightIndent", text.rightIndent, 0);
    optional("firstLineIndent", text.firstLineIndent, 0);
    optional("spaceBefore", text.spaceBefore, 0);
    optional("spaceAfter", text.spaceAfter, 0);
    if (text.textCase != TextCase::normal)
        json["case"] = rawValue(text.textCase);
    if (text.underline)
        json["underline"] = true;
    if (text.strikethrough)
        json["strikethrough"] = true;
    if (text.area)
        json["area"] = QJsonArray{text.area->width(), text.area->height()};
    return json;
}

TextContent decodeText(const QJsonObject &json)
{
    TextContent text;
    text.text = json["string"].toString();
    text.family = json["family"].toString(text.family);
    text.size = std::max(0.1, json["size"].toDouble(text.size));
    text.alignment = textAlignment(json["alignment"].toString()).value_or(TextAlignment::left);
    // Version 1 kept bold and italic flags, tracking in pt and leading as a multiple of the size.
    if (json.contains("style"))
        text.style = json["style"].toString(text.style);
    else
        text.style = TextContent::styleFor(text.family, json["bold"].toBool() ? 700 : 400, json["italic"].toBool());
    if (json.contains("trackingEm"))
        text.tracking = json["trackingEm"].toDouble();
    else
        text.tracking = json["tracking"].toDouble() / text.size * 1000;
    if (json.contains("leadingPt")) {
        text.leading = std::max(0.0, json["leadingPt"].toDouble());
    } else if (json.contains("leading")) {
        const double multiple = json["leading"].toDouble(1.2);
        if (std::abs(multiple - 1.2) > 1e-9)
            text.leading = multiple * text.size;
    }
    text.kerning = textKerning(json["kerning"].toString()).value_or(TextKerning::metrics);
    const QJsonObject kerns = json["kerns"].toObject();
    for (auto kern = kerns.begin(); kern != kerns.end(); ++kern) {
        bool number = false;
        const int at = kern.key().toInt(&number);
        if (number && at >= 0 && kern.value().isDouble())
            text.kerns[at] = kern.value().toDouble();
    }
    text.horizontalScale = std::clamp(json["horizontalScale"].toDouble(100), 1.0, 10000.0);
    text.verticalScale = std::clamp(json["verticalScale"].toDouble(100), 1.0, 10000.0);
    text.baselineShift = json["baselineShift"].toDouble();
    text.leftIndent = json["leftIndent"].toDouble();
    text.rightIndent = json["rightIndent"].toDouble();
    text.firstLineIndent = json["firstLineIndent"].toDouble();
    text.spaceBefore = json["spaceBefore"].toDouble();
    text.spaceAfter = json["spaceAfter"].toDouble();
    text.textCase = textCase(json["case"].toString()).value_or(TextCase::normal);
    text.underline = json["underline"].toBool();
    text.strikethrough = json["strikethrough"].toBool();
    const QJsonArray area = json["area"].toArray();
    if (area.size() == 2 && area[0].toDouble() > 0)
        text.area = QSizeF(area[0].toDouble(), std::max(0.0, area[1].toDouble()));
    return text;
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
        json["text"] = encode(object.text);
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
        // The stack past one of each; files without these keys read as they always did.
        QJsonArray fills, strokes;
        for (const Paint &fill : object.extraFills)
            fills.append(encode(fill));
        for (const StrokeStyle &stroke : object.extraStrokes)
            strokes.append(encode(stroke));
        if (!fills.isEmpty())
            json["moreFills"] = fills;
        if (!strokes.isEmpty())
            json["moreStrokes"] = strokes;
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
        object.text = decodeText(json["text"].toObject());
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
    for (const QJsonValue &fill : json["moreFills"].toArray())
        object.extraFills.push_back(decodePaint(fill.toObject()));
    for (const QJsonValue &stroke : json["moreStrokes"].toArray())
        object.extraStrokes.push_back(decodeStroke(stroke.toObject()));
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
    return {{"format", "omastrator"}, {"version", version},
            {"width", document.size.width()}, {"height", document.size.height()},
            {"background", color(document.background)}, {"objects", encode(document.objects)}};
}

VectorDocument decode(const QJsonObject &json)
{
    if (json["format"].toString() != QLatin1String("omastrator"))
        throw CodecError("not an Omastrator document");
    if (json["version"].toInt() < 1 || json["version"].toInt() > version)
        throw CodecError("made by a newer Omastrator");
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
