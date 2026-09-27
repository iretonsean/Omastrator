#include "Document/VectorDocument.h"
#include <algorithm>
#include <cmath>

namespace {
// Two "none" paints match whatever colour they last held.
bool samePaint(const Paint &a, const Paint &b)
{
    if (!a.isVisible() && !b.isVisible())
        return true;
    return a == b;
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

bool sameWeight(const StrokeStyle &a, const StrokeStyle &b)
{
    if (!a.isVisible() || !b.isVisible())
        return a.isVisible() == b.isVisible();
    return near(a.width, b.width);
}

bool sameFace(const TextContent &a, const TextContent &b)
{
    // Read through QFont so every way the model spells a face compares alike.
    const QFont left = a.font(), right = b.font();
    return left.family() == right.family() && left.styleName() == right.styleName() && left.weight() == right.weight()
        && left.italic() == right.italic();
}

bool shares(const VectorObject &like, const VectorObject &other, SameAttribute attribute)
{
    switch (attribute) {
    case SameAttribute::fillColor:
        return other.hasPaint() && samePaint(like.fill, other.fill);
    case SameAttribute::strokeColor:
        return other.hasPaint() && samePaint(like.stroke.paint, other.stroke.paint);
    case SameAttribute::fillAndStroke:
        return other.hasPaint() && samePaint(like.fill, other.fill) && samePaint(like.stroke.paint, other.stroke.paint);
    case SameAttribute::strokeWeight:
        return other.hasPaint() && sameWeight(like.stroke, other.stroke);
    case SameAttribute::opacity:
        return near(like.opacity, other.opacity);
    case SameAttribute::blendMode:
        return like.blendMode == other.blendMode;
    case SameAttribute::fontFamily:
        return other.kind == ObjectKind::text && like.text.family == other.text.family;
    case SameAttribute::fontFamilyStyleSize:
        return other.kind == ObjectKind::text && sameFace(like.text, other.text) && near(like.text.size, other.text.size);
    }
    return false;
}
}

std::vector<QUuid> VectorDocument::hitTestAll(QPointF point, double tolerance, size_t limit) const
{
    std::vector<QUuid> result;
    for (auto it = objects.rbegin(); it != objects.rend() && result.size() < limit; ++it) {
        const VectorObject &object = *it;
        if (object.isContainer() || !isEffectivelyVisible(object.id) || isEffectivelyLocked(object.id))
            continue;
        bool hit = false;
        if (object.kind == ObjectKind::path) {
            const double reach = tolerance + (object.stroke.isVisible() ? object.stroke.width / 2 : 0);
            hit = object.path.distanceToOutline(point) <= reach;
            if (!hit && object.fill.isVisible())
                hit = object.path.painterPath().contains(point);
        } else if (object.kind == ObjectKind::text) {
            // Glyph gaps would be hard to click; the text's box counts.
            const QRectF box = object.text.area ? object.transform.mapRect(object.text.frame()) : object.outline().boundingRect();
            hit = box.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point);
        } else {
            hit = object.outline().contains(point);
        }
        if (hit)
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> VectorDocument::matching(const QUuid &like, SameAttribute attribute) const
{
    std::vector<QUuid> result;
    const VectorObject *source = find(like);
    if (!source || source->isContainer())
        return result;
    // Font attributes only mean something between texts.
    if ((attribute == SameAttribute::fontFamily || attribute == SameAttribute::fontFamilyStyleSize) && source->kind != ObjectKind::text)
        return result;
    for (const VectorObject &object : objects) {
        if (object.isContainer() || !isEffectivelyVisible(object.id) || isEffectivelyLocked(object.id))
            continue;
        if (object.id == like || shares(*source, object, attribute))
            result.push_back(object.id);
    }
    return result;
}

std::vector<QUuid> VectorDocument::matching(ObjectFilter filter) const
{
    std::vector<QUuid> result;
    for (const VectorObject &object : objects) {
        if (object.kind == ObjectKind::layer || !isEffectivelyVisible(object.id) || isEffectivelyLocked(object.id))
            continue;
        bool wanted = false;
        switch (filter) {
        case ObjectFilter::textObjects:
            wanted = object.kind == ObjectKind::text;
            break;
        case ObjectFilter::images:
            wanted = object.kind == ObjectKind::image;
            break;
        case ObjectFilter::clippingMasks: {
            // The clip is the first child of a clip group.
            const VectorObject *parent = object.parentID ? find(*object.parentID) : nullptr;
            wanted = parent && parent->isClipGroup && children(parent->id).front() == object.id;
            break;
        }
        case ObjectFilter::openPaths:
            wanted = object.kind == ObjectKind::path && object.path.nodeCount() > 1
                && std::any_of(object.path.contours.begin(), object.path.contours.end(), [](const Contour &c) { return !c.closed && c.nodes.size() > 1; });
            break;
        case ObjectFilter::strayPoints:
            wanted = object.kind == ObjectKind::path && object.path.nodeCount() == 1;
            break;
        }
        if (wanted)
            result.push_back(object.id);
    }
    return result;
}
