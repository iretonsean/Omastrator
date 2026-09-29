#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QMimeData>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace {
QString nameFor(const VectorObject &object)
{
    switch (object.kind) {
    case ObjectKind::group:
        return QStringLiteral("Group");
    case ObjectKind::text:
        return object.text.text.section(QLatin1Char('\n'), 0, 0).left(40);
    case ObjectKind::image:
        return QStringLiteral("Image");
    case ObjectKind::frame:
        return QStringLiteral("Frame");
    default:
        return QStringLiteral("Path");
    }
}
}

std::optional<QUuid> EditorSession::insertionParent() const
{
    if (!m_document)
        return std::nullopt;
    // Inside the group the selection sits in, else the isolated group, else the active layer.
    const std::optional<QUuid> isolated = isolatedGroup();
    if (!m_selection.empty()) {
        const VectorObject *top = m_document->find(m_selection.back());
        if (top && top->parentID && (!isolated || *top->parentID == *isolated || m_document->isAncestor(*isolated, *top->parentID)))
            return top->parentID;
    }
    if (isolated)
        return isolated;
    return activeLayer();
}

void EditorSession::insertNew(VectorDocument &document, VectorObject object)
{
    const QUuid id = object.id;
    std::optional<QUuid> parent = insertionParent();
    if (!parent) {
        if (document.pages.empty()) {
            document = VectorDocument::blank(document.size);
        } else {
            // A page with no layer gets one; the pages stay.
            document.appendLayer(VectorDocument::blank(document.size).objects.front());
        }
        parent = document.layers().back();
    }
    // Locked or hidden layers take no new objects; the topmost open one does.
    if (document.isEffectivelyLocked(*parent) || !document.isEffectivelyVisible(*parent)) {
        for (const QUuid &layer : document.layers()) {
            if (!document.isEffectivelyLocked(layer) && document.isEffectivelyVisible(layer))
                parent = layer;
        }
    }
    std::optional<QUuid> above;
    if (!m_selection.empty() && document.find(m_selection.back()) && document.find(m_selection.back())->parentID == parent)
        above = m_selection.back();
    document.insert(std::move(object), *parent, above);
    m_selection = {id};
}

QUuid EditorSession::addObject(VectorObject object, const QString &editName)
{
    if (!m_document)
        return {};
    if (object.name.isEmpty())
        object.name = nameFor(object);
    const QUuid id = object.id;
    edit(editName, [&](VectorDocument &document) { insertNew(document, std::move(object)); });
    return id;
}

QUuid EditorSession::previewAddObject(VectorObject object, std::optional<QUuid> parent)
{
    if (!m_document || !m_interaction)
        return {};
    if (object.name.isEmpty())
        object.name = nameFor(object);
    const QUuid id = object.id;
    if (parent && m_document->find(*parent))
        m_document->insert(std::move(object), *parent);
    else
        insertNew(*m_document, std::move(object));
    notify();
    return id;
}

void EditorSession::previewRemoveObject(const QUuid &id)
{
    if (!m_document || !m_interaction || !m_document->find(id))
        return;
    m_document->remove({id});
    pruneSelection();
    notify();
}

VectorObject EditorSession::pathObject(const VectorPath &path, const QString &name) const
{
    VectorObject object;
    object.kind = ObjectKind::path;
    object.path = path;
    object.fill = m_defaultFill;
    object.stroke = m_defaultStroke;
    // An open two-point line would fill nothing.
    const bool open = std::all_of(path.contours.begin(), path.contours.end(), [](const Contour &c) { return !c.closed; });
    if (open && !object.stroke.isVisible())
        object.stroke.paint = m_defaultFill.isVisible() ? m_defaultFill : Paint::solid(Qt::black);
    object.name = name;
    return object;
}

QUuid EditorSession::addPath(const VectorPath &path, const QString &name)
{
    return addObject(pathObject(path, name), QStringLiteral("Draw %1").arg(name));
}

VectorObject EditorSession::textObject(QPointF baselineOrigin, const QString &text) const
{
    VectorObject object;
    object.kind = ObjectKind::text;
    object.text = defaultText;
    object.text.text = text;
    object.transform = QTransform::fromTranslate(baselineOrigin.x(), baselineOrigin.y());
    // Type is filled, not stroked, as it starts in Illustrator.
    object.fill = m_defaultFill.isVisible() ? m_defaultFill : Paint::solid(Qt::black);
    if (object.fill == Paint::solid(Qt::white))
        object.fill = Paint::solid(Qt::black);
    object.stroke.paint = Paint::none();
    return object;
}

QUuid EditorSession::addText(QPointF baselineOrigin, const QString &text)
{
    return addObject(textObject(baselineOrigin, text), QStringLiteral("Type"));
}

QUuid EditorSession::placeImage(const QImage &image, const QString &name, std::optional<QPointF> center)
{
    if (!m_document || image.isNull())
        return {};
    VectorObject object;
    object.kind = ObjectKind::image;
    object.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    object.name = name;
    QSizeF size = image.size();
    // Larger than the artboard: fit it, as Place does.
    const QRectF board = m_document->artboard(activeArtboard()).rect;
    const double scale = std::min({1.0, board.width() / size.width(), board.height() / size.height()});
    const QPointF middle = center.value_or(board.center());
    object.transform = QTransform::fromScale(scale, scale)
        * QTransform::fromTranslate(middle.x() - size.width() * scale / 2, middle.y() - size.height() * scale / 2);
    return addObject(object, QStringLiteral("Place"));
}

void EditorSession::updateObject(const VectorObject &object, const QString &editName)
{
    if (!m_document || !m_document->find(object.id) || *m_document->find(object.id) == object)
        return;
    edit(editName, [&](VectorDocument &document) { *document.find(object.id) = object; });
}

void EditorSession::deleteSelection()
{
    if (!m_pickedNodes.empty() && m_tool == Tool::directSelect) {
        deletePickedNodes();
        return;
    }
    if (m_selection.empty())
        return;
    deleteObjects(m_selection);
}

