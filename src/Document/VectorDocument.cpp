#include "Document/VectorDocument.h"
#include "Document/StrokeGeometry.h"
#include <QFontMetricsF>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
const std::array<std::pair<ObjectKind, const char *>, 5> kindNames{{
    {ObjectKind::layer, "layer"}, {ObjectKind::group, "group"}, {ObjectKind::path, "path"},
    {ObjectKind::text, "text"}, {ObjectKind::image, "image"},
}};

QRectF strokeBounds(const VectorObject &object)
{
    const QPainterPath outline = object.outline();
    QRectF bounds = outline.boundingRect();
    if (!object.hasPaint())
        return bounds;
    for (const StrokeStyle &stroke : object.strokes()) {
        if (stroke.isVisible())
            bounds = bounds.united(StrokeGeometry::extent(outline, stroke));
    }
    return bounds;
}
}

std::vector<Paint> VectorObject::fills() const
{
    std::vector<Paint> all{fill};
    all.insert(all.end(), extraFills.begin(), extraFills.end());
    return all;
}

std::vector<StrokeStyle> VectorObject::strokes() const
{
    std::vector<StrokeStyle> all{stroke};
    all.insert(all.end(), extraStrokes.begin(), extraStrokes.end());
    return all;
}

void VectorObject::setFills(std::vector<Paint> fills)
{
    fill = fills.empty() ? Paint::none() : fills.front();
    extraFills.assign(fills.size() > 1 ? fills.begin() + 1 : fills.end(), fills.end());
}

void VectorObject::setStrokes(std::vector<StrokeStyle> strokes)
{
    if (strokes.empty()) {
        // The weight and dashes stay for when a colour comes back.
        stroke.paint = Paint::none();
        extraStrokes.clear();
        return;
    }
    stroke = strokes.front();
    extraStrokes.assign(strokes.begin() + 1, strokes.end());
}

bool VectorObject::hasVisibleFill() const
{
    return fill.isVisible() || std::any_of(extraFills.begin(), extraFills.end(), [](const Paint &paint) { return paint.isVisible(); });
}

bool VectorObject::hasVisibleStroke() const
{
    return stroke.isVisible() || std::any_of(extraStrokes.begin(), extraStrokes.end(), [](const StrokeStyle &each) { return each.isVisible(); });
}

bool VectorObject::hasSimpleAppearance() const
{
    return extraFills.empty() && extraStrokes.empty() && fill.hasPlainComposite() && stroke.paint.hasPlainComposite()
        && (!stroke.isVisible() || stroke.isPlain());
}

void VectorObject::copyAppearance(const VectorObject &other)
{
    fill = other.fill;
    stroke = other.stroke;
    extraFills = other.extraFills;
    extraStrokes = other.extraStrokes;
    opacity = other.opacity;
    blendMode = other.blendMode;
}

QString rawValue(ObjectKind kind)
{
    return QString::fromLatin1(kindNames.at(size_t(kind)).second);
}

std::optional<ObjectKind> objectKind(const QString &rawValue)
{
    for (const auto &[kind, name] : kindNames) {
        if (rawValue == QLatin1String(name))
            return kind;
    }
    return std::nullopt;
}

QColor nextLayerColor(int index)
{
    // Illustrator's first layer colours: blue, red, green, blue-violet, orange.
    static const std::array<QColor, 6> colors{QColor(0x4f, 0x80, 0xff), QColor(0xff, 0x4f, 0x4f), QColor(0x4f, 0xc8, 0x4f),
                                              QColor(0x9a, 0x4f, 0xff), QColor(0xff, 0xa0, 0x28), QColor(0x28, 0xc8, 0xc8)};
    return colors[size_t(std::abs(index)) % colors.size()];
}

QPainterPath VectorObject::outline() const
{
    switch (kind) {
    case ObjectKind::path:
        return path.painterPath();
    case ObjectKind::text:
        return transform.map(text.outline());
    case ObjectKind::image: {
        QPainterPath rect;
        rect.addRect(QRectF(QPointF(0, 0), QSizeF(image.size())));
        return transform.map(rect);
    }
    default:
        return {};
    }
}

VectorDocument VectorDocument::blank(QSizeF size)
{
    VectorDocument document;
    document.size = size;
    VectorObject layer;
    layer.kind = ObjectKind::layer;
    layer.name = QStringLiteral("Layer 1");
    layer.layerColor = nextLayerColor(0);
    document.objects.push_back(layer);
    return document;
}

const VectorObject *VectorDocument::find(const QUuid &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : &objects[size_t(index)];
}

VectorObject *VectorDocument::find(const QUuid &id)
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : &objects[size_t(index)];
}

int VectorDocument::indexOf(const QUuid &id) const
{
    for (size_t index = 0; index < objects.size(); ++index) {
        if (objects[index].id == id)
            return int(index);
    }
    return -1;
}

