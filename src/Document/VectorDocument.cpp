#include "Document/VectorDocument.h"
#include "Document/StrokeGeometry.h"
#include <QFontMetricsF>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
const std::array<std::pair<ObjectKind, const char *>, 6> kindNames{{
    {ObjectKind::layer, "layer"}, {ObjectKind::group, "group"}, {ObjectKind::path, "path"},
    {ObjectKind::text, "text"}, {ObjectKind::image, "image"}, {ObjectKind::frame, "frame"},
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

VectorObject VectorObject::frame(const QRectF &rect, const QString &name)
{
    VectorObject object;
    object.kind = ObjectKind::frame;
    object.name = name;
    object.shape = LiveRectangle{.rect = rect.normalized(), .placement = {}};
    object.path = object.shape->path();
    object.fill = Paint::solid(Qt::white);
    object.stroke.paint = Paint::none();
    return object;
}

QPainterPath VectorObject::outline() const
{
    switch (kind) {
    case ObjectKind::path:
    case ObjectKind::frame:
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

std::optional<QUuid> VectorDocument::selectableObject(const QUuid &id) const
{
    const std::optional<QUuid> top = topLevelObject(id);
    const VectorObject *frame = top ? find(*top) : nullptr;
    if (!frame || frame->kind != ObjectKind::frame || *top == id)
        return top;
    for (const VectorObject *object = find(id); object && object->parentID; object = find(*object->parentID)) {
        if (*object->parentID == *top)
            return object->id;
    }
    return top;
}

std::vector<QUuid> VectorDocument::clippingFrames(const QUuid &id) const
{
    std::vector<QUuid> result;
    const VectorObject *object = find(id);
    for (const VectorObject *parent = object && object->parentID ? find(*object->parentID) : nullptr; parent;
         parent = parent->parentID ? find(*parent->parentID) : nullptr) {
        if (parent->kind == ObjectKind::frame && parent->clipsContent)
            result.push_back(parent->id);
    }
    return result;
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
    if (object->kind == ObjectKind::frame) {
        // Its box, and with clipping off whatever of its children shows past it.
        const QRectF box = includeStroke ? strokeBounds(*object) : object->outline().boundingRect();
        const QRectF content = object->clipsContent ? QRectF() : bounds(children(id), includeStroke);
        return content.isNull() ? box : box.united(content);
    }
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
    if (object->kind == ObjectKind::frame)
        result.addPath(object->outline());
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
    // Each subtree is contiguous after its root, so an object is inside exactly when its parent is on the open chain.
    std::vector<QUuid> chain{objects[size_t(index)].id};
    int end = index + 1;
    for (; end < int(objects.size()); ++end) {
        const std::optional<QUuid> &parent = objects[size_t(end)].parentID;
        while (!chain.empty() && !(parent && *parent == chain.back()))
            chain.pop_back();
        if (chain.empty())
            break;
        chain.push_back(objects[size_t(end)].id);
    }
    return end;
}

void VectorDocument::appendLayer(VectorObject layer)
{
    if (!pages.empty() && layer.page.isNull())
        layer.page = currentPageId();
    objects.push_back(std::move(layer));
}

void VectorDocument::insert(VectorObject object, const QUuid &parent, std::optional<QUuid> above)
{
    object.parentID = parent;
    if (object.kind == ObjectKind::layer && object.page.isNull() && !pages.empty())
        object.page = currentPageId();
    // Painting in order: the parent is the last object or one of its ancestors, so its subtree runs to the end.
    if (!above && !objects.empty()) {
        int at = int(objects.size()) - 1;
        while (objects[size_t(at)].id != parent && objects[size_t(at)].parentID && *objects[size_t(at)].parentID != parent) {
            const QUuid up = *objects[size_t(at)].parentID;
            while (--at >= 0 && objects[size_t(at)].id != up) {}
            if (at < 0)
                break;
        }
        if (at >= 0 && (objects[size_t(at)].id == parent || objects[size_t(at)].parentID == parent)) {
            objects.push_back(std::move(object));
            return;
        }
    }
    int at = -1;
    if (above) {
        const int index = indexOf(*above);
        if (index >= 0)
            at = subtreeEnd(index);
    }
    if (at < 0) {
        // Newest first: whatever is being painted into was opened recently.
        int parentIndex = int(objects.size()) - 1;
        while (parentIndex >= 0 && objects[size_t(parentIndex)].id != parent)
            --parentIndex;
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
    // A thread into what's removed goes nowhere now.
    for (VectorObject &object : objects) {
        if (std::find(doomed.begin(), doomed.end(), object.text.threadNext) != doomed.end())
            object.text.threadNext = QUuid();
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
    if (pages.size() < 2)
        return moveUnder(id, std::nullopt, index);
    // `index` counts this page's layers; moveUnder counts every page's.
    std::vector<QUuid> mine = layersOn(object->page);
    std::erase(mine, id);
    std::vector<QUuid> siblings = children(std::nullopt);
    std::erase(siblings, id);
    int at = -1;
    if (!mine.empty()) {
        const bool inside = index >= 0 && index < int(mine.size());
        const auto found = std::find(siblings.begin(), siblings.end(), inside ? mine[size_t(index)] : mine.back());
        at = int(found - siblings.begin()) + (inside ? 0 : 1);
    }
    return moveUnder(id, std::nullopt, at);
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

void VectorDocument::transform(const QUuid &id, const QTransform &transform, bool scaleCorners)
{
    VectorObject *object = find(id);
    if (!object)
        return;
    if (object->kind == ObjectKind::frame && object->shape) {
        // A frame's box is always a rectangle: skewed, it keeps the box around where its corners went.
        std::optional<LiveRectangle> box = object->shape->transformed(transform, scaleCorners);
        if (!box)
            box = LiveRectangle{.rect = transform.map(object->shape->placement.map(QPolygonF(object->shape->rect))).boundingRect(),
                                .placement = {}, .radii = object->shape->radii, .styles = object->shape->styles};
        object->shape = box;
        object->path = box->path();
    } else if (object->kind == ObjectKind::path) {
        // A live rectangle stays live while it stays a rectangle.
        std::optional<LiveRectangle> live;
        if (const LiveRectangle *shape = object->liveShape())
            live = shape->transformed(transform, scaleCorners);
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
    // A component's frame and an instance's placement move with them.
    if (object->component)
        object->component->placement = object->component->placement * transform;
    if (object->instance)
        object->instance->placement = object->instance->placement * transform;
    for (const QUuid &child : children(id))
        this->transform(child, transform, scaleCorners);
}

void VectorDocument::transform(const QUuid &id, const QTransform &transform, bool scaleStrokes, bool reflowAreaText, bool scaleCorners)
{
    // A box resize (the handles, W and H) on an upright frame is Figma's: the box changes and its
    // children follow their constraints. The Scale tool and Transform ▸ Scale still scale everything.
    if (const VectorObject *frame = find(id); reflowAreaText && frame && frame->kind == ObjectKind::frame && frame->shape
        && frame->shape->placement.isIdentity() && transform.type() <= QTransform::TxScale && transform.m11() > 0 && transform.m22() > 0) {
        resizeFrame(id, transform.mapRect(frame->shape->rect.normalized()));
        return;
    }
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
    this->transform(id, transform, scaleCorners);
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
        // A copy of a component inside it is a plain copy; the copy itself becomes an instance.
        if (&copy != &copies.front())
            copy.component.reset();
        // A thread stays within the copy; one that leaves it is dropped.
        if (!copy.text.threadNext.isNull()) {
            const auto found = std::find_if(renamed.begin(), renamed.end(), [&](const auto &pair) { return pair.first == copy.text.threadNext; });
            copy.text.threadNext = found == renamed.end() ? QUuid() : found->second;
        }
    }
    if (!copies.empty() && copies.front().component) {
        copies.front().instance = InstanceInfo{objects[size_t(from)].id, copies.front().component->placement, {}, {}};
        copies.front().component.reset();
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