void EditorSession::deleteObjects(const std::vector<QUuid> &ids)
{
    if (!m_document || ids.empty())
        return;
    edit(QStringLiteral("Delete"), [&](VectorDocument &document) {
        std::vector<QUuid> doomed;
        for (const QUuid &id : ids) {
            if (!document.isEffectivelyLocked(id) || document.find(id)->kind == ObjectKind::layer)
                doomed.push_back(id);
        }
        document.remove(doomed);
        // A page always keeps one layer.
        if (document.layers().empty()) {
            VectorObject layer;
            layer.kind = ObjectKind::layer;
            layer.name = QStringLiteral("Layer 1");
            layer.layerColor = nextLayerColor(0);
            document.appendLayer(layer);
        }
        std::erase_if(m_selection, [&](const QUuid &id) { return !document.find(id); });
    });
}

std::vector<QUuid> EditorSession::duplicateInto(VectorDocument &document, QPointF offset) const
{
    std::vector<QUuid> copies;
    for (const QUuid &id : selectionInOrder()) {
        std::vector<VectorObject> subtree = document.copySubtree(id);
        if (subtree.empty())
            continue;
        const QUuid parent = *document.find(id)->parentID;
        const QUuid root = subtree.front().id;
        // Insert the root above the original, then its children in order.
        document.insert(subtree.front(), parent, id);
        for (size_t index = 1; index < subtree.size(); ++index)
            document.insert(subtree[index], *subtree[index].parentID);
        document.transform(root, QTransform::fromTranslate(offset.x(), offset.y()));
        copies.push_back(root);
    }
    return copies;
}

void EditorSession::duplicateSelection(QPointF offset)
{
    if (!m_document || m_selection.empty())
        return;
    // Set first: the edit's notification is what refreshes Transform Again's entry.
    m_lastTransform = RepeatTransform{QTransform::fromTranslate(offset.x(), offset.y()), true, std::nullopt};
    edit(QStringLiteral("Duplicate"), [&](VectorDocument &document) { m_selection = duplicateInto(document, offset); });
}

void EditorSession::previewDuplicateSelection()
{
    if (!m_document || !m_interaction || m_selection.empty())
        return;
    // The copies join the base, so transforms move them and leave the originals.
    VectorDocument document = m_interaction->base;
    m_selection = duplicateInto(document, QPointF(0, 0));
    m_interaction->base = document;
    m_interaction->duplicated = true;
    m_document = std::move(document);
    pruneSelection();
    notify();
}

void EditorSession::groupSelection()
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> members = selectionInOrder();
    VectorObject group;
    group.kind = ObjectKind::group;
    group.name = QStringLiteral("Group");
    const QUuid groupID = group.id;
    edit(QStringLiteral("Group"), [&](VectorDocument &document) {
        const QUuid top = members.back();
        const QUuid parent = *document.find(top)->parentID;
        document.insert(group, parent, top);
        for (const QUuid &id : members)
            document.move(id, groupID, -1);
        m_selection = {groupID};
    });
}

void EditorSession::frameSelection()
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> members = selectionInOrder();
    const QRectF box = m_document->bounds(members);
    if (box.isEmpty() && box.topLeft().isNull())
        return;
    VectorObject frame = VectorObject::frame(box, m_document->uniqueName(QStringLiteral("Frame")));
    const QUuid frameID = frame.id;
    edit(QStringLiteral("Frame Selection"), [&](VectorDocument &document) {
        const QUuid top = members.back();
        const QUuid parent = *document.find(top)->parentID;
        document.insert(frame, parent, top);
        for (const QUuid &id : members)
            document.move(id, frameID, -1);
        m_selection = {frameID};
    });
}

namespace {
// Figma's reading of a frame's children for Shift+A: the direction they spread in, put in that
// order, the gap between them and the padding around them, all hugging.
AutoLayout inferAutoLayout(VectorDocument &document, const QUuid &frameID)
{
    AutoLayout layout;
    const QRectF box = document.find(frameID)->shape->rect.normalized();
    std::vector<std::pair<QUuid, QRectF>> items;
    for (const QUuid &child : document.children(frameID)) {
        if (document.find(child)->isVisible)
            items.emplace_back(child, document.bounds(child));
    }
    if (items.empty())
        return layout;
    double left = box.right(), right = box.left(), top = box.bottom(), bottom = box.top();
    double minX = std::numeric_limits<double>::max(), maxX = -minX, minY = minX, maxY = -minX;
    for (const auto &[id, rect] : items) {
        left = std::min(left, rect.left());
        right = std::max(right, rect.right());
        top = std::min(top, rect.top());
        bottom = std::max(bottom, rect.bottom());
        minX = std::min(minX, rect.center().x());
        maxX = std::max(maxX, rect.center().x());
        minY = std::min(minY, rect.center().y());
        maxY = std::max(maxY, rect.center().y());
    }
    const bool horizontal = items.size() < 2 ? box.width() >= box.height() : maxX - minX >= maxY - minY;
    layout.direction = horizontal ? LayoutDirection::horizontal : LayoutDirection::vertical;
    std::stable_sort(items.begin(), items.end(), [horizontal](const auto &a, const auto &b) {
        return horizontal ? a.second.left() < b.second.left() : a.second.top() < b.second.top();
    });
    double gaps = 0;
    for (size_t index = 1; index < items.size(); ++index)
        gaps += std::max(0.0, horizontal ? items[index].second.left() - items[index - 1].second.right()
                                         : items[index].second.top() - items[index - 1].second.bottom());
    layout.gap = items.size() > 1 ? std::round(gaps / double(items.size() - 1)) : 10;
    layout.paddingLeft = std::max(0.0, std::round(left - box.left()));
    layout.paddingTop = std::max(0.0, std::round(top - box.top()));
    layout.paddingRight = std::max(0.0, std::round(box.right() - right));
    layout.paddingBottom = std::max(0.0, std::round(box.bottom() - bottom));
    // The flow follows document order: put them in the order they sit.
    for (size_t index = 0; index < items.size(); ++index)
        document.move(items[index].first, frameID, int(index));
    return layout;
}
}

