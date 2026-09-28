#include "Document/EditorSession.h"
#include <algorithm>

// Select menu ------------------------------------------------------------------

void EditorSession::runSelect(const std::function<void()> &command)
{
    if (!m_document)
        return;
    m_lastSelect = command;
    command();
}

void EditorSession::reselect()
{
    if (!m_lastSelect)
        return;
    // A copy: the command may replace what it's called from.
    const std::function<void()> command = m_lastSelect;
    command();
}

void EditorSession::selectInverse()
{
    runSelect([this] {
        std::vector<QUuid> ids;
        for (const QUuid &layer : m_document->layers()) {
            for (const QUuid &child : m_document->children(layer)) {
                if (!m_document->isEffectivelyVisible(child) || m_document->isEffectivelyLocked(child))
                    continue;
                // An object holding a selected one counts as selected.
                const bool taken = std::any_of(m_selection.begin(), m_selection.end(),
                                               [&](const QUuid &id) { return id == child || m_document->isAncestor(child, id); });
                if (!taken)
                    ids.push_back(child);
            }
        }
        select(ids);
    });
}

void EditorSession::selectAdjacent(bool above)
{
    runSelect([this, above] {
        const std::vector<QUuid> ordered = selectionInOrder();
        if (ordered.empty())
            return;
        const QUuid from = above ? ordered.back() : ordered.front();
        const std::vector<QUuid> siblings = m_document->children(m_document->find(from)->parentID);
        const int start = int(std::find(siblings.begin(), siblings.end(), from) - siblings.begin());
        const int step = above ? 1 : -1;
        for (int index = start + step; index >= 0 && index < int(siblings.size()); index += step) {
            const QUuid &id = siblings[size_t(index)];
            if (m_document->isEffectivelyVisible(id) && !m_document->isEffectivelyLocked(id)) {
                select({id});
                return;
            }
        }
    });
}

void EditorSession::selectChildren()
{
    if (!m_document)
        return;
    std::vector<QUuid> inside;
    for (const QUuid &id : selectionInOrder()) {
        for (const QUuid &child : m_document->children(id)) {
            if (m_document->isEffectivelyVisible(child) && !m_document->isEffectivelyLocked(child))
                inside.push_back(child);
        }
    }
    if (!inside.empty())
        select(inside);
}

void EditorSession::selectParent()
{
    if (!m_document)
        return;
    std::vector<QUuid> out;
    for (const QUuid &id : selectionInOrder()) {
        const VectorObject *object = m_document->find(id);
        const VectorObject *parent = object && object->parentID ? m_document->find(*object->parentID) : nullptr;
        // A layer isn't something to select: the top level has no parent.
        if (parent && parent->kind != ObjectKind::layer && std::find(out.begin(), out.end(), parent->id) == out.end())
            out.push_back(parent->id);
    }
    if (!out.empty())
        select(out);
}

void EditorSession::selectSibling(bool next)
{
    if (!m_document || m_selection.empty())
        return;
    const std::vector<QUuid> ordered = selectionInOrder();
    const QUuid from = next ? ordered.back() : ordered.front();
    const std::vector<QUuid> siblings = m_document->children(m_document->find(from)->parentID);
    const int count = int(siblings.size());
    const int start = int(std::find(siblings.begin(), siblings.end(), from) - siblings.begin());
    for (int step = 1; step < count; ++step) {
        const QUuid &id = siblings[size_t(((start + (next ? step : -step)) % count + count) % count)];
        if (m_document->isEffectivelyVisible(id) && !m_document->isEffectivelyLocked(id)) {
            select({id});
            return;
        }
    }
}

void EditorSession::selectSame(SameAttribute attribute)
{
    runSelect([this, attribute] {
        const std::vector<QUuid> leaves = selectedLeaves();
        // No match (a font asked of a shape) leaves the selection as it was.
        if (const std::vector<QUuid> found = leaves.empty() ? std::vector<QUuid>() : m_document->matching(leaves.front(), attribute); !found.empty())
            select(found);
    });
}

void EditorSession::selectObjects(ObjectFilter filter)
{
    runSelect([this, filter] {
        m_pickedNodes.clear();
        select(m_document->matching(filter));
    });
}

void EditorSession::selectAllOnSameLayers()
{
    runSelect([this] {
        std::vector<QUuid> layers;
        for (const QUuid &id : m_selection) {
            if (const std::optional<QUuid> layer = m_document->layerOf(id); layer && std::find(layers.begin(), layers.end(), *layer) == layers.end())
                layers.push_back(*layer);
        }
        std::vector<QUuid> ids;
        for (const QUuid &layer : layers) {
            for (const QUuid &child : m_document->children(layer)) {
                if (m_document->isEffectivelyVisible(child) && !m_document->isEffectivelyLocked(child))
                    ids.push_back(child);
            }
        }
        if (!ids.empty())
            select(ids);
    });
}

// Transform Again --------------------------------------------------------------

