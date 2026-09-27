#include "Agent/AgentEdits.h"
#include "Agent/AgentParams.h"
#include "Agent/AgentTools.h"
#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Document/ImageTrace.h"
#include "IO/SvgImporter.h"
#include <QDir>
#include <QJsonArray>
#include <QTemporaryFile>

using AgentProtocol::Error;
using namespace AgentParams;

namespace {
// "none", a colour name or hex, or DocumentCodec paint JSON.
Paint paint(const QJsonValue &value, const QString &key)
{
    if (value.isObject())
        return DocumentCodec::decodePaint(value.toObject());
    if (!value.isString())
        fail(QStringLiteral("“%1” must be \"none\", a colour such as \"#ff6600\", or paint JSON.").arg(key));
    if (value.toString() == QLatin1String("none"))
        return Paint::none();
    const QColor color = QColor::fromString(value.toString());
    if (!color.isValid())
        fail(QStringLiteral("“%1” is not a colour: %2.").arg(key, value.toString()));
    return Paint::solid(color);
}

// The stroke with the given fields changed, the rest kept.
StrokeStyle mergedStroke(const StrokeStyle &stroke, const QJsonObject &changes)
{
    QJsonObject merged = DocumentCodec::encode(stroke);
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const QString key = it.key();
        if (key == QLatin1String("color") || key == QLatin1String("paint")) {
            merged["paint"] = DocumentCodec::encode(paint(it.value(), QStringLiteral("stroke.") + key));
        } else if (key == QLatin1String("width") || key == QLatin1String("miterLimit")) {
            if (!it.value().isDouble() || it.value().toDouble() < 0)
                fail(QStringLiteral("“stroke.%1” must be a number, zero or more.").arg(key));
            merged[key] = it.value();
        } else if (key == QLatin1String("cap") || key == QLatin1String("join")) {
            const QStringList allowed = key == QLatin1String("cap") ? QStringList{"butt", "square", "round"} : QStringList{"miter", "bevel", "round"};
            if (!allowed.contains(it.value().toString()))
                fail(QStringLiteral("“stroke.%1” must be one of: %2.").arg(key, allowed.join(QStringLiteral(", "))));
            merged[key] = it.value();
        } else if (key == QLatin1String("dashes")) {
            if (!it.value().isArray())
                fail(QStringLiteral("“stroke.dashes” must be an array of lengths."));
            merged[key] = it.value();
        } else {
            fail(QStringLiteral("“stroke” has no field “%1”. Use paint, color, width, cap, join, miterLimit or dashes.").arg(key));
        }
    }
    return DocumentCodec::decodeStroke(merged);
}

std::optional<QUuid> openLayerOrFail(const VectorDocument &document, std::optional<QUuid> preferred)
{
    const auto layer = AgentEdits::openLayer(document, preferred);
    if (!layer)
        throw Error(AgentProtocol::busy, QStringLiteral("Every layer is locked or hidden, so there is nowhere to add art."));
    return layer;
}

VectorDocument parseSvg(const QString &svg)
{
    try {
        return SvgImporter::parse(svg.toUtf8());
    } catch (const FileError &failure) {
        fail(QStringLiteral("The SVG could not be imported: %1").arg(failure.message()));
    }
}
}

QJsonObject AgentTools::insertSvg(const QJsonObject &params)
{
    const QString svg = requiredString(params, QStringLiteral("svg"));
    const QString name = string(params, QStringLiteral("name")).value_or(QStringLiteral("Generated Art"));
    const auto at = point(params, QStringLiteral("at"));
    const auto fitSize = numbers(params, QStringLiteral("fit"), 2);
    if (fitSize && !((*fitSize)[0] > 0 && (*fitSize)[1] > 0))
        fail(QStringLiteral("“fit” must be a width and height above zero."));
    const VectorDocument art = parseSvg(svg);
    VectorDocument edited = draft();
    const auto layer = openLayerOrFail(edited, session().activeLayer());
    const QUuid id = AgentEdits::insertArt(edited, art, name, *layer);
    if (fitSize) {
        const QSizeF size((*fitSize)[0], (*fitSize)[1]);
        const QPointF corner = at.value_or(QPointF((edited.size.width() - size.width()) / 2, (edited.size.height() - size.height()) / 2));
        AgentEdits::fit(edited, id, QRectF(corner, size));
    } else if (at) {
        const QPointF shift = *at - edited.bounds(id).topLeft();
        edited.transform(id, QTransform::fromTranslate(shift.x(), shift.y()));
    } else if (boolean(params, QStringLiteral("center"), false)) {
        const QPointF shift = QPointF(edited.size.width() / 2, edited.size.height() / 2) - edited.bounds(id).center();
        edited.transform(id, QTransform::fromTranslate(shift.x(), shift.y()));
    }
    propose(QStringLiteral("Insert SVG"), edited, {id});
    return {{"id", idString(id)}, {"bounds", rect(edited.bounds(id))}, {"children", int(edited.descendants(id).size())}};
}

