#include "Document/DocumentCodec.h"
#include <QBuffer>
#include <QByteArray>
#include <QJsonValue>
#include <algorithm>
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

QJsonValue id(const QUuid &value)
{
    return value.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(value.toString(QUuid::WithoutBraces));
}

// A format's keys where it differs from `against`: a run against its object, the object against the defaults.
void writeCharacter(QJsonObject &json, const CharacterFormat &format, const CharacterFormat &against)
{
    if (format.family != against.family)
        json["family"] = format.family;
    if (format.style != against.style)
        json["style"] = format.style;
    if (format.size != against.size)
        json["size"] = format.size;
    if (format.tracking != against.tracking)
        json["trackingEm"] = format.tracking;
    if (format.baselineShift != against.baselineShift)
        json["baselineShift"] = format.baselineShift;
    if (format.textCase != against.textCase)
        json["case"] = rawValue(format.textCase);
    if (format.underline != against.underline)
        json["underline"] = format.underline;
    if (format.strikethrough != against.strikethrough)
        json["strikethrough"] = format.strikethrough;
    if (format.features != against.features) {
        QJsonObject features;
        for (const auto &[tag, value] : format.features)
            features[tag] = value;
        json["features"] = features;
    }
    if (format.fill != against.fill)
        json["fill"] = format.fill ? QJsonValue(color(*format.fill)) : QJsonValue(QJsonValue::Null);
    if (format.characterStyle != against.characterStyle)
        json["characterStyle"] = id(format.characterStyle);
}

// Keys left out keep what `format` already says.
CharacterFormat readCharacter(const QJsonObject &json, CharacterFormat format)
{
    format.family = json["family"].toString(format.family);
    format.style = json["style"].toString(format.style);
    if (json.contains("size"))
        format.size = std::max(0.1, json["size"].toDouble(format.size));
    format.tracking = json["trackingEm"].toDouble(format.tracking);
    format.baselineShift = json["baselineShift"].toDouble(format.baselineShift);
    if (json.contains("case"))
        format.textCase = textCase(json["case"].toString()).value_or(format.textCase);
    format.underline = json["underline"].toBool(format.underline);
    format.strikethrough = json["strikethrough"].toBool(format.strikethrough);
    if (json.contains("features")) {
        format.features.clear();
        const QJsonObject features = json["features"].toObject();
        for (auto feature = features.begin(); feature != features.end(); ++feature) {
            if (feature.key().size() == 4 && feature.value().isDouble())
                format.features[feature.key()] = feature.value().toInt();
        }
    }
    if (json.contains("fill")) {
        const QColor fill = readColor(json["fill"], QColor());
        format.fill = fill.isValid() ? std::optional<QColor>(fill) : std::nullopt;
    }
    if (json.contains("characterStyle"))
        format.characterStyle = QUuid::fromString(json["characterStyle"].toString());
    return format;
}

void writeParagraph(QJsonObject &json, const ParagraphFormat &format, const ParagraphFormat &against)
{
    if (format.alignment != against.alignment)
        json["alignment"] = rawValue(format.alignment);
    if (format.leading != against.leading)
        json["leadingPt"] = format.leading ? QJsonValue(*format.leading) : QJsonValue(QJsonValue::Null);
    const auto number = [&json](const char *key, double value, double otherwise) {
        if (value != otherwise)
            json[QLatin1String(key)] = value;
    };
    number("leftIndent", format.leftIndent, against.leftIndent);
    number("rightIndent", format.rightIndent, against.rightIndent);
    number("firstLineIndent", format.firstLineIndent, against.firstLineIndent);
    number("spaceBefore", format.spaceBefore, against.spaceBefore);
    number("spaceAfter", format.spaceAfter, against.spaceAfter);
    if (format.paragraphStyle != against.paragraphStyle)
        json["paragraphStyle"] = id(format.paragraphStyle);
}

ParagraphFormat readParagraph(const QJsonObject &json, ParagraphFormat format)
{
    if (json.contains("alignment"))
        format.alignment = textAlignment(json["alignment"].toString()).value_or(format.alignment);
    if (json.contains("leadingPt"))
        format.leading = json["leadingPt"].isDouble() ? std::optional<double>(std::max(0.0, json["leadingPt"].toDouble())) : std::nullopt;
    format.leftIndent = json["leftIndent"].toDouble(format.leftIndent);
    format.rightIndent = json["rightIndent"].toDouble(format.rightIndent);
    format.firstLineIndent = json["firstLineIndent"].toDouble(format.firstLineIndent);
    format.spaceBefore = json["spaceBefore"].toDouble(format.spaceBefore);
    format.spaceAfter = json["spaceAfter"].toDouble(format.spaceAfter);
    if (json.contains("paragraphStyle"))
        format.paragraphStyle = QUuid::fromString(json["paragraphStyle"].toString());
    return format;
}

