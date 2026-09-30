#include "Document/EditorSession.h"
#include <algorithm>

// Frames as Figma's: what's drawn over one goes in it, and a move drops the selection into the frame under the pointer.

namespace {
// An instance's children are copies of its master's, so nothing is drawn or dropped into one.
bool insideInstance(const VectorDocument &document, const QUuid &id)
{
    for (std::optional<QUuid> at = id; at;) {
        const VectorObject *object = document.find(*at);
        if (!object)
            return false;
        if (object->instance)
            return true;
        at = object->parentID;
    }
    return false;
}
}

std::vector<QUuid> EditorSession::transformRoots() const
{
    if (!m_document)
        return m_selection;
    std::vector<QUuid> roots;
    for (const QUuid &id : m_selection) {
        const bool inside = std::any_of(m_selection.begin(), m_selection.end(),
                                        [&](const QUuid &other) { return other != id && m_document->isAncestor(other, id); });
        if (!inside)
            roots.push_back(id);
    }
    return roots;
}

std::optional<QUuid> EditorSession::frameAt(QPointF point, const std::vector<QUuid> &excluding) const
{
    if (!m_document)
        return std::nullopt;
    const VectorDocument &document = *m_document;
    std::optional<QUuid> found;
    // Children follow their parent in the list and later siblings sit on top, so the last match is the topmost.
    for (const VectorObject &object : document.objects) {
        if (object.kind != ObjectKind::frame || !object.shape || object.autoLayout || !document.isOnCurrentPage(object.id)
            || !document.isEffectivelyVisible(object.id) || document.isEffectivelyLocked(object.id))
            continue;
        const bool left = std::any_of(excluding.begin(), excluding.end(),
                                      [&](const QUuid &out) { return out == object.id || document.isAncestor(out, object.id); });
        if (left || insideInstance(document, object.id) || !object.path.painterPath().contains(point))
            continue;
        found = object.id;
    }
    return found;
}

std::optional<QUuid> EditorSession::drawingParent(QPointF point) const
{
    // Inside an entered group, new objects stay in it.
    if (!m_document || isolatedGroup())
        return std::nullopt;
    if (const std::optional<QUuid> host = frameAt(point))
        return host;
    // Drawn outside every frame, it goes on the page even when the selection sits in a frame.
    const std::optional<QUuid> parent = insertionParent();
    if (!parent)
        return std::nullopt;
    bool framed = false;
    for (std::optional<QUuid> at = parent; at && !framed;) {
        const VectorObject *object = m_document->find(*at);
        framed = object && object->kind == ObjectKind::frame;
        at = object ? object->parentID : std::nullopt;
    }
    const std::optional<QUuid> layer = framed ? m_document->layerOf(*parent) : std::nullopt;
    if (!layer || m_document->isEffectivelyLocked(*layer) || !m_document->isEffectivelyVisible(*layer))
        return std::nullopt;
    return layer;
}

void EditorSession::previewDropAt(QPointF point)
{
    if (!m_document || !m_interaction || isolatedGroup())
        return;
    const std::vector<QUuid> roots = transformRoots();
    const std::optional<QUuid> target = frameAt(point, roots);
    bool moved = false;
    for (const QUuid &id : roots) {
        const VectorObject *object = m_document->find(id);
        if (!object || !object->parentID || object->kind == ObjectKind::layer || m_document->isEffectivelyLocked(id))
            continue;
        const VectorObject *parent = m_document->find(*object->parentID);
        // Only a frame's or a layer's own children change hands: a group keeps its own, and auto layout places its own.
        if (!parent || (parent->kind != ObjectKind::frame && parent->kind != ObjectKind::layer) || parent->autoLayout
            || insideInstance(*m_document, parent->id))
            continue;
        if (target) {
            if (*target != parent->id)
                moved = m_document->move(id, *target, -1) || moved;
            continue;
        }
        if (parent->kind != ObjectKind::frame)
            continue;
        // Out of every frame: just above the outermost frame it left, beside it.
        const VectorObject *outer = parent;
        for (const VectorObject *at = parent; at && at->parentID; at = m_document->find(*at->parentID)) {
            if (at->kind == ObjectKind::frame)
                outer = at;
        }
        if (!outer->parentID)
            continue;
        const QUuid holder = *outer->parentID;
        const std::vector<QUuid> siblings = m_document->children(holder);
        const auto found = std::find(siblings.begin(), siblings.end(), outer->id);
        moved = m_document->move(id, holder, int(found - siblings.begin()) + 1) || moved;
    }
    if (moved)
        notify();
}