void EditorSession::addAutoLayout()
{
    if (!m_document || m_selection.empty())
        return;
    const VectorObject *single = m_selection.size() == 1 ? m_document->find(m_selection.front()) : nullptr;
    if (single && single->kind == ObjectKind::frame && !single->autoLayout) {
        const QUuid id = single->id;
        edit(QStringLiteral("Add Auto Layout"), [&](VectorDocument &document) {
            // Reading the layout reorders the children, which moves objects: find the frame after.
            const AutoLayout layout = inferAutoLayout(document, id);
            VectorObject *frame = document.find(id);
            frame->autoLayout = layout;
            frame->layout.width = frame->layout.height = LayoutSizing::hug;
        });
        return;
    }
    const std::vector<QUuid> members = selectionInOrder();
    const QRectF box = m_document->bounds(members);
    VectorObject frame = VectorObject::frame(box, m_document->uniqueName(QStringLiteral("Frame")));
    // Wrapping keeps what's behind visible: no paper of its own.
    frame.fill = Paint::none();
    const QUuid frameID = frame.id;
    edit(QStringLiteral("Add Auto Layout"), [&](VectorDocument &document) {
        const QUuid top = members.back();
        const QUuid parent = *document.find(top)->parentID;
        document.insert(frame, parent, top);
        for (const QUuid &id : members)
            document.move(id, frameID, -1);
        const AutoLayout layout = inferAutoLayout(document, frameID);
        VectorObject *made = document.find(frameID);
        made->autoLayout = layout;
        made->layout.width = made->layout.height = LayoutSizing::hug;
        m_selection = {frameID};
    });
}

bool EditorSession::canRemoveAutoLayout() const
{
    const std::vector<QUuid> frames = selectedFrames();
    return std::any_of(frames.begin(), frames.end(), [&](const QUuid &id) { return m_document->find(id)->autoLayout.has_value(); });
}

void EditorSession::removeAutoLayout()
{
    if (!canRemoveAutoLayout())
        return;
    const std::vector<QUuid> frames = selectedFrames();
    edit(QStringLiteral("Remove Auto Layout"), [&](VectorDocument &document) {
        for (const QUuid &id : frames) {
            VectorObject *frame = document.find(id);
            frame->autoLayout.reset();
            // It keeps the size it has.
            frame->layout.width = frame->layout.height = LayoutSizing::fixed;
        }
    });
}

std::optional<AutoLayout> EditorSession::selectedAutoLayout() const
{
    const std::vector<QUuid> frames = selectedFrames();
    if (frames.empty())
        return std::nullopt;
    const std::optional<AutoLayout> first = m_document->find(frames.front())->autoLayout;
    return std::all_of(frames.begin(), frames.end(), [&](const QUuid &id) { return m_document->find(id)->autoLayout == first; }) ? first : std::nullopt;
}

void EditorSession::setAutoLayout(const AutoLayout &layout, const QString &editName)
{
    const std::vector<QUuid> frames = selectedFrames();
    if (frames.empty())
        return;
    edit(editName, [&](VectorDocument &document) {
        for (const QUuid &id : frames)
            document.find(id)->autoLayout = layout;
    });
}

void EditorSession::setLayoutSizing(Qt::Orientation axis, LayoutSizing sizing)
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> ids = m_selection;
    edit(QStringLiteral("Resizing"), [&](VectorDocument &document) {
        for (const QUuid &id : ids) {
            VectorObject *object = document.find(id);
            const VectorObject *parent = object && object->parentID ? document.find(*object->parentID) : nullptr;
            // Hug needs content that lays itself out; fill needs an auto-layout parent to give room.
            if (!object || (sizing == LayoutSizing::hug && !object->autoLayout && object->kind != ObjectKind::text)
                || (sizing == LayoutSizing::fill && !(parent && parent->autoLayout)))
                continue;
            (axis == Qt::Horizontal ? object->layout.width : object->layout.height) = sizing;
        }
    });
}

void EditorSession::setAbsolutePosition(bool absolute)
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> ids = m_selection;
    edit(absolute ? QStringLiteral("Absolute Position") : QStringLiteral("Auto Layout Position"), [&](VectorDocument &document) {
        for (const QUuid &id : ids)
            document.find(id)->layout.absolute = absolute;
    });
}

void EditorSession::setConstraint(Qt::Orientation axis, LayoutConstraint constraint)
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> ids = m_selection;
    edit(QStringLiteral("Constraints"), [&](VectorDocument &document) {
        for (const QUuid &id : ids)
            (axis == Qt::Horizontal ? document.find(id)->layout.horizontal : document.find(id)->layout.vertical) = constraint;
    });
}

std::vector<QUuid> EditorSession::selectedFrames() const
{
    std::vector<QUuid> frames;
    for (const QUuid &id : m_selection) {
        const VectorObject *object = m_document ? m_document->find(id) : nullptr;
        if (object && object->kind == ObjectKind::frame)
            frames.push_back(id);
    }
    return frames;
}

bool EditorSession::selectedFramesClip() const
{
    const std::vector<QUuid> frames = selectedFrames();
    return !frames.empty() && std::all_of(frames.begin(), frames.end(), [&](const QUuid &id) { return m_document->find(id)->clipsContent; });
}

void EditorSession::setClipsContent(bool clips)
{
    const std::vector<QUuid> frames = selectedFrames();
    if (frames.empty())
        return;
    edit(clips ? QStringLiteral("Clip Content") : QStringLiteral("Show Content Past the Frame"), [&](VectorDocument &document) {
        for (const QUuid &id : frames)
            document.find(id)->clipsContent = clips;
    });
}

QRectF EditorSession::framePlacement(QSizeF size) const
{
    if (!m_document)
        return QRectF(QPointF(0, 0), size);
    const QPointF middle = viewport.viewSize.isEmpty() ? m_document->artboard(m_activeArtboard).rect.center()
                                                       : viewport.documentPoint(viewport.center(), m_document->size);
    return QRectF(QPointF(std::round(middle.x() - size.width() / 2), std::round(middle.y() - size.height() / 2)), size);
}