std::vector<QUuid> VectorDocument::children(const std::optional<QUuid> &parent) const
{
    std::vector<QUuid> result;
    for (const VectorObject &object : objects) {
        if (object.parentID == parent)
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> VectorDocument::descendants(const QUuid &id) const
{
    std::vector<QUuid> result;
    for (const QUuid &child : children(id)) {
        result.push_back(child);
        const auto nested = descendants(child);
        result.insert(result.end(), nested.begin(), nested.end());
    }
    return result;
}

std::vector<QUuid> VectorDocument::layers() const
{
    return children(std::nullopt);
}

bool VectorDocument::isAncestor(const QUuid &ancestor, const QUuid &of) const
{
    const VectorObject *object = find(of);
    while (object && object->parentID) {
        if (*object->parentID == ancestor)
            return true;
        object = find(*object->parentID);
    }
    return false;
}

std::optional<QUuid> VectorDocument::layerOf(const QUuid &id) const
{
    const VectorObject *object = find(id);
    while (object && object->parentID)
        object = find(*object->parentID);
    return object ? std::optional(object->id) : std::nullopt;
}

std::optional<QUuid> VectorDocument::topLevelObject(const QUuid &id) const
{
    const VectorObject *object = find(id);
    if (!object || !object->parentID)
        return std::nullopt;
    while (object) {
        const VectorObject *parent = object->parentID ? find(*object->parentID) : nullptr;
        if (parent && !parent->parentID)
            return object->id;
        object = parent;
    }
    return std::nullopt;
}

bool VectorDocument::isEffectivelyVisible(const QUuid &id) const
{
    for (const VectorObject *object = find(id); object; object = object->parentID ? find(*object->parentID) : nullptr) {
        if (!object->isVisible)
            return false;
    }
    return true;
}

bool VectorDocument::isEffectivelyLocked(const QUuid &id) const
{
    for (const VectorObject *object = find(id); object; object = object->parentID ? find(*object->parentID) : nullptr) {
        if (object->isLocked)
            return true;
    }
    return false;
}

QRectF VectorDocument::bounds(const QUuid &id, bool includeStroke) const
{
    const VectorObject *object = find(id);
    if (!object)
        return {};
    if (object->isContainer()) {
        if (object->isClipGroup) {
            const auto kids = children(id);
            if (!kids.empty())
                return bounds(kids.front(), false);
        }
        return bounds(children(id), includeStroke);
    }
    // Area type's box is its bounds, however little text it holds.
    if (object->kind == ObjectKind::text && object->text.area) {
        const QRectF box = object->transform.mapRect(object->text.frame());
        return includeStroke ? box.united(strokeBounds(*object)) : box;
    }
    return includeStroke ? strokeBounds(*object) : object->outline().boundingRect();
}

QRectF VectorDocument::bounds(const std::vector<QUuid> &ids, bool includeStroke) const
{
    QRectF result;
    for (const QUuid &id : ids) {
        const QRectF rect = bounds(id, includeStroke);
        if (rect.isNull() && rect.topLeft().isNull())
            continue;
        result = result.isNull() && result.topLeft().isNull() ? rect : result.united(rect);
    }
    return result;
}

QPainterPath VectorDocument::outline(const QUuid &id) const
{
    const VectorObject *object = find(id);
    if (!object)
        return {};
    if (!object->isContainer())
        return object->outline();
    QPainterPath result;
    for (const QUuid &child : children(id))
        result.addPath(outline(child));
    return result;
}

std::optional<QUuid> VectorDocument::hitTest(QPointF point, double tolerance) const
{
    const std::vector<QUuid> hits = hitTestAll(point, tolerance, 1);
    return hits.empty() ? std::nullopt : std::optional(hits.front());
}

int VectorDocument::subtreeEnd(int index) const
{
    // Objects are kept with each subtree contiguous after its root.
    const QUuid root = objects[size_t(index)].id;
    int end = index + 1;
    while (end < int(objects.size()) && isAncestor(root, objects[size_t(end)].id))
        ++end;
    return end;
}

void VectorDocument::insert(VectorObject object, const QUuid &parent, std::optional<QUuid> above)
{
    object.parentID = parent;
    int at = -1;
    if (above) {
        const int index = indexOf(*above);
        if (index >= 0)
            at = subtreeEnd(index);
    }
    if (at < 0) {
        const int parentIndex = indexOf(parent);
        at = parentIndex < 0 ? int(objects.size()) : subtreeEnd(parentIndex);
    }
    objects.insert(objects.begin() + at, std::move(object));
}

void VectorDocument::remove(const std::vector<QUuid> &ids)
{
    std::vector<QUuid> doomed = ids;
    for (const QUuid &id : ids) {
        const auto nested = descendants(id);
        doomed.insert(doomed.end(), nested.begin(), nested.end());
    }
    std::erase_if(objects, [&](const VectorObject &object) {
        return std::find(doomed.begin(), doomed.end(), object.id) != doomed.end();
    });
}

bool VectorDocument::move(const QUuid &id, const QUuid &parent, int index)
{
    return moveUnder(id, parent, index);
}

bool VectorDocument::moveLayer(const QUuid &id, int index)
{
    const VectorObject *object = find(id);
    if (!object || object->kind != ObjectKind::layer)
        return false;
    return moveUnder(id, std::nullopt, index);
}

bool VectorDocument::moveUnder(const QUuid &id, const std::optional<QUuid> &parent, int index)
{
    if (parent && (id == *parent || isAncestor(id, *parent)))
        return false;
    const int from = indexOf(id);
    if (from < 0)
        return false;
    const int end = subtreeEnd(from);
    std::vector<VectorObject> subtree(objects.begin() + from, objects.begin() + end);
    objects.erase(objects.begin() + from, objects.begin() + end);
    subtree.front().parentID = parent;
    const std::vector<QUuid> siblings = children(parent);
    int at;
    if (index < 0 || index >= int(siblings.size())) {
        const int parentIndex = parent ? indexOf(*parent) : -1;
        at = parentIndex < 0 ? int(objects.size()) : subtreeEnd(parentIndex);
    } else {
        at = indexOf(siblings[size_t(index)]);
    }
    objects.insert(objects.begin() + at, subtree.begin(), subtree.end());
    return true;
}

void VectorDocument::transform(const QUuid &id, const QTransform &transform)
{
    VectorObject *object = find(id);
    if (!object)
        return;
    if (object->kind == ObjectKind::path) {
        // A live rectangle stays live while it stays a rectangle.
        std::optional<LiveRectangle> live;
        if (const LiveRectangle *shape = object->liveShape())
            live = shape->transformed(transform);
        object->shape = live;
        if (live) {
            const Qt::FillRule rule = object->path.fillRule;
            object->path = live->path();
            object->path.fillRule = rule;
        } else {
            object->path = object->path.transformed(transform);
        }
    } else if (object->kind == ObjectKind::text || object->kind == ObjectKind::image)
        object->transform = object->transform * transform;
    for (const QUuid &child : children(id))
        this->transform(child, transform);
}

void VectorDocument::transform(const QUuid &id, const QTransform &transform, bool scaleStrokes, bool reflowAreaText)
{
    std::vector<QUuid> areas;
    const bool upright = transform.type() <= QTransform::TxScale && transform.m11() > 0 && transform.m22() > 0;
    if (reflowAreaText && upright) {
        std::vector<QUuid> all = descendants(id);
        all.push_back(id);
        for (const QUuid &each : all) {
            const VectorObject *object = find(each);
            if (object && object->kind == ObjectKind::text && object->text.area && object->transform.type() <= QTransform::TxTranslate)
                areas.push_back(each);
        }
    }
    // Area type's new box, read before anything moves.
    std::vector<QRectF> boxes;
    for (const QUuid &each : areas) {
        const VectorObject *object = find(each);
        boxes.push_back(transform.mapRect(object->transform.mapRect(QRectF(QPointF(), object->text.area->width() > 0 ? QSizeF(object->text.area->width(), std::max(object->text.area->height(), 1.0)) : QSizeF(1, 1)))));
    }
    this->transform(id, transform);
    for (size_t index = 0; index < areas.size(); ++index) {
        VectorObject *object = find(areas[index]);
        const QRectF box = boxes[index];
        const double height = object->text.area->height() > 0 ? std::max(1.0, box.height()) : 0;
        object->text.area = QSizeF(std::max(1.0, box.width()), height);
        object->transform = QTransform::fromTranslate(box.left(), box.top());
    }
    const double factor = std::sqrt(std::abs(transform.determinant()));
    if (std::abs(factor - 1) < 1e-9 || factor < 1e-9)
        return;
    std::vector<QUuid> leaves = descendants(id);
    leaves.push_back(id);
    for (const QUuid &leaf : leaves) {
        VectorObject *object = find(leaf);
        // Paths hold document coordinates; text strokes scale with its transform unless undone.
        const bool path = object->kind == ObjectKind::path && scaleStrokes;
        const bool text = object->kind == ObjectKind::text && !scaleStrokes;
        if (!path && !text)
            continue;
        std::vector<StrokeStyle> strokes = object->strokes();
        for (StrokeStyle &stroke : strokes)
            stroke.width = path ? stroke.width * factor : stroke.width / factor;
        object->setStrokes(strokes);
    }
}

std::vector<VectorObject> VectorDocument::copySubtree(const QUuid &id) const
{
    const int from = indexOf(id);
    if (from < 0)
        return {};
    std::vector<VectorObject> copies(objects.begin() + from, objects.begin() + subtreeEnd(from));
    std::vector<std::pair<QUuid, QUuid>> renamed;
    for (VectorObject &copy : copies) {
        const QUuid fresh = QUuid::createUuid();
        renamed.emplace_back(copy.id, fresh);
        copy.id = fresh;
    }
    for (VectorObject &copy : copies) {
        for (const auto &[old, fresh] : renamed) {
            if (copy.parentID == old)
                copy.parentID = fresh;
        }
    }
    return copies;
}

QString VectorDocument::uniqueName(const QString &base) const
{
    for (int number = 1;; ++number) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(number);
        const bool taken = std::any_of(objects.begin(), objects.end(), [&](const VectorObject &o) { return o.name == candidate; });
        if (!taken)
            return candidate;
    }
}