QJsonObject encodeStyle(const TextStyle &style)
{
    QJsonObject character{{"family", style.character.family}, {"style", style.character.style}, {"size", style.character.size}};
    writeCharacter(character, style.character, CharacterFormat{});
    QJsonObject json{{"id", style.id.toString(QUuid::WithoutBraces)}, {"name", style.name},
                     {"kind", style.kind == TextStyleKind::paragraph ? "paragraph" : "character"}, {"character", character}};
    if (style.kind == TextStyleKind::paragraph) {
        QJsonObject paragraph;
        writeParagraph(paragraph, style.paragraph, ParagraphFormat{});
        json["paragraph"] = paragraph;
    }
    if (!style.typeToken.isEmpty())
        json["typeToken"] = style.typeToken;
    return json;
}

TextStyle decodeStyle(const QJsonObject &json)
{
    TextStyle style;
    style.id = QUuid::fromString(json["id"].toString());
    if (style.id.isNull())
        throw CodecError("a text style has no id");
    style.name = json["name"].toString();
    style.kind = json["kind"].toString() == QLatin1String("paragraph") ? TextStyleKind::paragraph : TextStyleKind::character;
    style.character = readCharacter(json["character"].toObject(), CharacterFormat{});
    style.paragraph = readParagraph(json["paragraph"].toObject(), ParagraphFormat{});
    style.typeToken = json["typeToken"].toString();
    return style;
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
    if (!paint.token.isEmpty())
        json["token"] = paint.token;
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
    paint.token = json["token"].toString();
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
    writeCharacter(json, text.character(), CharacterFormat{});
    writeParagraph(json, text.paragraph(), ParagraphFormat{});
    if (text.kerning != TextKerning::metrics)
        json["kerning"] = rawValue(text.kerning);
    if (!text.kerns.empty()) {
        QJsonObject kerns;
        for (const auto &[at, kern] : text.kerns)
            kerns[QString::number(at)] = kern;
        json["kerns"] = kerns;
    }
    if (text.horizontalScale != 100)
        json["horizontalScale"] = text.horizontalScale;
    if (text.verticalScale != 100)
        json["verticalScale"] = text.verticalScale;
    if (text.area)
        json["area"] = QJsonArray{text.area->width(), text.area->height()};
    if (!text.runs.empty()) {
        QJsonArray runs;
        for (const TextRun &run : text.runs) {
            QJsonObject entry{{"start", run.start}, {"length", run.length}};
            writeCharacter(entry, run.format, text.character());
            runs.append(entry);
        }
        json["runs"] = runs;
    }
    if (!text.paragraphFormats.empty()) {
        QJsonArray paragraphs;
        for (const auto &[index, format] : text.paragraphFormats) {
            QJsonObject entry{{"index", index}};
            writeParagraph(entry, format, text.paragraph());
            paragraphs.append(entry);
        }
        json["paragraphs"] = paragraphs;
    }
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
    // Version 3: features, styles, runs and paragraphs.
    CharacterFormat own = readCharacter(json, text.character());
    own.fill.reset();
    text.character() = own;
    text.paragraphStyle = QUuid::fromString(json["paragraphStyle"].toString());
    for (const QJsonValue &value : json["runs"].toArray()) {
        const QJsonObject entry = value.toObject();
        const int start = entry["start"].toInt(-1), length = entry["length"].toInt();
        if (start < 0 || length <= 0 || start + length > text.text.size())
            continue;
        text.runs.push_back({start, length, readCharacter(entry, text.character())});
    }
    std::sort(text.runs.begin(), text.runs.end(), [](const TextRun &a, const TextRun &b) { return a.start < b.start; });
    for (const QJsonValue &value : json["paragraphs"].toArray()) {
        const QJsonObject entry = value.toObject();
        text.paragraphFormats[entry["index"].toInt(-1)] = readParagraph(entry, text.paragraph());
    }
    text.normalize();
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
    if (!object.liftedFrom.isEmpty())
        json["liftedFrom"] = object.liftedFrom;
    switch (object.kind) {
    case ObjectKind::path:
        json["path"] = encode(object.path);
        // Only a shape still live is worth keeping.
        if (const LiveRectangle *shape = object.liveShape())
            json["shape"] = encode(*shape);
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
    if (!object.tokenRefs.empty()) {
        QJsonObject refs;
        for (const auto &[key, token] : object.tokenRefs)
            refs[key] = token;
        json["tokens"] = refs;
    }
    if (object.component)
        json["component"] = Components::encode(*object.component);
    if (object.instance)
        json["instance"] = Components::encode(*object.instance);
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
    object.liftedFrom = json["liftedFrom"].toString();
    object.transform = readTransform(json["transform"]);
    if (object.kind == ObjectKind::path) {
        object.path = decodePath(json["path"].toObject());
        if (json.contains("shape"))
            object.shape = decodeShape(json["shape"].toObject());
        // A shape that no longer makes this path was edited elsewhere.
        if (!object.liveShape())
            object.shape.reset();
    }
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
    const QJsonObject refs = json["tokens"].toObject();
    for (auto ref = refs.begin(); ref != refs.end(); ++ref)
        object.tokenRefs[ref.key()] = ref.value().toString();
    // Only groups hold components and instances.
    if (object.kind == ObjectKind::group && json.contains("component"))
        object.component = Components::decodeComponent(json["component"].toObject());
    if (object.kind == ObjectKind::group && json.contains("instance")) {
        object.instance = Components::decodeInstance(json["instance"].toObject());
        if (object.instance->master.isNull())
            object.instance.reset();
    }
    return object;
}

QJsonObject encode(const LiveRectangle &shape)
{
    QJsonArray radii, styles;
    for (int corner = 0; corner < 4; ++corner) {
        radii.append(shape.radii[size_t(corner)]);
        styles.append(rawValue(shape.styles[size_t(corner)]));
    }
    return {{"kind", "rectangle"}, {"rect", QJsonArray{shape.rect.x(), shape.rect.y(), shape.rect.width(), shape.rect.height()}},
            {"placement", transform(shape.placement)}, {"radii", radii}, {"corners", styles}};
}

std::optional<LiveRectangle> decodeShape(const QJsonObject &json)
{
    const QJsonArray rect = json["rect"].toArray();
    if (json["kind"].toString() != QLatin1String("rectangle") || rect.size() != 4)
        return std::nullopt;
    LiveRectangle shape;
    shape.rect = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble());
    shape.placement = readTransform(json["placement"]);
    const QJsonArray radii = json["radii"].toArray(), styles = json["corners"].toArray();
    for (int corner = 0; corner < 4; ++corner) {
        shape.radii[size_t(corner)] = std::max(0.0, radii.at(corner).toDouble());
        shape.styles[size_t(corner)] = cornerStyle(styles.at(corner).toString()).value_or(CornerStyle::round);
    }
    if (!std::isfinite(shape.rect.width()) || !std::isfinite(shape.rect.height()))
        return std::nullopt;
    return shape;
}