QUuid EditorSession::addFrame(const QRectF &rect, const QString &name)
{
    const auto taken = [&](const QString &candidate) {
        return std::any_of(m_document->objects.begin(), m_document->objects.end(), [&](const VectorObject &o) { return o.name == candidate; });
    };
    QString label = QStringLiteral("Frame 1");
    if (m_document) {
        if (name.isEmpty()) {
            label = m_document->uniqueName(QStringLiteral("Frame"));
        } else {
            // A second pick of a preset is "iPhone 16 2": the first keeps the bare name.
            label = name;
            for (int number = 2; taken(label); ++number)
                label = QStringLiteral("%1 %2").arg(name).arg(number);
        }
    }
    VectorObject frame = VectorObject::frame(rect, label);
    const QUuid id = frame.id;
    edit(QStringLiteral("Frame"), [&](VectorDocument &document) {
        // Drawn inside a frame, it nests in the innermost one there, as Figma's do.
        std::optional<QUuid> host;
        for (const VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::frame && document.isOnCurrentPage(object.id) && document.isEffectivelyVisible(object.id)
                && !document.isEffectivelyLocked(object.id) && object.path.painterPath().contains(rect.normalized()))
                host = object.id;
        }
        if (host)
            document.insert(frame, *host);
        else
            insertNew(document, frame);
        m_selection = {id};
    });
    return id;
}

void EditorSession::ungroupSelection()
{
    if (!canUngroup())
        return;
    edit(QStringLiteral("Ungroup"), [&](VectorDocument &document) {
        std::vector<QUuid> released;
        for (const QUuid &id : selectionInOrder()) {
            const VectorObject *group = document.find(id);
            // A frame lets go of its children too (Figma's Remove Frame), its box going with it.
            if (!group || (group->kind != ObjectKind::group && group->kind != ObjectKind::frame)) {
                released.push_back(id);
                continue;
            }
            const QUuid parent = *group->parentID;
            const auto siblings = document.children(parent);
            int index = int(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            const auto kids = document.children(id);
            // Group opacity carries into its children.
            const double opacity = group->opacity;
            for (const QUuid &child : kids) {
                document.move(child, parent, index++);
                document.find(child)->opacity *= opacity;
                released.push_back(child);
            }
            document.remove({id});
        }
        m_selection = released;
    });
}

void EditorSession::makeClippingMask()
{
    if (!m_document || m_selection.size() < 2)
        return;
    groupSelection();
    const QUuid group = m_selection.front();
    edit(QStringLiteral("Make Clipping Mask"), [&](VectorDocument &document) {
        const auto kids = document.children(group);
        // The topmost object becomes the clip, moved to the bottom.
        document.move(kids.back(), group, 0);
        VectorObject *clip = document.find(kids.back());
        clip->fill = Paint::none();
        clip->stroke.paint = Paint::none();
        document.find(group)->isClipGroup = true;
        document.find(group)->name = QStringLiteral("Clip Group");
    });
}

void EditorSession::releaseClippingMask()
{
    if (!m_document)
        return;
    edit(QStringLiteral("Release Clipping Mask"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            VectorObject *group = document.find(id);
            if (group && group->isClipGroup) {
                group->isClipGroup = false;
                group->name = QStringLiteral("Group");
            }
        }
    });
}

void EditorSession::makeOpacityMask()
{
    if (!m_document || m_selection.size() < 2)
        return;
    groupSelection();
    const QUuid group = m_selection.front();
    edit(QStringLiteral("Make Mask"), [&](VectorDocument &document) {
        // Grouping keeps the topmost selected object last: it's already the mask.
        document.find(group)->mask = OpacityMask();
        document.find(group)->name = QStringLiteral("Mask Group");
    });
}

void EditorSession::releaseOpacityMask()
{
    if (!m_document)
        return;
    edit(QStringLiteral("Release Mask"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            VectorObject *group = document.find(id);
            if (group && group->mask) {
                group->mask.reset();
                group->name = QStringLiteral("Group");
            }
        }
    });
}

void EditorSession::setOpacityMaskClip(bool clip)
{
    if (!m_document)
        return;
    edit(QStringLiteral("Mask Clip"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            VectorObject *group = document.find(id);
            if (group && group->mask)
                group->mask->clip = clip;
        }
    });
}

void EditorSession::setOpacityMaskInverted(bool inverted)
{
    if (!m_document)
        return;
    edit(QStringLiteral("Invert Mask"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            VectorObject *group = document.find(id);
            if (group && group->mask)
                group->mask->inverted = inverted;
        }
    });
}

std::optional<QUuid> EditorSession::selectedMaskGroup() const
{
    if (!m_document || m_selection.size() != 1)
        return std::nullopt;
    const VectorObject *object = m_document->find(m_selection.front());
    return object && object->mask ? std::optional(object->id) : std::nullopt;
}

void EditorSession::arrange(ArrangeOrder order)
{
    if (!m_document || m_selection.empty())
        return;
    static const char *names[] = {"Bring to Front", "Bring Forward", "Send Backward", "Send to Back"};
    edit(QString::fromLatin1(names[int(order)]), [&](VectorDocument &document) {
        std::vector<QUuid> ids = selectionInOrder();
        // Moving front-most first keeps the others' relative order.
        if (order == ArrangeOrder::bringToFront || order == ArrangeOrder::bringForward)
            std::reverse(ids.begin(), ids.end());
        for (const QUuid &id : ids) {
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
            if (target == index)
                continue;
            // `move` counts the index among siblings without the moved one.
            document.move(id, parent, target == last ? -1 : target);
        }
    });
}

void EditorSession::align(AlignEdge edge, AlignTarget target)
{
    if (!m_document || m_selection.empty())
        return;
    // A key object, once clicked, is what the selection aligns to.
    const bool toKey = m_keyObject && target != AlignTarget::artboard;
    const QRectF reference = toKey ? m_document->bounds(*m_keyObject)
        : target == AlignTarget::artboard || m_selection.size() == 1 ? m_document->artboard(activeArtboard()).rect
                                                                     : selectionBounds();
    edit(QStringLiteral("Align"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            if (document.isEffectivelyLocked(id) || (toKey && id == *m_keyObject))
                continue;
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
    });
}

void EditorSession::moveSelection(QPointF delta)
{
    if (!m_pickedNodes.empty() && m_tool == Tool::directSelect && m_document) {
        edit(QStringLiteral("Move Points"), [&](VectorDocument &document) {
            for (const PickedNode &picked : m_pickedNodes) {
                if (VectorObject *object = document.find(picked.object)) {
                    if (PathNode *node = object->path.node(picked.node))
                        node->translate(delta);
                }
            }
        });
        return;
    }
    transformSelection(QTransform::fromTranslate(delta.x(), delta.y()), QStringLiteral("Move"));
}