QJsonObject AgentTools::setStyle(const QJsonObject &params)
{
    const std::vector<QUuid> requested = targets(params);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    std::optional<Paint> fill;
    if (params.contains("fill"))
        fill = paint(params["fill"], QStringLiteral("fill"));
    const QJsonValue stroke = params["stroke"];
    if (!stroke.isUndefined() && !stroke.isObject())
        fail(QStringLiteral("“stroke” must be an object such as {\"color\": \"#000\", \"width\": 2}."));
    const auto opacity = number(params, QStringLiteral("opacity"));
    if (opacity && (*opacity < 0 || *opacity > 1))
        fail(QStringLiteral("“opacity” must be 0 to 1."));
    std::optional<LayerBlendMode> blend;
    if (const auto name = string(params, QStringLiteral("blendMode"))) {
        // The camelCase names, or the ones document_get shows ("Soft Light").
        static const QStringList camel{"normal", "multiply", "screen", "overlay", "softLight", "darken", "lighten",
                                       "difference", "colorDodge", "colorBurn", "hue", "saturation", "color", "luminosity"};
        const qsizetype index = camel.indexOf(*name);
        blend = index >= 0 ? std::optional(allLayerBlendModes[size_t(index)]) : layerBlendMode(*name);
        if (!blend)
            fail(QStringLiteral("“blendMode” %1 is not a blend mode. See the schema for the list.").arg(*name));
    }
    if (!fill && !stroke.isObject() && !opacity && !blend)
        fail(QStringLiteral("Pass at least one of fill, stroke, opacity or blendMode."));
    for (const QUuid &id : AgentEdits::leaves(edited, ids)) {
        VectorObject *object = edited.find(id);
        if (!object->hasPaint() || edited.isEffectivelyLocked(id))
            continue;
        if (fill)
            object->fill = *fill;
        if (stroke.isObject())
            object->stroke = mergedStroke(object->stroke, stroke.toObject());
    }
    for (const QUuid &id : ids) {
        VectorObject *object = edited.find(id);
        if (opacity)
            object->opacity = *opacity;
        if (blend)
            object->blendMode = *blend;
    }
    propose(QStringLiteral("Set Style"), edited, ids);
    return {{"ids", idArray(ids)}};
}

QJsonObject AgentTools::transform(const QJsonObject &params)
{
    const std::vector<QUuid> requested = targets(params);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    const auto matrix = numbers(params, QStringLiteral("matrix"), 6);
    const auto translate = point(params, QStringLiteral("translate"));
    const auto rotate = number(params, QStringLiteral("rotate"));
    std::optional<QPointF> scale;
    if (params["scale"].isDouble())
        scale = QPointF(params["scale"].toDouble(), params["scale"].toDouble());
    else if (params.contains("scale"))
        scale = point(params, QStringLiteral("scale"));
    if (scale && (scale->x() == 0 || scale->y() == 0 || !std::isfinite(scale->x()) || !std::isfinite(scale->y())))
        fail(QStringLiteral("“scale” must not be zero."));
    QTransform change;
    if (matrix) {
        if (translate || rotate || scale)
            fail(QStringLiteral("Pass either “matrix”, or translate, rotate and scale, not both."));
        const std::vector<double> &m = *matrix;
        change = QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
        if (!change.isInvertible())
            fail(QStringLiteral("“matrix” flattens the objects to nothing; its determinant is zero."));
    } else {
        if (!translate && !rotate && !scale)
            fail(QStringLiteral("Pass matrix, or at least one of translate, rotate and scale."));
        const QPointF origin = point(params, QStringLiteral("origin")).value_or(edited.bounds(ids).center());
        const QPointF factor = scale.value_or(QPointF(1, 1));
        const QPointF offset = translate.value_or(QPointF());
        change = QTransform::fromTranslate(-origin.x(), -origin.y()) * QTransform::fromScale(factor.x(), factor.y())
            * QTransform().rotate(rotate.value_or(0)) * QTransform::fromTranslate(origin.x() + offset.x(), origin.y() + offset.y());
    }
    for (const QUuid &id : ids)
        edited.transform(id, change);
    propose(QStringLiteral("Transform"), edited, ids);
    return {{"ids", idArray(ids)}, {"bounds", rect(edited.bounds(ids))}};
}