QJsonArray encode(const std::vector<Guide> &guides)
{
    QJsonArray array;
    for (const Guide &guide : guides)
        array.append(QJsonObject{{"axis", guide.orientation == Qt::Horizontal ? "horizontal" : "vertical"}, {"position", guide.position}});
    return array;
}

std::vector<Guide> decodeGuides(const QJsonArray &json)
{
    std::vector<Guide> guides;
    for (const QJsonValue &value : json) {
        const QJsonObject guide = value.toObject();
        const double position = guide["position"].toDouble(std::nan(""));
        if (!std::isfinite(position))
            continue;
        guides.push_back({guide["axis"].toString() == QLatin1String("vertical") ? Qt::Vertical : Qt::Horizontal, position});
    }
    return guides;
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
    QJsonObject json{{"format", "omastrator"}, {"version", version},
                     {"width", document.size.width()}, {"height", document.size.height()},
                     {"background", color(document.background)}, {"objects", encode(document.objects)},
                     {"guides", encode(document.guides)}};
    if (!document.textStyles.empty()) {
        QJsonArray styles;
        for (const TextStyle &style : document.textStyles)
            styles.append(encodeStyle(style));
        json["textStyles"] = styles;
    }
    if (!document.tokens.empty())
        json["tokens"] = DesignTokens::encode(document.tokens);
    if (!document.tokenModes.isEmpty()) {
        json["tokenModes"] = QJsonArray::fromStringList(document.tokenModes);
        json["tokenMode"] = document.tokenMode;
    }
    return json;
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
    document.guides = decodeGuides(json["guides"].toArray());
    for (const QJsonValue &style : json["textStyles"].toArray())
        document.textStyles.push_back(decodeStyle(style.toObject()));
    // Version 4: the design system. Earlier files have none.
    document.tokens = DesignTokens::decode(json["tokens"].toArray());
    for (const QJsonValue &mode : json["tokenModes"].toArray()) {
        if (!mode.toString().isEmpty() && !document.tokenModes.contains(mode.toString()))
            document.tokenModes.append(mode.toString());
    }
    document.tokenMode = document.tokenModes.contains(json["tokenMode"].toString()) ? json["tokenMode"].toString() : document.tokenModes.value(0);
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