void EditorSession::transformSelection(const QTransform &transform, const QString &editName, bool reflowAreaText)
{
    if (!m_document || m_selection.empty() || transform.isIdentity())
        return;
    // Set first: the edit's notification is what refreshes Transform Again's entry.
    m_lastTransform = RepeatTransform{transform, false, std::nullopt};
    edit(editName, [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            if (!document.isEffectivelyLocked(id))
                document.transform(id, transform, scaleStrokes, reflowAreaText, scaleCorners);
        }
    });
}

void EditorSession::transformEach(const std::function<QTransform(const QRectF &bounds)> &transform, const QString &editName, bool reflowAreaText)
{
    if (!m_document || m_selection.empty())
        return;
    std::vector<std::pair<QUuid, QTransform>> moves;
    for (const QUuid &id : m_selection) {
        if (m_document->isEffectivelyLocked(id))
            continue;
        const QTransform each = transform(m_document->bounds(id, false));
        if (!each.isIdentity())
            moves.push_back({id, each});
    }
    if (moves.empty())
        return;
    edit(editName, [&](VectorDocument &document) {
        for (const auto &[id, each] : moves)
            document.transform(id, each, scaleStrokes, reflowAreaText, scaleCorners);
    });
}

void EditorSession::rotateSelection(double degrees, std::optional<QPointF> pivot)
{
    const QPointF c = pivot.value_or(selectionBounds().center());
    QTransform transform;
    transform.translate(c.x(), c.y());
    transform.rotate(degrees);
    transform.translate(-c.x(), -c.y());
    transformSelection(transform, QStringLiteral("Rotate"));
    if (m_lastTransform && m_lastTransform->transform == transform)
        m_lastTransform->center = c;
}

void EditorSession::flipSelection(Qt::Orientation orientation)
{
    const QPointF c = selectionBounds().center();
    const bool horizontal = orientation == Qt::Horizontal;
    const QTransform transform = QTransform::fromTranslate(-c.x(), -c.y())
        * QTransform::fromScale(horizontal ? -1 : 1, horizontal ? 1 : -1) * QTransform::fromTranslate(c.x(), c.y());
    transformSelection(transform, horizontal ? QStringLiteral("Reflect Horizontal") : QStringLiteral("Reflect Vertical"));
    if (m_lastTransform && m_lastTransform->transform == transform)
        m_lastTransform->center = c;
}

void EditorSession::scaleSelection(double sx, double sy)
{
    if (!(sx > 0 && sy > 0))
        return;
    const QPointF c = selectionBounds().center();
    const QTransform transform = QTransform::fromTranslate(-c.x(), -c.y()) * QTransform::fromScale(sx, sy)
        * QTransform::fromTranslate(c.x(), c.y());
    transformSelection(transform, QStringLiteral("Scale"));
    if (m_lastTransform && m_lastTransform->transform == transform)
        m_lastTransform->center = c;
}

void EditorSession::combineSelection(BooleanOperation operation)
{
    if (!canCombine())
        return;
    static const char *names[] = {"Unite", "Intersect", "Minus Front", "Exclude"};
    edit(QString::fromLatin1(names[int(operation)]), [&](VectorDocument &document) {
        std::vector<QPainterPath> shapes;
        std::vector<QUuid> used;
        for (const QUuid &id : selectedLeaves()) {
            const VectorObject *object = document.find(id);
            if (!object || !object->hasPaint() || document.isEffectivelyLocked(id))
                continue;
            QPainterPath shape = object->outline();
            shape.setFillRule(Qt::WindingFill);
            shapes.push_back(shape);
            used.push_back(id);
        }
        if (shapes.size() < 2)
            return;
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
        result.path = VectorPath::fromPainterPath(combine(shapes, operation));
        const QUuid parent = *document.find(used.back())->parentID;
        document.insert(result, parent, used.back());
        document.remove(used);
        m_selection = result.path.isEmpty() ? std::vector<QUuid>{} : std::vector<QUuid>{result.id};
        if (result.path.isEmpty())
            document.remove({result.id});
    });
}

void EditorSession::outlineSelectedStrokes()
{
    if (!m_document)
        return;
    edit(QStringLiteral("Outline Stroke"), [&](VectorDocument &document) {
        std::vector<QUuid> results;
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (!object || !object->hasPaint() || !object->hasVisibleStroke()) {
                results.push_back(id);
                continue;
            }
            // Each visible stroke becomes a filled path, stacked as they drew.
            std::vector<VectorObject> outlines;
            for (const StrokeStyle &stroke : object->strokes()) {
                if (!stroke.isVisible())
                    continue;
                VectorObject outline = *object;
                outline.id = QUuid::createUuid();
                outline.kind = ObjectKind::path;
                outline.transform = {};
                outline.setFills({stroke.paint});
                outline.setStrokes({});
                outline.path = VectorPath::fromPainterPath(outlineStroke(object->outline(), stroke));
                outline.name = QStringLiteral("Outlined Stroke");
                outlines.push_back(outline);
            }
            const QUuid parent = *object->parentID;
            const bool keep = object->hasVisibleFill();
            if (keep) {
                object->setStrokes({});
                results.push_back(id);
            }
            QUuid below = id;
            for (const VectorObject &outline : outlines) {
                document.insert(outline, parent, below);
                below = outline.id;
                results.push_back(outline.id);
            }
            if (!keep)
                document.remove({id});
        }
        m_selection = results;
    });
}

void EditorSession::offsetSelection(double distance)
{
    if (!m_document || distance == 0)
        return;
    edit(QStringLiteral("Offset Path"), [&](VectorDocument &document) {
        std::vector<QUuid> results;
        for (const QUuid &id : selectedLeaves()) {
            const VectorObject *object = document.find(id);
            if (!object || object->kind != ObjectKind::path)
                continue;
            VectorObject offset = *object;
            offset.id = QUuid::createUuid();
            offset.path = VectorPath::fromPainterPath(offsetPath(object->outline(), distance, object->stroke.join));
            offset.name = QStringLiteral("Offset Path");
            const QUuid parent = *object->parentID;
            const auto siblings = document.children(parent);
            const int index = int(std::find(siblings.begin(), siblings.end(), id) - siblings.begin());
            // A grown copy goes behind the original, a shrunk one in front.
            document.insert(offset, parent, id);
            if (distance > 0)
                document.move(offset.id, parent, index);
            results.push_back(offset.id);
        }
        m_selection = results;
    });
}

