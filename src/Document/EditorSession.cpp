#include "Document/EditorSession.h"
#include "Document/ImageTrace.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
struct ToolInfo {
    Tool tool;
    const char *raw;
    const char *title;
};
const std::array<ToolInfo, 23> toolInfo{{
    {Tool::select, "select", "Selection"},
    {Tool::directSelect, "directSelect", "Direct Selection"},
    {Tool::pen, "pen", "Pen"},
    {Tool::pencil, "pencil", "Pencil"},
    {Tool::text, "text", "Type"},
    {Tool::typeOnPath, "typeOnPath", "Type on a Path"},
    {Tool::line, "line", "Line Segment"},
    {Tool::rectangle, "rectangle", "Rectangle"},
    {Tool::roundedRectangle, "roundedRectangle", "Rounded Rectangle"},
    {Tool::ellipse, "ellipse", "Ellipse"},
    {Tool::polygon, "polygon", "Polygon"},
    {Tool::star, "star", "Star"},
    {Tool::shapeBuilder, "shapeBuilder", "Shape Builder"},
    {Tool::scissors, "scissors", "Scissors"},
    {Tool::rotate, "rotate", "Rotate"},
    {Tool::scale, "scale", "Scale"},
    {Tool::gradient, "gradient", "Gradient"},
    {Tool::width, "width", "Width"},
    {Tool::eyedropper, "eyedropper", "Eyedropper"},
    {Tool::hand, "hand", "Hand"},
    {Tool::zoom, "zoom", "Zoom"},
    {Tool::artboard, "artboard", "Artboard"},
    {Tool::frame, "frame", "Frame"},
}};
}

QString rawValue(Tool tool)
{
    return QString::fromLatin1(toolInfo.at(size_t(tool)).raw);
}

std::optional<Tool> toolNamed(const QString &raw)
{
    for (const ToolInfo &info : toolInfo) {
        if (raw == QLatin1String(info.raw))
            return info.tool;
    }
    return std::nullopt;
}

QString title(Tool tool)
{
    return QString::fromLatin1(toolInfo.at(size_t(tool)).title);
}

bool isShapeTool(Tool tool)
{
    switch (tool) {
    case Tool::line:
    case Tool::rectangle:
    case Tool::roundedRectangle:
    case Tool::ellipse:
    case Tool::polygon:
    case Tool::star:
        return true;
    default:
        return false;
    }
}

EditorSession::EditorSession(QObject *parent) : QObject(parent)
{
    m_history.setEntryLimit(historyLimit());
}

void EditorSession::notify(bool documentToo)
{
    // A drag previews its instances too.
    if (documentToo && m_interaction)
        settle();
    // Wrap and thread previews follow a drag live, same as the committed edit.
    if (documentToo && m_document) {
        m_document->applyAutoLayout();
        m_document->reflowText();
    }
    // Artboard 1's size double as the viewport's reference point; keep the document
    // origin still on screen when it changes (a drag on the Artboard tool, or undo).
    if (documentToo && m_document && m_document->size != m_viewportDocumentSize) {
        const QSizeF delta = m_document->size - m_viewportDocumentSize;
        viewport.pan += QSizeF(delta.width() * viewport.pointsPerPixel() / 2, delta.height() * viewport.pointsPerPixel() / 2);
        m_viewportDocumentSize = m_document->size;
    }
    if (documentToo)
        emit documentChanged();
    emit changed();
}

void EditorSession::createDocument(QSizeF size)
{
    if (!(size.width() > 0 && size.height() > 0))
        return;
    loadDocument(VectorDocument::blank(size));
}