QJsonObject AgentTools::arrange(const QJsonObject &params)
{
    const int order = *choice(params, QStringLiteral("order"),
                              {QStringLiteral("bringToFront"), QStringLiteral("bringForward"), QStringLiteral("sendBackward"), QStringLiteral("sendToBack")}, true);
    const std::vector<QUuid> requested = targets(params);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    AgentEdits::arrange(edited, ids, ArrangeOrder(order));
    propose(QStringLiteral("Arrange"), edited, ids);
    return {{"ids", idArray(ids)}};
}

QJsonObject AgentTools::align(const QJsonObject &params)
{
    const int edge = *choice(params, QStringLiteral("edge"),
                             {QStringLiteral("left"), QStringLiteral("horizontalCenter"), QStringLiteral("right"), QStringLiteral("top"),
                              QStringLiteral("verticalCenter"), QStringLiteral("bottom")}, true);
    const int target = choice(params, QStringLiteral("target"), {QStringLiteral("selection"), QStringLiteral("artboard")}).value_or(0);
    const std::vector<QUuid> requested = targets(params);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    AgentEdits::align(edited, ids, AlignEdge(edge), AlignTarget(target));
    propose(QStringLiteral("Align"), edited, ids);
    return {{"ids", idArray(ids)}, {"bounds", rect(edited.bounds(ids))}};
}

QJsonObject AgentTools::distribute(const QJsonObject &params)
{
    const int axis = *choice(params, QStringLiteral("axis"), {QStringLiteral("horizontal"), QStringLiteral("vertical")}, true);
    const std::vector<QUuid> requested = targets(params);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    AgentEdits::distribute(edited, ids, DistributeAxis(axis));
    propose(QStringLiteral("Distribute"), edited, ids);
    return {{"ids", idArray(ids)}};
}

QJsonObject AgentTools::group(const QJsonObject &params)
{
    const std::vector<QUuid> requested = targets(params, true);
    const QString name = string(params, QStringLiteral("name")).value_or(QString());
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    const QUuid id = AgentEdits::group(edited, ids, name);
    propose(QStringLiteral("Group"), edited, {id});
    return {{"id", idString(id)}};
}

QJsonObject AgentTools::ungroup(const QJsonObject &params)
{
    const std::vector<QUuid> requested = targets(params, true);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    if (std::none_of(ids.begin(), ids.end(), [&](const QUuid &id) { return edited.find(id)->kind == ObjectKind::group; }))
        fail(QStringLiteral("None of those objects is a group."));
    const std::vector<QUuid> released = AgentEdits::ungroup(edited, ids);
    propose(QStringLiteral("Ungroup"), edited, released);
    return {{"ids", idArray(released)}};
}

QJsonObject AgentTools::pathfinder(const QJsonObject &params)
{
    const int operation = *choice(params, QStringLiteral("operation"),
                                  {QStringLiteral("unite"), QStringLiteral("intersect"), QStringLiteral("minusFront"), QStringLiteral("exclude")}, true);
    const std::vector<QUuid> ids = targets(params, true);
    VectorDocument edited = draft();
    const auto result = AgentEdits::combine(edited, ids, BooleanOperation(operation));
    propose(QStringLiteral("Pathfinder"), edited, result ? std::vector<QUuid>{*result} : std::vector<QUuid>{});
    return {{"id", result ? QJsonValue(idString(*result)) : QJsonValue(QJsonValue::Null)},
            {"note", result ? QString() : QStringLiteral("The shapes don't overlap that way, so nothing is left.")}};
}

QJsonObject AgentTools::remove(const QJsonObject &params)
{
    const std::vector<QUuid> requested = targets(params, true);
    VectorDocument edited = draft();
    const std::vector<QUuid> ids = unlocked(edited, requested);
    edited.remove(ids);
    std::vector<QUuid> selection = session().selection();
    std::erase_if(selection, [&](const QUuid &id) { return !edited.find(id); });
    propose(QStringLiteral("Delete"), edited, selection);
    return {{"deleted", idArray(ids)}};
}

QJsonObject AgentTools::select(const QJsonObject &params)
{
    const auto given = ids(params);
    if (!given)
        fail(QStringLiteral("“ids” is required; pass [] to deselect."));
    std::vector<QUuid> chosen;
    if (!given->empty())
        chosen = targets(params, true);
    session().select(chosen);
    return {{"selection", idArray(session().selection())}};
}

