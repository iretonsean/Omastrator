#include "Document/PathOperations.h"
#include "IO/FigmaMapperInternal.h"
#include "Logging.h"
#include <QSizeF>
#include <algorithm>
#include <cmath>

namespace FigmaMap {

void Context::warn(const QString &message)
{
    if (!warnings.contains(message))
        warnings << message;
}

namespace {
constexpr int maximumDepth = 256;

// Counts one level of nesting for as long as it lives; `tooDeep` says to stop.
struct DepthGuard {
    Context &ctx;
    bool tooDeep;
    explicit DepthGuard(Context &context) : ctx(context), tooDeep(++context.depth > maximumDepth)
    {
        if (tooDeep)
            ctx.warn(QStringLiteral("Layers nested deeper than %1 levels were left out.").arg(maximumDepth));
    }
    ~DepthGuard() { --ctx.depth; }
};

QSizeF nodeSize(const QVariantMap &node)
{
    const QVariantMap size = map(node, "size");
    if (!size.isEmpty())
        return {num(size, "x", 1), num(size, "y", 1)};
    return {1, 1};
}

// A star's or polygon's point count from the file: 3..1000, whatever the file claims.
int clampedCount(double count)
{
    return int(std::clamp(std::isfinite(count) ? count : 3.0, 3.0, 1000.0));
}

// A non-rectangle, non-ellipse shape's local (untransformed) outline: Kiwi's parametric
// fields when they're there, else the flattened geometry both sources can give (Kiwi's
// vector network blob, or REST's fillGeometry when ?geometry=paths was asked for).
VectorPath localShapePath(Context &ctx, const QVariantMap &node, const QString &type)
{
    const QSizeF size = nodeSize(node);
    if (type == QLatin1String("STAR") && node.contains(QStringLiteral("count"))) {
        const int points = clampedCount(num(node, "count", 5));
        const double outer = std::min(size.width(), size.height()) / 2;
        return Shapes::star({size.width() / 2, size.height() / 2}, outer, outer * num(node, "starInnerScale", 0.5), points);
    }
    if (type == QLatin1String("REGULAR_POLYGON") && node.contains(QStringLiteral("count"))) {
        const int sides = clampedCount(num(node, "count", 3));
        return Shapes::polygon({size.width() / 2, size.height() / 2}, std::min(size.width(), size.height()) / 2, sides);
    }
    if (type == QLatin1String("LINE"))
        return Shapes::line({0, 0}, {size.width(), size.height()});
    return decodeVectorNetwork(ctx, node);
}

// A boolean operation's own outline: its children combined, flattening any nesting.
// FIGMA.md: the model has no live boolean node, so this becomes one plain path.
QPainterPath booleanOutline(Context &ctx, const Guid &guid, const QTransform &parentTransform)
{
    const DepthGuard guard(ctx);
    if (guard.tooDeep)
        return {};
    const Node &node = ctx.tree.nodes.at(guid);
    const QString type = str(node.fields, "type");
    const QTransform own = matrix(node.fields.value(QStringLiteral("transform"))) * parentTransform;
    if (type == QLatin1String("GROUP") || type == QLatin1String("FRAME")) {
        QPainterPath result;
        for (const Guid &child : node.children)
            result |= booleanOutline(ctx, child, own);
        return result;
    }
    if (type == QLatin1String("BOOLEAN_OPERATION")) {
        std::vector<QPainterPath> parts;
        for (const Guid &child : node.children)
            parts.push_back(booleanOutline(ctx, child, own));
        static const std::map<QString, BooleanOperation> ops{{QStringLiteral("UNION"), BooleanOperation::unite},
                                                              {QStringLiteral("INTERSECT"), BooleanOperation::intersect},
                                                              {QStringLiteral("SUBTRACT"), BooleanOperation::minusFront},
                                                              {QStringLiteral("EXCLUDE"), BooleanOperation::exclude}};
        const auto op = ops.find(str(node.fields, "booleanOperation"));
        return parts.empty() ? QPainterPath() : combine(parts, op == ops.end() ? BooleanOperation::unite : op->second);
    }
    if (type == QLatin1String("RECTANGLE")) {
        const QSizeF size = nodeSize(node.fields);
        return Shapes::rectangle(QRectF(QPointF(0, 0), size), num(node.fields, "cornerRadius")).transformed(own).painterPath();
    }
    if (type == QLatin1String("ELLIPSE")) {
        const QSizeF size = nodeSize(node.fields);
        return Shapes::ellipse(QRectF(QPointF(0, 0), size)).transformed(own).painterPath();
    }
    return localShapePath(ctx, node.fields, type).transformed(own).painterPath();
}

// A node built for its own bounds; frames and rectangles share this box.
QRectF localBox(const QVariantMap &node)
{
    return QRectF(QPointF(0, 0), nodeSize(node));
}

std::array<double, 4> cornerRadii(const QVariantMap &node)
{
    const bool independent = boolean(node, "rectangleCornerRadiiIndependent");
    const double uniform = num(node, "cornerRadius");
    return {independent ? num(node, "rectangleTopLeftCornerRadius") : uniform, independent ? num(node, "rectangleTopRightCornerRadius") : uniform,
            independent ? num(node, "rectangleBottomRightCornerRadius") : uniform, independent ? num(node, "rectangleBottomLeftCornerRadius") : uniform};
}

void applyRectangle(Context &ctx, const QVariantMap &node, VectorObject &object, const QTransform &transform)
{
    LiveRectangle shape;
    shape.rect = localBox(node);
    shape.placement = transform;
    shape.radii = cornerRadii(node);
    if (num(node, "cornerSmoothing") > 0)
        ctx.warn(QStringLiteral("Corner smoothing was left out; corners came in sharp-cornered (no smoothing)."));
    object.shape = shape;
    object.path = shape.path();
}

void applyFrameBox(Context &ctx, const QVariantMap &node, VectorObject &object, const QTransform &transform)
{
    LiveRectangle shape;
    shape.rect = localBox(node);
    shape.placement = transform;
    shape.radii = cornerRadii(node);
    object.shape = shape;
    object.path = shape.path();
    object.clipsContent = node.contains(QStringLiteral("clipsContent")) ? boolean(node, "clipsContent", true) : !boolean(node, "frameMaskDisabled");
    Q_UNUSED(ctx);
}

QUuid insert(Context &ctx, VectorObject object, const Guid &guid, const QUuid &parent)
{
    const QUuid id = object.id;
    ctx.document.insert(std::move(object), parent);
    ctx.objectFor[guid] = id;
    return id;
}

void mapChildren(Context &ctx, const Guid &guid, const QUuid &parent, const QTransform &transform)
{
    for (const Guid &child : ctx.tree.nodes.at(guid).children)
        mapNode(ctx, child, parent, transform);
}
}

void applyCommon(Context &ctx, const QVariantMap &node, VectorObject &object)
{
    if (boolean(node, "mask"))
        ctx.warn(QStringLiteral("Masks aren’t supported yet; masked layers show without clipping."));
    object.name = str(node, "name", object.name);
    object.isVisible = boolean(node, "visible", true);
    object.isLocked = boolean(node, "locked", false);
    object.opacity = std::clamp(num(node, "opacity", 1), 0.0, 1.0);
    static const std::map<QString, LayerBlendMode> modes{
        {QStringLiteral("NORMAL"), LayerBlendMode::normal},           {QStringLiteral("PASS_THROUGH"), LayerBlendMode::normal},
        {QStringLiteral("DARKEN"), LayerBlendMode::darken},           {QStringLiteral("MULTIPLY"), LayerBlendMode::multiply},
        {QStringLiteral("LINEAR_BURN"), LayerBlendMode::colorBurn},   {QStringLiteral("COLOR_BURN"), LayerBlendMode::colorBurn},
        {QStringLiteral("LIGHTEN"), LayerBlendMode::lighten},         {QStringLiteral("SCREEN"), LayerBlendMode::screen},
        {QStringLiteral("LINEAR_DODGE"), LayerBlendMode::colorDodge}, {QStringLiteral("COLOR_DODGE"), LayerBlendMode::colorDodge},
        {QStringLiteral("OVERLAY"), LayerBlendMode::overlay},         {QStringLiteral("SOFT_LIGHT"), LayerBlendMode::softLight},
        {QStringLiteral("HARD_LIGHT"), LayerBlendMode::overlay},      {QStringLiteral("DIFFERENCE"), LayerBlendMode::difference},
        {QStringLiteral("EXCLUSION"), LayerBlendMode::difference},    {QStringLiteral("HUE"), LayerBlendMode::hue},
        {QStringLiteral("SATURATION"), LayerBlendMode::saturation},   {QStringLiteral("COLOR"), LayerBlendMode::color},
        {QStringLiteral("LUMINOSITY"), LayerBlendMode::luminosity}};
    const auto mode = modes.find(str(node, "blendMode", QStringLiteral("NORMAL")));
    object.blendMode = mode == modes.end() ? LayerBlendMode::normal : mode->second;
}

std::optional<QUuid> mapNode(Context &ctx, const Guid &guid, const QUuid &parent, const QTransform &parentTransform)
{
    const DepthGuard guard(ctx);
    if (guard.tooDeep)
        return std::nullopt;
    const auto found = ctx.tree.nodes.find(guid);
    if (found == ctx.tree.nodes.end())
        return std::nullopt;
    const QVariantMap &fields = found->second.fields;
    const QString type = str(fields, "type");
    const QTransform own = matrix(fields.value(QStringLiteral("transform"))) * parentTransform;
    const auto parentNode = ctx.tree.nodes.find(found->second.parent);
    const QVariantMap *parentFields = parentNode == ctx.tree.nodes.end() ? nullptr : &parentNode->second.fields;

    if (type == QLatin1String("BOOLEAN_OPERATION")) {
        VectorObject object;
        object.kind = ObjectKind::path;
        applyCommon(ctx, fields, object);
        object.path = VectorPath::fromPainterPath(booleanOutline(ctx, guid, parentTransform));
        applyFillsAndStrokes(ctx, fields, object);
        applyEffects(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        ctx.warn(QStringLiteral("Boolean operations became one plain path; they're no longer live."));
        return insert(ctx, std::move(object), guid, parent);
    }
    if (type == QLatin1String("RECTANGLE")) {
        VectorObject object;
        object.kind = ObjectKind::path;
        applyCommon(ctx, fields, object);
        applyRectangle(ctx, fields, object, own);
        applyFillsAndStrokes(ctx, fields, object);
        applyEffects(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        return insert(ctx, std::move(object), guid, parent);
    }
    if (type == QLatin1String("ELLIPSE") || type == QLatin1String("STAR") || type == QLatin1String("REGULAR_POLYGON")
        || type == QLatin1String("LINE") || type == QLatin1String("VECTOR")) {
        VectorObject object;
        object.kind = ObjectKind::path;
        applyCommon(ctx, fields, object);
        const VectorPath local = type == QLatin1String("ELLIPSE") ? Shapes::ellipse(localBox(fields)) : localShapePath(ctx, fields, type);
        object.path = local.transformed(own);
        applyFillsAndStrokes(ctx, fields, object);
        applyEffects(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        return insert(ctx, std::move(object), guid, parent);
    }
    if (type == QLatin1String("TEXT")) {
        VectorObject object;
        object.kind = ObjectKind::text;
        applyCommon(ctx, fields, object);
        object.transform = own;
        mapText(ctx, fields, object);
        applyFillsAndStrokes(ctx, fields, object);
        applyEffects(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        return insert(ctx, std::move(object), guid, parent);
    }
    if (type == QLatin1String("INSTANCE")) {
        VectorObject object;
        object.kind = ObjectKind::group;
        applyCommon(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        const QUuid id = insert(ctx, std::move(object), guid, parent);
        deferInstance(ctx, fields, id, own);
        // Its children come from the master, through Components::sync at the end.
        return id;
    }
    if (type == QLatin1String("SYMBOL")) {
        if (const std::optional<QImage> image = soleImageFill(ctx, fields)) {
            VectorObject object;
            object.kind = ObjectKind::image;
            applyCommon(ctx, fields, object);
            object.transform = own;
            object.image = *image;
            applyLayoutItem(ctx, fields, parentFields, object);
            return insert(ctx, std::move(object), guid, parent);
        }
        VectorObject object;
        object.kind = ObjectKind::group;
        applyCommon(ctx, fields, object);
        mapComponent(ctx, fields, guid, object, own);
        applyLayoutItem(ctx, fields, parentFields, object);
        const QUuid id = insert(ctx, std::move(object), guid, parent);
        mapChildren(ctx, guid, id, own);
        return id;
    }
    if (type == QLatin1String("FRAME") || type == QLatin1String("COMPONENT")) {
        VectorObject object;
        object.kind = ObjectKind::frame;
        applyCommon(ctx, fields, object);
        applyFrameBox(ctx, fields, object, own);
        applyFillsAndStrokes(ctx, fields, object);
        applyEffects(ctx, fields, object);
        applyAutoLayout(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        const QUuid id = insert(ctx, std::move(object), guid, parent);
        mapChildren(ctx, guid, id, own);
        return id;
    }
    if (type == QLatin1String("GROUP") || type == QLatin1String("SECTION")) {
        VectorObject object;
        object.kind = ObjectKind::group;
        applyCommon(ctx, fields, object);
        applyLayoutItem(ctx, fields, parentFields, object);
        const QUuid id = insert(ctx, std::move(object), guid, parent);
        mapChildren(ctx, guid, id, own);
        return id;
    }
    ctx.warn(QStringLiteral("A \"%1\" node isn't something this app can bring in yet, so it was left out.").arg(type));
    return std::nullopt;
}

VectorDocument map(const Tree &tree, QStringList &warnings)
{
    VectorDocument document;
    document.objects.clear();
    Context ctx{tree, document, warnings, {}, {}, tree.imagesByHash};
    std::vector<Artboard> boards;
    // Several Figma pages become pages, each from the origin; one stays the plain document.
    const bool paged = tree.pages.size() > 1;
    std::vector<Page> pageList;
    double x = 0;
    constexpr double gutter = 100;
    for (const Guid &page : tree.pages) {
        const Node &pageNode = tree.nodes.at(page);
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = str(pageNode.fields, "name", QStringLiteral("Page"));
        layer.layerColor = nextLayerColor(int(boards.size()));
        if (paged) {
            pageList.push_back(Page{QUuid::createUuid(), layer.name});
            layer.page = pageList.back().id;
        }
        document.objects.push_back(layer);
        const QUuid layerId = document.objects.back().id;
        const size_t firstInstance = ctx.pendingInstances.size();
        for (const Guid &child : pageNode.children)
            mapNode(ctx, child, layerId, {});
        const QRectF bounds = document.bounds(layerId, true);
        const QRectF box = bounds.isValid() ? bounds : QRectF(0, 0, 800, 600);
        // Each page's content sits inside its own artboard, at x, not piled at the origin.
        if (bounds.isValid() && (bounds.left() != x || bounds.top() != 0)) {
            const QTransform shift = QTransform::fromTranslate(x - bounds.left(), -bounds.top());
            document.transform(layerId, shift);
            // Instances resolve after every page has moved, so their placement moves with the page.
            for (size_t i = firstInstance; i < ctx.pendingInstances.size(); ++i)
                ctx.pendingInstances[i].transform = ctx.pendingInstances[i].transform * shift;
        }
        Artboard board;
        board.name = layer.name;
        board.rect = QRectF(x, 0, box.width(), box.height());
        if (paged)
            board.page = pageList.back().id;
        boards.push_back(board);
        if (!paged)
            x += box.width() + gutter;
    }
    resolveInstances(ctx);
    Components::sync(document);
    if (paged) {
        document.pages = pageList;
        document.currentPage = pageList.front().id;
        document.artboards = boards;
        document.size = boards.front().rect.size();
        document.background = boards.front().background;
    } else if (!boards.empty()) {
        document.size = boards.front().rect.size();
        document.setArtboards(boards);
    } else if (document.layers().empty()) {
        document = VectorDocument::blank(QSizeF(800, 600));
    }
    return document;
}

VectorDocument mapNode(const Tree &tree, const Guid &nodeID, QStringList &warnings)
{
    VectorDocument document;
    document.objects.clear();
    Context ctx{tree, document, warnings, {}, {}, tree.imagesByHash};
    VectorObject layer;
    layer.kind = ObjectKind::layer;
    layer.name = QStringLiteral("Layer 1");
    document.objects.push_back(layer);
    const QUuid layerId = document.objects.back().id;
    mapNode(ctx, nodeID, layerId, {});
    resolveInstances(ctx);
    Components::sync(document);
    const QRectF bounds = document.bounds(layerId, true);
    if (bounds.isValid()) {
        document.transform(layerId, QTransform::fromTranslate(-bounds.left(), -bounds.top()));
        document.size = QSizeF(std::max(1.0, bounds.width()), std::max(1.0, bounds.height()));
    }
    if (document.layers().empty())
        document = VectorDocument::blank(QSizeF(800, 600));
    return document;
}
}