void EditorSession::loadDocument(VectorDocument document)
{
    m_interaction.reset();
    m_editDepth = 0;
    m_refusedEditDepths.clear();
    m_document = std::move(document);
    Components::sync(*m_document);
    m_history.reset();
    m_selection.clear();
    m_pickedNodes.clear();
    m_isolation.clear();
    m_keyObject.reset();
    const auto layers = m_document->layers();
    m_activeLayer = layers.empty() ? std::nullopt : std::optional(layers.back());
    m_activeArtboard = 0;
    m_artboardSelected = false;
    viewport.fit(m_document->size);
    m_viewportDocumentSize = m_document->size;
    notify();
}

void EditorSession::closeDocument()
{
    m_interaction.reset();
    m_editDepth = 0;
    m_refusedEditDepths.clear();
    m_document.reset();
    m_history.reset();
    m_selection.clear();
    m_pickedNodes.clear();
    m_isolation.clear();
    m_keyObject.reset();
    m_activeLayer.reset();
    m_artboardSelected = false;
    notify();
}

void EditorSession::setArtboardBackground(const QColor &color)
{
    if (!m_document)
        return;
    const int index = activeArtboard();
    if (m_document->artboard(index).background == color)
        return;
    edit(QStringLiteral("Artboard Colour"), [&](VectorDocument &document) {
        std::vector<Artboard> boards = document.allArtboards();
        boards[size_t(index)].background = color;
        document.setArtboards(boards);
    });
}

void EditorSession::selectTool(Tool tool)
{
    if (m_tool == tool)
        return;
    if (m_interaction && m_tool == Tool::pen && m_document) {
        // A pen path left at one anchor is no path; it goes without a trace.
        std::vector<QUuid> lone;
        for (const VectorObject &object : m_document->objects) {
            if (object.kind == ObjectKind::path && object.path.nodeCount() < 2 && !m_interaction->before.find(object.id))
                lone.push_back(object.id);
        }
        if (!lone.empty()) {
            m_document->remove(lone);
            pruneSelection();
        }
    }
    if (m_interaction)
        commitInteraction();
    m_tool = tool;
    if (tool != Tool::directSelect && tool != Tool::pen)
        m_pickedNodes.clear();
    notify(false);
}

void EditorSession::setDefaultFill(const Paint &fill)
{
    m_defaultFill = fill;
    notify(false);
}

void EditorSession::setDefaultStroke(const StrokeStyle &stroke)
{
    m_defaultStroke = stroke;
    notify(false);
}

void EditorSession::swapFillAndStroke()
{
    if (!m_selection.empty() && m_document) {
        edit(QStringLiteral("Swap Fill and Stroke"), [&](VectorDocument &document) {
            for (const QUuid &id : selectedLeaves()) {
                VectorObject *object = document.find(id);
                if (object && object->hasPaint())
                    std::swap(object->fill, object->stroke.paint);
            }
        });
    }
    std::swap(m_defaultFill, m_defaultStroke.paint);
    notify(false);
}

void EditorSession::resetDefaultColors()
{
    m_defaultFill = Paint::solid(Qt::white);
    m_defaultStroke = StrokeStyle{};
    if (!m_selection.empty() && m_document) {
        edit(QStringLiteral("Default Fill and Stroke"), [&](VectorDocument &document) {
            for (const QUuid &id : selectedLeaves()) {
                VectorObject *object = document.find(id);
                if (object && object->hasPaint()) {
                    object->setFills({m_defaultFill});
                    object->setStrokes({m_defaultStroke});
                }
            }
        });
    }
    notify(false);
}