void EditorSession::simplifySelection(double tolerance)
{
    if (!m_document)
        return;
    edit(QStringLiteral("Simplify"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (object && object->kind == ObjectKind::path)
                object->path = simplify(object->path, tolerance);
        }
    });
}

namespace {
// One path, or a group of one path per colour when runs have their own; each keeps the run's look.
void outlineText(VectorDocument &document, const QUuid &id)
{
    VectorObject &text = *document.find(id);
    const auto fills = text.text.fills();
    const QTransform place = text.transform;
    text.transform = {};
    if (fills.size() <= 1) {
        text.path = VectorPath::fromPainterPath(place.map(text.text.outline()));
        text.path.fillRule = Qt::WindingFill;
        text.kind = ObjectKind::path;
        text.text = {};
        return;
    }
    const VectorObject made = text;
    text.kind = ObjectKind::group;
    text.text = {};
    text.fill = Paint::none();
    text.stroke.paint = Paint::none();
    for (const auto &[color, glyphs] : fills) {
        VectorObject piece;
        piece.kind = ObjectKind::path;
        piece.name = made.name;
        piece.path = VectorPath::fromPainterPath(place.map(glyphs));
        piece.path.fillRule = Qt::WindingFill;
        piece.fill = color ? Paint::solid(*color) : made.fill;
        piece.stroke = made.stroke;
        document.insert(piece, id);
    }
}
}

void EditorSession::convertTextToPaths()
{
    if (!m_document)
        return;
    edit(QStringLiteral("Create Outlines"), [&](VectorDocument &document) {
        std::vector<QUuid> results;
        for (const QUuid &id : m_selection) {
            std::vector<QUuid> texts;
            const VectorObject *object = document.find(id);
            if (!object)
                continue;
            if (object->kind == ObjectKind::text)
                texts.push_back(id);
            for (const QUuid &nested : document.descendants(id)) {
                if (document.find(nested)->kind == ObjectKind::text)
                    texts.push_back(nested);
            }
            for (const QUuid &textID : texts)
                outlineText(document, textID);
            results.push_back(id);
        }
        m_selection = results;
    });
}

void EditorSession::makeCompoundPath()
{
    if (!m_document || m_selection.size() < 2)
        return;
    edit(QStringLiteral("Make Compound Path"), [&](VectorDocument &document) {
        std::vector<QUuid> used;
        VectorPath combined;
        combined.fillRule = Qt::OddEvenFill;
        for (const QUuid &id : selectedLeaves()) {
            const VectorObject *object = document.find(id);
            if (!object || object->kind != ObjectKind::path)
                continue;
            combined.contours.insert(combined.contours.end(), object->path.contours.begin(), object->path.contours.end());
            used.push_back(id);
        }
        if (used.size() < 2)
            return;
        VectorObject result = *document.find(used.back());
        result.id = QUuid::createUuid();
        result.path = combined;
        result.name = QStringLiteral("Compound Path");
        document.insert(result, *document.find(used.back())->parentID, used.back());
        document.remove(used);
        m_selection = {result.id};
    });
}

void EditorSession::releaseCompoundPath()
{
    if (!m_document)
        return;
    edit(QStringLiteral("Release Compound Path"), [&](VectorDocument &document) {
        std::vector<QUuid> results;
        for (const QUuid &id : selectionInOrder()) {
            const VectorObject *object = document.find(id);
            if (!object || object->kind != ObjectKind::path || object->path.contours.size() < 2) {
                results.push_back(id);
                continue;
            }
            const VectorObject original = *object;
            QUuid above = id;
            for (const Contour &contour : original.path.contours) {
                VectorObject part = original;
                part.id = QUuid::createUuid();
                part.path.contours = {contour};
                part.name = QStringLiteral("Path");
                document.insert(part, *original.parentID, above);
                above = part.id;
                results.push_back(part.id);
            }
            document.remove({id});
        }
        m_selection = results;
    });
}

void EditorSession::setFillOfSelection(const Paint &fill)
{
    if (m_selection.empty()) {
        setDefaultFill(fill);
        return;
    }
    if (fillTextRange(fill))
        return;
    m_defaultFill = fill;
    edit(QStringLiteral("Fill"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            // The fill's place in the stack keeps its opacity and blend.
            if (object && object->hasPaint() && !document.isEffectivelyLocked(id)) {
                Paint next = fill;
                next.opacity = object->fill.opacity;
                next.blendMode = object->fill.blendMode;
                object->fill = next;
                // A whole text's fill reaches the runs coloured on their own too.
                object->text.formatCharacters(0, int(object->text.text.size()), [](CharacterFormat &format) { format.fill.reset(); });
            }
        }
    });
}

void EditorSession::setStrokeOfSelection(const StrokeStyle &stroke)
{
    if (m_selection.empty()) {
        setDefaultStroke(stroke);
        return;
    }
    m_defaultStroke = stroke;
    edit(QStringLiteral("Stroke"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (object && object->hasPaint() && !document.isEffectivelyLocked(id))
                object->stroke = stroke;
        }
    });
}

void EditorSession::setOpacityOfSelection(double opacity)
{
    opacity = std::clamp(opacity, 0.0, 1.0);
    edit(QStringLiteral("Opacity"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            if (VectorObject *object = document.find(id))
                object->opacity = opacity;
        }
    });
}

void EditorSession::setBlendModeOfSelection(LayerBlendMode mode)
{
    edit(QStringLiteral("Blending Mode"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection) {
            if (VectorObject *object = document.find(id))
                object->blendMode = mode;
        }
    });
}

