#include "Agent/AgentEdits.h"
#include "Agent/AgentProtocol.h"
#include <algorithm>

namespace AgentEdits {
std::vector<QUuid> inOrder(const VectorDocument &document, const std::vector<QUuid> &ids)
{
    std::vector<QUuid> result;
    for (const VectorObject &object : document.objects) {
        if (std::find(ids.begin(), ids.end(), object.id) != ids.end())
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> roots(const VectorDocument &document, const std::vector<QUuid> &ids)
{
    std::vector<QUuid> result;
    for (const QUuid &id : ids) {
        const bool nested = std::any_of(ids.begin(), ids.end(), [&](const QUuid &other) { return other != id && document.isAncestor(other, id); });
        if (!nested)
            result.push_back(id);
    }
    return result;
}

std::vector<QUuid> leaves(const VectorDocument &document, const std::vector<QUuid> &ids)
{
    std::vector<QUuid> result;
    for (const QUuid &id : inOrder(document, ids)) {
        const VectorObject *object = document.find(id);
        if (!object->isContainer()) {
            if (std::find(result.begin(), result.end(), id) == result.end())
                result.push_back(id);
            continue;
        }
        for (const QUuid &nested : document.descendants(id)) {
            const VectorObject *leaf = document.find(nested);
            if (leaf && !leaf->isContainer() && std::find(result.begin(), result.end(), nested) == result.end())
                result.push_back(nested);
        }
    }
    return result;
}

std::optional<QUuid> openLayer(const VectorDocument &document, std::optional<QUuid> preferred)
{
    auto takes = [&](const QUuid &layer) { return !document.isEffectivelyLocked(layer) && document.isEffectivelyVisible(layer); };
    if (preferred && document.find(*preferred) && takes(*preferred))
        return preferred;
    std::optional<QUuid> result;
    for (const QUuid &layer : document.layers()) {
        if (takes(layer))
            result = layer;
    }
    return result;
}

QUuid insertArt(VectorDocument &document, const VectorDocument &art, const QString &name, const QUuid &parent,
                std::optional<QUuid> above)
{
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = name;
    const QUuid groupID = group.id;
    document.insert(std::move(group), parent, above);
    // Parents come first in `objects`, so appending keeps each parent's order.
    for (const VectorObject &object : art.objects) {
        if (object.kind == ObjectKind::layer)
            continue;
        const VectorObject *owner = object.parentID ? art.find(*object.parentID) : nullptr;
        const QUuid into = !owner || owner->kind == ObjectKind::layer ? groupID : owner->id;
        document.insert(object, into);
    }
    if (document.children(groupID).empty())
        throw AgentProtocol::Error(AgentProtocol::invalidParams, QStringLiteral("The SVG has no shapes to import."));
    return groupID;
}

void fit(VectorDocument &document, const QUuid &id, const QRectF &box)
{
    const QRectF bounds = document.bounds(id);
    if (bounds.isNull() || !(box.width() > 0 && box.height() > 0))
        return;
    // A line has no height: fit the side it has.
    const double sx = bounds.width() > 0 ? box.width() / bounds.width() : 1e9;
    const double sy = bounds.height() > 0 ? box.height() / bounds.height() : 1e9;
    const double scale = std::min(sx, sy) < 1e9 ? std::min(sx, sy) : 1;
    const QPointF from = bounds.center(), to = box.center();
    document.transform(id, QTransform::fromTranslate(-from.x(), -from.y()) * QTransform::fromScale(scale, scale)
                               * QTransform::fromTranslate(to.x(), to.y()));
}

QUuid group(VectorDocument &document, const std::vector<QUuid> &ids, const QString &name)
{
    const std::vector<QUuid> members = inOrder(document, ids);
    if (members.empty())
        throw AgentProtocol::Error(AgentProtocol::invalidParams, QStringLiteral("Name at least one object to group."));
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = name.isEmpty() ? QStringLiteral("Group") : name;
    const QUuid groupID = group.id;
    const QUuid top = members.back();
    document.insert(std::move(group), *document.find(top)->parentID, top);
    for (const QUuid &id : members)
        document.move(id, groupID, -1);
    return groupID;
}

std::vector<QUuid> ungroup(VectorDocument &document, const std::vector<QUuid> &ids)
{
    std::vector<QUuid> released;
    for (const QUuid &id : inOrder(document, ids)) {
        const VectorObject *group = document.find(id);
        if (!group || group->kind != ObjectKind::group) {
            released.push_back(id);
            continue;
        }
        const QUuid parent = *group->parentID;
        const auto siblings = document.children(parent);
        int index = int(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
        // Group opacity carries into its children.
        const double opacity = group->opacity;
        for (const QUuid &child : document.children(id)) {
            document.move(child, parent, index++);
            document.find(child)->opacity *= opacity;
            released.push_back(child);
        }
        document.remove({id});
    }
    return released;
}

void arrange(VectorDocument &document, const std::vector<QUuid> &ids, ArrangeOrder order)
{
    std::vector<QUuid> ordered = inOrder(document, ids);
    // Moving front-most first keeps the others' relative order.
    if (order == ArrangeOrder::bringToFront || order == ArrangeOrder::bringForward)
        std::reverse(ordered.begin(), ordered.end());
    for (const QUuid &id : ordered) {
        const QUuid parent = *document.find(id)->parentID;
        const auto siblings = document.children(parent);
        const int index = int(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
        const int last = int(siblings.size()) - 1;
        int target = index;
        switch (order) {
        case ArrangeOrder::bringToFront:
            target = last;
            break;
        case ArrangeOrder::bringForward:
            target = std::min(last, index + 1);
            break;
        case ArrangeOrder::sendBackward:
            target = std::max(0, index - 1);
            break;
        case ArrangeOrder::sendToBack:
            target = 0;
            break;
        }
        // `move` counts the index among siblings without the moved one.
        if (target != index)
            document.move(id, parent, target == last ? -1 : target);
    }
}

void align(VectorDocument &document, const std::vector<QUuid> &ids, AlignEdge edge, AlignTarget target)
{
    const QRectF reference = target == AlignTarget::artboard || ids.size() == 1 ? QRectF(QPointF(0, 0), document.size)
                                                                                : document.bounds(ids);
    for (const QUuid &id : ids) {
        const QRectF bounds = document.bounds(id);
        QPointF delta;
        switch (edge) {
        case AlignEdge::left:
            delta.setX(reference.left() - bounds.left());
            break;
        case AlignEdge::horizontalCenter:
            delta.setX(reference.center().x() - bounds.center().x());
            break;
        case AlignEdge::right:
            delta.setX(reference.right() - bounds.right());
            break;
        case AlignEdge::top:
            delta.setY(reference.top() - bounds.top());
            break;
        case AlignEdge::verticalCenter:
            delta.setY(reference.center().y() - bounds.center().y());
            break;
        case AlignEdge::bottom:
            delta.setY(reference.bottom() - bounds.bottom());
            break;
        }
        document.transform(id, QTransform::fromTranslate(delta.x(), delta.y()));
    }
}

void distribute(VectorDocument &document, const std::vector<QUuid> &ids, DistributeAxis axis)
{
    if (ids.size() < 3)
        throw AgentProtocol::Error(AgentProtocol::invalidParams, QStringLiteral("Distribute needs three or more objects."));
    std::vector<QUuid> sorted = ids;
    const bool horizontal = axis == DistributeAxis::horizontal;
    auto center = [&](const QUuid &id) {
        const QPointF c = document.bounds(id).center();
        return horizontal ? c.x() : c.y();
    };
    std::sort(sorted.begin(), sorted.end(), [&](const QUuid &a, const QUuid &b) { return center(a) < center(b); });
    const double first = center(sorted.front()), last = center(sorted.back());
    const double step = (last - first) / double(sorted.size() - 1);
    for (size_t index = 1; index + 1 < sorted.size(); ++index) {
        const double shift = first + step * double(index) - center(sorted[index]);
        document.transform(sorted[index], horizontal ? QTransform::fromTranslate(shift, 0) : QTransform::fromTranslate(0, shift));
    }
}

std::optional<QUuid> combine(VectorDocument &document, const std::vector<QUuid> &ids, BooleanOperation operation)
{
    std::vector<QPainterPath> shapes;
    std::vector<QUuid> used;
    for (const QUuid &id : leaves(document, ids)) {
        const VectorObject *object = document.find(id);
        if (!object->hasPaint() || document.isEffectivelyLocked(id))
            continue;
        QPainterPath shape = object->outline();
        shape.setFillRule(Qt::WindingFill);
        shapes.push_back(shape);
        used.push_back(id);
    }
    if (shapes.size() < 2)
        throw AgentProtocol::Error(AgentProtocol::invalidParams, QStringLiteral("Pathfinder needs two or more unlocked paths or texts."));
    VectorObject result = *document.find(used.front());
    const VectorObject &topmost = *document.find(used.back());
    result.id = QUuid::createUuid();
    result.kind = ObjectKind::path;
    result.transform = {};
    result.name = QStringLiteral("Compound Path");
    // Illustrator's Pathfinder keeps the top object's style, except Minus Front.
    if (operation != BooleanOperation::minusFront) {
        result.setFills(topmost.fills());
        result.setStrokes(topmost.strokes());
    }
    result.path = VectorPath::fromPainterPath(::combine(shapes, operation));
    const QUuid parent = *topmost.parentID;
    const QUuid above = topmost.id;
    const bool empty = result.path.isEmpty();
    const QUuid resultID = result.id;
    document.insert(std::move(result), parent, above);
    document.remove(used);
    if (empty) {
        document.remove({resultID});
        return std::nullopt;
    }
    // Groups left with nothing inside go too.
    for (const QUuid &id : ids) {
        const VectorObject *object = document.find(id);
        if (object && object->kind == ObjectKind::group && document.children(id).empty())
            document.remove({id});
    }
    return resultID;
}
}