void EditorSession::transformAgain()
{
    if (!m_document || m_selection.empty() || !m_lastTransform)
        return;
    const RepeatTransform repeat = *m_lastTransform;
    QTransform transform = repeat.transform;
    if (repeat.center) {
        // Pivot on the selection's centre now, as the first one pivoted on its own.
        const QPointF shift = *repeat.center - selectionBounds().center();
        transform = QTransform::fromTranslate(shift.x(), shift.y()) * transform * QTransform::fromTranslate(-shift.x(), -shift.y());
    }
    if (repeat.duplicate) {
        edit(QStringLiteral("Transform Again"), [&](VectorDocument &document) {
            m_selection = duplicateInto(document, QPointF(0, 0));
            for (const QUuid &id : m_selection)
                document.transform(id, transform);
        });
    } else {
        transformSelection(transform, QStringLiteral("Transform Again"));
    }
    m_lastTransform = repeat;
}

// Layers panel -----------------------------------------------------------------

namespace {
std::vector<QUuid> othersBeside(const VectorDocument &document, const QUuid &id)
{
    const VectorObject *object = document.find(id);
    if (!object)
        return {};
    std::vector<QUuid> siblings = document.children(object->parentID);
    std::erase(siblings, id);
    return siblings;
}
}

bool EditorSession::anyOtherVisible(const QUuid &id) const
{
    if (!m_document)
        return false;
    const std::vector<QUuid> others = othersBeside(*m_document, id);
    return std::any_of(others.begin(), others.end(), [&](const QUuid &other) { return m_document->find(other)->isVisible; });
}

bool EditorSession::anyOtherUnlocked(const QUuid &id) const
{
    if (!m_document)
        return false;
    const std::vector<QUuid> others = othersBeside(*m_document, id);
    return std::any_of(others.begin(), others.end(), [&](const QUuid &other) { return !m_document->find(other)->isLocked; });
}

void EditorSession::setOthersVisible(const QUuid &id, bool visible)
{
    if (!m_document || !m_document->find(id))
        return;
    edit(visible ? QStringLiteral("Show Others") : QStringLiteral("Hide Others"), [&](VectorDocument &document) {
        for (const QUuid &other : othersBeside(document, id))
            document.find(other)->isVisible = visible;
        document.find(id)->isVisible = true;
        if (!visible)
            std::erase_if(m_selection, [&](const QUuid &s) { return !document.isEffectivelyVisible(s); });
    });
}

void EditorSession::setOthersLocked(const QUuid &id, bool locked)
{
    if (!m_document || !m_document->find(id))
        return;
    edit(locked ? QStringLiteral("Lock Others") : QStringLiteral("Unlock Others"), [&](VectorDocument &document) {
        for (const QUuid &other : othersBeside(document, id))
            document.find(other)->isLocked = locked;
        document.find(id)->isLocked = false;
        if (locked)
            std::erase_if(m_selection, [&](const QUuid &s) { return document.isEffectivelyLocked(s); });
    });
}

void EditorSession::setLayerColor(const QUuid &id, const QColor &color)
{
    const VectorObject *layer = m_document ? m_document->find(id) : nullptr;
    if (!layer || layer->kind != ObjectKind::layer || layer->layerColor == color)
        return;
    edit(QStringLiteral("Layer Color"), [&](VectorDocument &document) { document.find(id)->layerColor = color; });
}

void EditorSession::duplicateLayer(const QUuid &id)
{
    const VectorObject *layer = m_document ? m_document->find(id) : nullptr;
    if (!layer || layer->kind != ObjectKind::layer)
        return;
    std::vector<VectorObject> copies = m_document->copySubtree(id);
    copies.front().name = QStringLiteral("%1 copy").arg(layer->name);
    const QUuid copy = copies.front().id;
    edit(QStringLiteral("Duplicate Layer"), [&](VectorDocument &document) {
        // A layer's subtree is contiguous: the copy goes right after it, one layer up.
        const int from = document.indexOf(id);
        const size_t end = size_t(from) + 1 + document.descendants(id).size();
        document.objects.insert(document.objects.begin() + std::ptrdiff_t(end), copies.begin(), copies.end());
        m_selection.clear();
    });
    m_activeLayer = copy;
    notify(false);
}

// View -------------------------------------------------------------------------

void EditorSession::zoomToSelection()
{
    if (m_document && hasSelection())
        zoomToRect(selectionBounds(true));
}

void EditorSession::zoomToRect(const QRectF &rect)
{
    if (!m_document || viewport.viewSize.isEmpty() || (rect.isNull() && rect.topLeft().isNull()))
        return;
    // A line or a point still gets a sensible frame.
    const double width = std::max(rect.width(), 1.0), height = std::max(rect.height(), 1.0);
    const double fitted = std::min(std::max(1.0, viewport.viewSize.width() - 96) / width, std::max(1.0, viewport.viewSize.height() - 96) / height);
    viewport.setZoom(fitted * viewport.backingScale, viewport.center(), m_document->size);
    const QPointF shown = viewport.viewPoint(rect.center(), m_document->size);
    viewport.translate(QSizeF(viewport.center().x() - shown.x(), viewport.center().y() - shown.y()));
    notify(false);
}