void EditorSession::pickStyle(const QUuid &from)
{
    if (!m_document)
        return;
    const VectorObject *source = m_document->find(from);
    if (!source || !source->hasPaint())
        return;
    const VectorObject style = *source;
    m_defaultFill = style.fill;
    m_defaultStroke = style.stroke;
    // A copied appearance keeps the eye on: the defaults paint new objects.
    m_defaultFill.isHidden = false;
    m_defaultStroke.paint.isHidden = false;
    if (m_selection.empty()) {
        notify(false);
        return;
    }
    edit(QStringLiteral("Eyedropper"), [&](VectorDocument &document) {
        for (const QUuid &id : selectedLeaves()) {
            VectorObject *object = document.find(id);
            if (object && object->hasPaint() && id != from) {
                object->setFills(style.fills());
                object->setStrokes(style.strokes());
                object->opacity = style.opacity;
                if (object->kind == ObjectKind::text && style.kind == ObjectKind::text)
                    applyCharacterStyle(object->text, style.text);
            }
        }
    });
}

void EditorSession::deletePickedNodes()
{
    if (!m_document || m_pickedNodes.empty())
        return;
    const std::vector<PickedNode> picked = m_pickedNodes;
    edit(QStringLiteral("Delete Points"), [&](VectorDocument &document) {
        std::vector<QUuid> emptied;
        for (const QUuid &id : [&] {
                 std::vector<QUuid> ids;
                 for (const PickedNode &p : picked) {
                     if (std::find(ids.begin(), ids.end(), p.object) == ids.end())
                         ids.push_back(p.object);
                 }
                 return ids;
             }()) {
            VectorObject *object = document.find(id);
            if (!object)
                continue;
            std::vector<NodeRef> refs;
            for (const PickedNode &p : picked) {
                if (p.object == id)
                    refs.push_back(p.node);
            }
            // Back to front, so indexes stay valid.
            std::sort(refs.begin(), refs.end(), std::greater<>());
            for (const NodeRef &ref : refs) {
                if (!object->path.node(ref))
                    continue;
                auto &nodes = object->path.contours[size_t(ref.contour)].nodes;
                nodes.erase(nodes.begin() + ref.node);
            }
            std::erase_if(object->path.contours, [](const Contour &contour) { return contour.nodes.size() < 2; });
            if (object->path.isEmpty())
                emptied.push_back(id);
        }
        document.remove(emptied);
        m_pickedNodes.clear();
    });
}

void EditorSession::closePath(const QUuid &id, int contour)
{
    if (!m_document)
        return;
    const VectorObject *object = m_document->find(id);
    if (!object || contour < 0 || contour >= int(object->path.contours.size()))
        return;
    edit(QStringLiteral("Close Path"), [&](VectorDocument &document) {
        document.find(id)->path.contours[size_t(contour)].closed = true;
    });
}

QUuid EditorSession::addLayer()
{
    if (!m_document)
        return {};
    VectorObject layer;
    layer.kind = ObjectKind::layer;
    const int count = int(m_document->layers().size());
    layer.name = QStringLiteral("Layer %1").arg(count + 1);
    layer.layerColor = nextLayerColor(count);
    const QUuid id = layer.id;
    edit(QStringLiteral("New Layer"), [&](VectorDocument &document) { document.appendLayer(layer); });
    m_activeLayer = id;
    notify(false);
    return id;
}

void EditorSession::rename(const QUuid &id, const QString &name)
{
    if (!m_document || !m_document->find(id) || m_document->find(id)->name == name || name.trimmed().isEmpty())
        return;
    edit(QStringLiteral("Rename"), [&](VectorDocument &document) { document.find(id)->name = name.trimmed(); });
}

void EditorSession::setVisible(const QUuid &id, bool visible)
{
    if (!m_document || !m_document->find(id) || m_document->find(id)->isVisible == visible)
        return;
    edit(visible ? QStringLiteral("Show") : QStringLiteral("Hide"), [&](VectorDocument &document) {
        document.find(id)->isVisible = visible;
        if (!visible)
            std::erase_if(m_selection, [&](const QUuid &s) { return s == id || document.isAncestor(id, s); });
    });
}

void EditorSession::setLocked(const QUuid &id, bool locked)
{
    if (!m_document || !m_document->find(id) || m_document->find(id)->isLocked == locked)
        return;
    edit(locked ? QStringLiteral("Lock") : QStringLiteral("Unlock"), [&](VectorDocument &document) {
        document.find(id)->isLocked = locked;
        if (locked)
            std::erase_if(m_selection, [&](const QUuid &s) { return s == id || document.isAncestor(id, s); });
    });
}

void EditorSession::setExpanded(const QUuid &id, bool expanded)
{
    // Opening a row is not an edit: no undo step, no modified flag.
    if (!m_document || !m_document->find(id))
        return;
    m_document->find(id)->isExpanded = expanded;
    notify(false);
}

bool EditorSession::moveObject(const QUuid &id, const QUuid &parent, int index)
{
    if (!m_document)
        return false;
    const VectorObject *object = m_document->find(id);
    const VectorObject *target = m_document->find(parent);
    if (!object || !target || !target->isContainer() || object->kind == ObjectKind::layer)
        return false;
    if (id == parent || m_document->isAncestor(id, parent))
        return false;
    bool moved = false;
    edit(QStringLiteral("Move to Layer"), [&](VectorDocument &document) { moved = document.move(id, parent, index); });
    return moved;
}

bool EditorSession::moveLayer(const QUuid &id, int index)
{
    if (!m_document)
        return false;
    const VectorObject *object = m_document->find(id);
    if (!object || object->kind != ObjectKind::layer)
        return false;
    const std::vector<QUuid> before = m_document->layers();
    bool moved = false;
    edit(QStringLiteral("Move Layer"), [&](VectorDocument &document) { moved = document.moveLayer(id, index) && document.layers() != before; });
    return moved;
}

void EditorSession::lockSelection()
{
    if (!m_document || m_selection.empty())
        return;
    edit(QStringLiteral("Lock"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection)
            document.find(id)->isLocked = true;
        m_selection.clear();
    });
}

void EditorSession::unlockAll()
{
    edit(QStringLiteral("Unlock All"), [&](VectorDocument &document) {
        std::vector<QUuid> unlocked;
        for (VectorObject &object : document.objects) {
            if (object.isLocked && object.kind != ObjectKind::layer && document.isOnCurrentPage(object.id)) {
                object.isLocked = false;
                unlocked.push_back(object.id);
            }
        }
        m_selection = unlocked;
    });
}