QJsonObject AgentTools::updateObject(const QJsonObject &params)
{
    if (!params["object"].isObject())
        fail(QStringLiteral("“object” must be DocumentCodec object JSON, as document_get returns it."));
    VectorObject object = DocumentCodec::decodeObject(params["object"].toObject());
    VectorDocument edited = draft();
    VectorObject *existing = edited.find(object.id);
    if (!existing)
        fail(QStringLiteral("No object has the id %1.").arg(idString(object.id)));
    if (existing->kind == ObjectKind::layer || object.kind == ObjectKind::layer)
        fail(QStringLiteral("Layers can't be replaced; edit the objects in them."));
    if (existing->isContainer() != object.isContainer())
        fail(QStringLiteral("A group stays a group, and a path, text or image stays one of those."));
    if (edited.isEffectivelyLocked(object.id))
        fail(QStringLiteral("That object is locked. The user can unlock it in the Layers panel."));
    // Where it sits in the tree is the document's to say.
    object.parentID = existing->parentID;
    *existing = object;
    propose(QStringLiteral("Edit Object"), edited, {object.id});
    return {{"id", idString(object.id)}, {"bounds", rect(edited.bounds(object.id))}};
}

QJsonObject AgentTools::replaceObjects(const QJsonObject &params)
{
    const QString svg = requiredString(params, QStringLiteral("svg"));
    const QString name = string(params, QStringLiteral("name")).value_or(QStringLiteral("Replacement"));
    const bool fitting = boolean(params, QStringLiteral("fit"), true);
    const std::vector<QUuid> requested = targets(params, true);
    const VectorDocument art = parseSvg(svg);
    VectorDocument edited = draft();
    for (const QUuid &id : requested) {
        if (edited.isEffectivelyLocked(id))
            fail(QStringLiteral("%1 is locked. The user can unlock it in the Layers panel.").arg(idString(id)));
    }
    const std::vector<QUuid> ordered = AgentEdits::inOrder(edited, AgentEdits::roots(edited, requested));
    const QRectF bounds = edited.bounds(ordered);
    const QUuid top = ordered.back();
    const QUuid id = AgentEdits::insertArt(edited, art, name, *edited.find(top)->parentID, top);
    if (fitting)
        AgentEdits::fit(edited, id, bounds);
    edited.remove(ordered);
    propose(QStringLiteral("Replace"), edited, {id});
    return {{"id", idString(id)}, {"bounds", rect(edited.bounds(id))}, {"replaced", idArray(ordered)}};
}

QJsonObject AgentTools::traceImage(const QJsonObject &params)
{
    const int mode = *choice(params, QStringLiteral("mode"), {QStringLiteral("color"), QStringLiteral("blackAndWhite")}, true);
    const auto colors = number(params, QStringLiteral("colors"));
    if (colors && (*colors < 2 || *colors > 16 || *colors != std::floor(*colors)))
        fail(QStringLiteral("“colors” must be a whole number from 2 to 16."));
    VectorDocument edited = draft();
    std::optional<QUuid> imageID = uuid(params["id"], QStringLiteral("id"));
    if (!imageID) {
        std::vector<QUuid> images;
        for (const QUuid &leaf : AgentEdits::leaves(edited, session().selection())) {
            if (edited.find(leaf)->kind == ObjectKind::image)
                images.push_back(leaf);
        }
        if (images.size() != 1)
            fail(QStringLiteral("Select exactly one placed image, or pass its “id”."));
        imageID = images.front();
    }
    const VectorObject *image = edited.find(*imageID);
    if (!image || image->kind != ObjectKind::image)
        fail(QStringLiteral("%1 is not a placed image.").arg(idString(*imageID)));
    if (edited.isEffectivelyLocked(*imageID))
        fail(QStringLiteral("That image is locked. The user can unlock it in the Layers panel."));

    ImageTrace::Options options;
    options.colors = mode == 1 ? 1 : int(colors.value_or(6));
    // The original pixels, so the agent can compare its cleanup against them.
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("omastrator-trace-source-XXXXXX.png")));
    file.setAutoRemove(false);
    if (!file.open() || !image->image.save(&file, "PNG"))
        throw Error(AgentProtocol::fileError, QStringLiteral("Could not write the image for the agent to see."));
    const QString imagePath = file.fileName();
    const QSize pixels = image->image.size();
    const std::optional<QUuid> traced = ImageTrace::traceInPlace(edited, *imageID, options);
    if (!traced) {
        QFile::remove(imagePath);
        fail(QStringLiteral("The trace found no shapes. Try the other mode, or more colours."));
    }
    const QUuid groupID = *traced;
    const std::vector<QUuid> paths = edited.children(groupID);
    propose(QStringLiteral("Image Trace"), edited, {groupID});
    return {{"id", idString(groupID)}, {"ids", idArray(paths)}, {"bounds", rect(edited.bounds(groupID))},
            {"imagePath", imagePath}, {"imageSize", QJsonArray{pixels.width(), pixels.height()}}};
}
