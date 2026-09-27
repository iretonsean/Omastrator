#include "Document/VectorDocument.h"
#include <QFontMetricsF>
#include <QPainterPathStroker>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
const std::array<std::pair<ObjectKind, const char *>, 5> kindNames{{
    {ObjectKind::layer, "layer"}, {ObjectKind::group, "group"}, {ObjectKind::path, "path"},
    {ObjectKind::text, "text"}, {ObjectKind::image, "image"},
}};
const std::array<std::pair<TextAlignment, const char *>, 3> alignmentNames{{
    {TextAlignment::left, "left"}, {TextAlignment::center, "center"}, {TextAlignment::right, "right"},
}};

QRectF strokeBounds(const VectorObject &object)
{
    const QPainterPath outline = object.outline();
    if (!object.hasPaint() || !object.stroke.isVisible())
        return outline.boundingRect();
    QPainterPathStroker stroker;
    stroker.setWidth(object.stroke.width);
    stroker.setCapStyle(object.stroke.cap);
    stroker.setJoinStyle(object.stroke.join);
    stroker.setMiterLimit(object.stroke.miterLimit);
    return outline.boundingRect().united(stroker.createStroke(outline).boundingRect());
}
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

QString rawValue(TextAlignment alignment)
{
    return QString::fromLatin1(alignmentNames.at(size_t(alignment)).second);
}

std::optional<TextAlignment> textAlignment(const QString &rawValue)
{
    for (const auto &[alignment, name] : alignmentNames) {
        if (rawValue == QLatin1String(name))
            return alignment;
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

QFont TextContent::font() const
{
    QFont font(family);
    font.setBold(bold);
    font.setItalic(italic);
    font.setLetterSpacing(QFont::AbsoluteSpacing, tracking);
    // Outlines at the design size: point sizes would follow the screen's DPI.
    font.setPixelSize(std::max(1, int(std::lround(size))));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QPainterPath TextContent::outline() const
{
    QPainterPath path;
    const QFont face = font();
    // Pixel sizes are whole; scale the outlines to fractional sizes.
    const double scale = size / std::max(1.0, double(face.pixelSize()));
    const QFontMetricsF metrics(face);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int line = 0; line < lines.size(); ++line) {
        const double width = metrics.horizontalAdvance(lines[line]);
        double x = 0;
        if (alignment == TextAlignment::center)
            x = -width / 2;
        else if (alignment == TextAlignment::right)
            x = -width;
        path.addText(QPointF(x, line * size * leading / scale), face, lines[line]);
    }
    return scale == 1 ? path : QTransform::fromScale(scale, scale).map(path);
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
    for (auto it = objects.rbegin(); it != objects.rend(); ++it) {
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
            hit = object.outline().boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point);
        } else {
            hit = object.outline().contains(point);
        }
        if (hit)
            return object.id;
    }
    return std::nullopt;
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
    if (object->kind == ObjectKind::path)
        object->path = object->path.transformed(transform);
    else if (object->kind == ObjectKind::text || object->kind == ObjectKind::image)
        object->transform = object->transform * transform;
    for (const QUuid &child : children(id))
        this->transform(child, transform);
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