void EditorSession::hideSelection()
{
    if (!m_document || m_selection.empty())
        return;
    edit(QStringLiteral("Hide"), [&](VectorDocument &document) {
        for (const QUuid &id : m_selection)
            document.find(id)->isVisible = false;
        m_selection.clear();
    });
}

void EditorSession::showAll()
{
    edit(QStringLiteral("Show All"), [&](VectorDocument &document) {
        std::vector<QUuid> shown;
        for (VectorObject &object : document.objects) {
            if (!object.isVisible && object.kind != ObjectKind::layer && document.isOnCurrentPage(object.id)) {
                object.isVisible = true;
                shown.push_back(object.id);
            }
        }
        m_selection = shown;
    });
}

void EditorSession::copy() const
{
    if (!m_document || m_selection.empty())
        return;
    std::vector<VectorObject> objects;
    for (const QUuid &id : selectionInOrder()) {
        const int from = m_document->indexOf(id);
        const auto nested = m_document->descendants(id);
        objects.push_back(m_document->objects[size_t(from)]);
        objects.back().parentID.reset();
        for (const VectorObject &object : m_document->objects) {
            if (std::find(nested.begin(), nested.end(), object.id) != nested.end())
                objects.push_back(object);
        }
    }
    auto *data = new QMimeData;
    data->setData(QString::fromLatin1(DocumentCodec::clipboardMimeType),
                  QJsonDocument(DocumentCodec::encode(objects, false)).toJson(QJsonDocument::Compact));
    m_pasteCount = 0;
    QApplication::clipboard()->setMimeData(data);
}

void EditorSession::cut()
{
    copy();
    deleteObjects(m_selection);
}

namespace {
EditorSession::ExternalPasteHandler &externalPasteHandler()
{
    static EditorSession::ExternalPasteHandler handler;
    return handler;
}
}

void EditorSession::setExternalPasteHandler(ExternalPasteHandler handler)
{
    externalPasteHandler() = std::move(handler);
}

bool EditorSession::canPaste() const
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (!m_document || !data)
        return false;
    if (data->hasFormat(QString::fromLatin1(DocumentCodec::clipboardMimeType)) || data->hasImage())
        return true;
    const ExternalPasteHandler &handler = externalPasteHandler();
    return handler.recognises && handler.recognises(*data);
}

void EditorSession::paste(bool inPlace)
{
    paste(inPlace ? PastePosition::inPlace : PastePosition::offset);
}

void EditorSession::paste(PastePosition position)
{
    if (!m_document)
        return;
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (!data)
        return;
    std::vector<VectorObject> objects;
    if (data->hasFormat(QString::fromLatin1(DocumentCodec::clipboardMimeType))) {
        try {
            objects = DocumentCodec::decodeObjects(
                QJsonDocument::fromJson(data->data(QString::fromLatin1(DocumentCodec::clipboardMimeType))).array());
        } catch (const CodecError &) {
            return;
        }
    } else if (externalPasteHandler().read) {
        try {
            if (auto external = externalPasteHandler().read(*data)) {
                objects = std::move(external->objects);
                if (!external->warnings.isEmpty())
                    emit pasteLeftOut(external->warnings);
            }
        } catch (...) {
            return;
        }
    }
    if (objects.empty()) {
        if (data->hasImage())
            placeImage(qvariant_cast<QImage>(data->imageData()), QStringLiteral("Pasted Image"));
        return;
    }
    pasteObjects(std::move(objects), position);
}

void EditorSession::pasteObjects(std::vector<VectorObject> objects, PastePosition position)
{
    const double shift = position == PastePosition::offset ? 10.0 * ++m_pasteCount : 0;
    static const char *names[] = {"Paste", "Paste in Place", "Paste in Front", "Paste in Back"};
    // Front and back stack against the selection, inside its parent.
    const std::vector<QUuid> selected = selectionInOrder();
    const bool stacking = position == PastePosition::front || position == PastePosition::back;
    const std::optional<QUuid> neighbour = stacking && !selected.empty()
        ? std::optional(position == PastePosition::front ? selected.back() : selected.front())
        : std::nullopt;
    edit(QString::fromLatin1(names[int(position)]), [&](VectorDocument &document) {
        std::vector<std::pair<QUuid, QUuid>> renamed;
        for (VectorObject &object : objects) {
            const QUuid fresh = QUuid::createUuid();
            renamed.emplace_back(object.id, fresh);
            object.id = fresh;
            object.page = QUuid();
        }
        // A thread stays within what's pasted together; one that leaves it is dropped.
        for (VectorObject &object : objects) {
            if (object.text.threadNext.isNull())
                continue;
            const auto found = std::find_if(renamed.begin(), renamed.end(), [&](const auto &pair) { return pair.first == object.text.threadNext; });
            object.text.threadNext = found == renamed.end() ? QUuid() : found->second;
        }
        const std::optional<QUuid> layer = neighbour ? document.find(*neighbour)->parentID : isolatedGroup() ? isolatedGroup() : activeLayer();
        if (!layer)
            return;
        std::vector<QUuid> roots;
        for (VectorObject &object : objects) {
            if (!object.parentID) {
                roots.push_back(object.id);
                document.insert(object, *layer);
                continue;
            }
            for (const auto &[old, fresh] : renamed) {
                if (*object.parentID == old)
                    object.parentID = fresh;
            }
            document.insert(object, *object.parentID);
        }
        for (const QUuid &root : roots)
            document.transform(root, QTransform::fromTranslate(shift, shift));
        if (position == PastePosition::back || (position == PastePosition::front && neighbour)) {
            for (size_t index = 0; index < roots.size(); ++index) {
                std::vector<QUuid> siblings = document.children(*layer);
                std::erase(siblings, roots[index]);
                const auto at = neighbour ? std::find(siblings.begin(), siblings.end(), *neighbour) : siblings.end();
                // `move` counts among the siblings without the moved one; front keeps the copies in order above.
                int target = neighbour ? int(at - siblings.begin()) : int(index);
                if (position == PastePosition::front)
                    target += 1 + int(index);
                document.move(roots[index], *layer, target >= int(siblings.size()) ? -1 : target);
            }
        }
        m_selection = roots;
    });
}