bool EditorSession::isSelected(const QUuid &id) const
{
    return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

void EditorSession::select(const std::vector<QUuid> &ids)
{
    std::vector<QUuid> kept;
    if (m_document) {
        for (const QUuid &id : ids) {
            const VectorObject *object = m_document->find(id);
            if (object && object->kind != ObjectKind::layer && std::find(kept.begin(), kept.end(), id) == kept.end())
                kept.push_back(id);
        }
    }
    if (kept == m_selection)
        return;
    if (!kept.empty())
        m_artboardSelected = false;
    m_selection = std::move(kept);
    if (m_keyObject && (m_selection.size() < 2 || !isSelected(*m_keyObject)))
        m_keyObject.reset();
    std::erase_if(m_pickedNodes, [&](const PickedNode &picked) {
        return !std::any_of(m_selection.begin(), m_selection.end(), [&](const QUuid &id) {
            return id == picked.object || (m_document && m_document->isAncestor(id, picked.object));
        });
    });
    if (!m_selection.empty() && m_document) {
        if (const auto layer = m_document->layerOf(m_selection.back()))
            m_activeLayer = layer;
        // The selection's centre lands the active artboard on it too.
        const int at = m_document->artboardAt(selectionBounds().center());
        if (at >= 0)
            m_activeArtboard = at;
    }
    notify(false);
}

void EditorSession::toggleSelected(const QUuid &id)
{
    std::vector<QUuid> ids = m_selection;
    if (isSelected(id))
        std::erase(ids, id);
    else
        ids.push_back(id);
    select(ids);
}

void EditorSession::selectAll()
{
    if (!m_document)
        return;
    std::vector<QUuid> ids;
    // Isolated, Select All stays inside the group.
    const std::vector<QUuid> containers = isolatedGroup() ? std::vector<QUuid>{*isolatedGroup()} : m_document->layers();
    for (const QUuid &container : containers) {
        for (const QUuid &child : m_document->children(container)) {
            if (m_document->isEffectivelyVisible(child) && !m_document->isEffectivelyLocked(child))
                ids.push_back(child);
        }
    }
    select(ids);
}

void EditorSession::deselectAll()
{
    m_pickedNodes.clear();
    m_artboardSelected = false;
    select({});
    notify(false);
}

std::vector<QUuid> EditorSession::objectsIn(const QRectF &rect, bool deep) const
{
    std::vector<QUuid> result;
    if (!m_document)
        return result;
    const QRectF area = rect.normalized();
    const std::optional<QUuid> isolated = isolatedGroup();
    for (const VectorObject &object : m_document->objects) {
        // Isolated, only the group's own children count.
        if (isolated && !deep && object.parentID != isolated)
            continue;
        if (isolated && !m_document->isAncestor(*isolated, object.id))
            continue;
        if (object.kind == ObjectKind::layer || !m_document->isEffectivelyVisible(object.id) || m_document->isEffectivelyLocked(object.id))
            continue;
        if (deep ? object.isContainer() : (!isolated && (!object.parentID || m_document->topLevelObject(object.id) != object.id)))
            continue;
        const QRectF bounds = m_document->bounds(object.id);
        // Lines have no area; their box still counts.
        if (area.intersects(bounds.adjusted(-0.01, -0.01, 0.01, 0.01)))
            result.push_back(object.id);
    }
    return result;
}

QRectF EditorSession::selectionBounds(bool includeStroke) const
{
    return m_document ? m_document->bounds(m_selection, includeStroke) : QRectF();
}

std::vector<QUuid> EditorSession::selectedLeaves() const
{
    std::vector<QUuid> result;
    if (!m_document)
        return result;
    for (const QUuid &id : selectionInOrder()) {
        const VectorObject *object = m_document->find(id);
        if (!object)
            continue;
        // A frame paints itself: it answers for its own fills, strokes and box, not its children's.
        if (!object->isContainer() || object->kind == ObjectKind::frame) {
            result.push_back(id);
            continue;
        }
        const std::vector<QUuid> nested = m_document->descendants(id);
        for (size_t index = 0; index < nested.size(); ++index) {
            const VectorObject *leaf = m_document->find(nested[index]);
            if (!leaf || (leaf->isContainer() && leaf->kind != ObjectKind::frame))
                continue;
            result.push_back(nested[index]);
            // Descendants follow their parent: step past the frame's own.
            while (leaf->kind == ObjectKind::frame && index + 1 < nested.size() && m_document->isAncestor(leaf->id, nested[index + 1]))
                ++index;
        }
    }
    return result;
}

std::vector<QUuid> EditorSession::selectionInOrder() const
{
    // Document order: bottom to top.
    std::vector<QUuid> result;
    if (!m_document)
        return result;
    for (const VectorObject &object : m_document->objects) {
        if (isSelected(object.id))
            result.push_back(object.id);
    }
    return result;
}

void EditorSession::pickNodes(const std::vector<PickedNode> &nodes)
{
    if (nodes == m_pickedNodes)
        return;
    m_pickedNodes = nodes;
    notify(false);
}

std::optional<QUuid> EditorSession::activeLayer() const
{
    if (!m_document)
        return std::nullopt;
    if (m_activeLayer && m_document->find(*m_activeLayer))
        return m_activeLayer;
    const auto layers = m_document->layers();
    return layers.empty() ? std::nullopt : std::optional(layers.back());
}

void EditorSession::setActiveLayer(const QUuid &id)
{
    if (!m_document)
        return;
    m_activeLayer = m_document->layerOf(id);
    notify(false);
}

void EditorSession::pruneSelection()
{
    if (!m_document) {
        m_selection.clear();
        m_pickedNodes.clear();
        m_isolation.clear();
        m_keyObject.reset();
        return;
    }
    // An edited anchor ends a live shape.
    m_document->expandEditedShapes();
    m_document->applyAutoLayout();
    m_document->reflowText();
    std::erase_if(m_selection, [&](const QUuid &id) { return !m_document->find(id); });
    if (m_keyObject && (m_selection.size() < 2 || !isSelected(*m_keyObject)))
        m_keyObject.reset();
    // Isolation ends at the first group gone.
    const auto gone = std::find_if(m_isolation.begin(), m_isolation.end(), [&](const QUuid &id) {
        const VectorObject *group = m_document->find(id);
        return !group || group->kind != ObjectKind::group;
    });
    m_isolation.erase(gone, m_isolation.end());
    std::erase_if(m_pickedNodes, [&](const PickedNode &picked) {
        const VectorObject *object = m_document->find(picked.object);
        return !object || !object->path.node(picked.node);
    });
}

void EditorSession::beginEdit(const QString &name)
{
    ++m_editDepth;
    if (refuseWhenLocked()) {
        m_refusedEditDepths.push_back(m_editDepth);
        return;
    }
    // The preference may have changed since this document opened.
    m_history.setEntryLimit(historyLimit());
    m_history.begin(name, m_document, m_selection);
}

void EditorSession::endEdit()
{
    const int depth = m_editDepth;
    m_editDepth = std::max(0, m_editDepth - 1);
    if (!m_refusedEditDepths.empty() && m_refusedEditDepths.back() == depth) {
        m_refusedEditDepths.pop_back();
        return;
    }
    settle();
    m_history.end(m_document, m_selection);
    notify();
}

void EditorSession::edit(const QString &name, const std::function<void(VectorDocument &)> &change)
{
    if (!m_document || refuseWhenLocked())
        return;
    if (m_interaction)
        commitInteraction();
    beginEdit(name);
    change(*m_document);
    pruneSelection();
    endEdit();
}

void EditorSession::restore(const DocumentHistory::Snapshot &snapshot)
{
    const bool locked = isDocumentLocked();
    m_document = snapshot.document;
    if (m_document)
        m_document->locked = locked;
    m_selection = snapshot.selection;
    if (!m_selection.empty())
        m_artboardSelected = false;
    m_pickedNodes.clear();
    pruneSelection();
    notify();
}

// Inside an open beginEdit, DocumentHistory refuses both: the edit's own step would otherwise replay the undone one.
void EditorSession::undo()
{
    if (refuseWhenLocked())
        return;
    if (m_interaction)
        cancelInteraction();
    if (const auto snapshot = m_history.undo())
        restore(*snapshot);
}

void EditorSession::redo()
{
    if (refuseWhenLocked())
        return;
    if (m_interaction)
        cancelInteraction();
    if (const auto snapshot = m_history.redo())
        restore(*snapshot);
}

void EditorSession::markSaved()
{
    m_history.markSaved();
    notify(false);
}

void EditorSession::markUnsaved()
{
    m_history.markUnsaved();
    notify(false);
}

void EditorSession::beginInteraction(const QString &name)
{
    if (!m_document || refuseWhenLocked())
        return;
    if (m_interaction)
        commitInteraction();
    m_interaction = Interaction{name, *m_document, m_selection, *m_document, std::nullopt, false};
}

void EditorSession::previewTransform(const QTransform &transform, bool reflowAreaText)
{
    if (!m_document || !m_interaction)
        return;
    VectorDocument document = m_interaction->base;
    for (const QUuid &id : m_selection) {
        if (!document.isEffectivelyLocked(id))
            document.transform(id, transform, scaleStrokes, reflowAreaText, scaleCorners);
    }
    m_document = std::move(document);
    m_interaction->transform = transform;
    notify();
}

void EditorSession::previewObject(const VectorObject &object)
{
    if (!m_document || !m_interaction)
        return;
    if (VectorObject *target = m_document->find(object.id)) {
        *target = object;
        notify();
    }
}

const VectorObject *EditorSession::originalObject(const QUuid &id) const
{
    return m_interaction ? m_interaction->base.find(id) : nullptr;
}

void EditorSession::commitInteraction()
{
    if (!m_interaction)
        return;
    Interaction interaction = std::move(*m_interaction);
    m_interaction.reset();
    if (!m_document || *m_document == interaction.before) {
        notify();
        return;
    }
    // A drag's move, scale or rotate is what Transform Again repeats.
    if (interaction.transform && !interaction.transform->isIdentity())
        m_lastTransform = RepeatTransform{*interaction.transform, interaction.duplicated, std::nullopt};
    // Record the step as though it happened all at once.
    VectorDocument after = std::move(*m_document);
    m_document = std::move(interaction.before);
    m_history.setEntryLimit(historyLimit());
    m_history.begin(interaction.name, m_document, interaction.selection);
    m_document = std::move(after);
    settle();
    pruneSelection();
    m_history.end(m_document, m_selection);
    notify();
}

void EditorSession::cancelInteraction()
{
    if (!m_interaction)
        return;
    m_document = std::move(m_interaction->before);
    m_selection = std::move(m_interaction->selection);
    m_interaction.reset();
    pruneSelection();
    notify();
}

void EditorSession::zoomIn()
{
    if (!m_document)
        return;
    viewport.setZoom(viewport.zoom() * 2, viewport.center(), m_document->size);
    notify(false);
}

void EditorSession::zoomOut()
{
    if (!m_document)
        return;
    viewport.setZoom(viewport.zoom() / 2, viewport.center(), m_document->size);
    notify(false);
}

void EditorSession::zoomToFit()
{
    if (!m_document)
        return;
    const Artboard first = m_document->artboard(0);
    if (m_document->artboardCount() == 1 && first.rect.topLeft() == QPointF(0, 0)) {
        viewport.fit(m_document->size);
        notify(false);
        return;
    }
    zoomToRect(m_document->artboard(activeArtboard()).rect);
}

void EditorSession::actualSize()
{
    if (!m_document)
        return;
    viewport.setZoom(viewport.backingScale, viewport.center(), m_document->size);
    notify(false);
}

void EditorSession::setZoom(double zoom, QPointF anchoredAt)
{
    if (!m_document)
        return;
    viewport.setZoom(zoom, anchoredAt, m_document->size);
    notify(false);
}

void EditorSession::panView(QSizeF by)
{
    if (by.isNull())
        return;
    viewport.translate(by);
    notify(false);
}

void EditorSession::resizeView(QSizeF size, double backingScale)
{
    if (viewport.viewSize == size && viewport.backingScale == backingScale)
        return;
    viewport.resize(size, backingScale, m_document ? std::optional(m_document->size) : std::nullopt);
    notify(false);
}

void EditorSession::setShowsGrid(bool shown)
{
    showsGrid = shown;
    notify();
}

void EditorSession::setSnapsToGrid(bool snaps)
{
    snapsToGrid = snaps;
    notify(false);
}

void EditorSession::setShowsOutline(bool shown)
{
    showsOutline = shown;
    notify();
}

QPointF EditorSession::snapped(QPointF point) const
{
    if (!snapsToGrid || gridSpacing <= 0)
        return point;
    return {std::round(point.x() / gridSpacing) * gridSpacing, std::round(point.y() / gridSpacing) * gridSpacing};
}

bool EditorSession::canGroup() const
{
    return !m_selection.empty();
}

bool EditorSession::canUngroup() const
{
    if (!m_document)
        return false;
    return std::any_of(m_selection.begin(), m_selection.end(), [&](const QUuid &id) {
        const VectorObject *object = m_document->find(id);
        return object && (object->kind == ObjectKind::group || object->kind == ObjectKind::frame);
    });
}

bool EditorSession::canCombine() const
{
    if (!m_document)
        return false;
    int paths = 0;
    for (const QUuid &id : selectedLeaves()) {
        const VectorObject *object = m_document->find(id);
        if (object && object->hasPaint())
            ++paths;
    }
    return paths >= 2;
}

void EditorSession::previewDocument(const VectorDocument &document, const std::vector<QUuid> &selection)
{
    if (!m_document || !m_interaction)
        return;
    m_document = document;
    m_selection = selection;
    pruneSelection();
    notify();
}

bool EditorSession::previewShapeBuild(const ShapeBuilder::Arrangement &arrangement, const ShapeBuilder::Gesture &gesture)
{
    if (!m_document || !m_interaction)
        return false;
    VectorDocument document = m_interaction->base;
    std::vector<QUuid> created;
    if (!ShapeBuilder::build(document, arrangement, gesture, shapeBuilder, m_defaultFill, m_defaultStroke, &created)) {
        previewDocument(m_interaction->base, m_interaction->selection);
        return false;
    }
    // Illustrator keeps the built shapes selected, so the next drag works on them too.
    std::vector<QUuid> selection;
    std::vector<std::optional<QUuid>> parents;
    for (const QUuid &id : m_interaction->selection) {
        if (const VectorObject *object = m_interaction->base.find(id))
            parents.push_back(object->parentID);
        if (document.find(id))
            selection.push_back(id);
    }
    for (const QUuid &id : created) {
        const VectorObject *object = document.find(id);
        const VectorObject *parent = object && object->parentID ? document.find(*object->parentID) : nullptr;
        if (parent && (parent->kind == ObjectKind::layer || std::find(parents.begin(), parents.end(), object->parentID) != parents.end()))
            selection.push_back(id);
    }
    previewDocument(document, selection);
    return true;
}

std::optional<QUuid> EditorSession::selectedImage() const
{
    std::optional<QUuid> found;
    for (const QUuid &id : selectedLeaves()) {
        if (m_document->find(id)->kind != ObjectKind::image)
            continue;
        // Two images leave the choice open: nothing to trace.
        if (found)
            return std::nullopt;
        found = id;
    }
    return found;
}

void EditorSession::traceSelectedImage(int colors)
{
    const std::optional<QUuid> image = selectedImage();
    if (!image || m_document->isEffectivelyLocked(*image))
        return;
    ImageTrace::Options options;
    options.colors = std::clamp(colors, 1, 16);
    edit(QStringLiteral("Image Trace"), [&](VectorDocument &document) {
        if (const auto group = ImageTrace::traceInPlace(document, *image, options))
            m_selection = {*group};
    });
}
