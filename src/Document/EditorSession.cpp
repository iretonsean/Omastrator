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
const std::array<ToolInfo, 18> toolInfo{{
    {Tool::select, "select", "Selection"},
    {Tool::directSelect, "directSelect", "Direct Selection"},
    {Tool::pen, "pen", "Pen"},
    {Tool::pencil, "pencil", "Pencil"},
    {Tool::text, "text", "Type"},
    {Tool::line, "line", "Line Segment"},
    {Tool::rectangle, "rectangle", "Rectangle"},
    {Tool::roundedRectangle, "roundedRectangle", "Rounded Rectangle"},
    {Tool::ellipse, "ellipse", "Ellipse"},
    {Tool::polygon, "polygon", "Polygon"},
    {Tool::star, "star", "Star"},
    {Tool::shapeBuilder, "shapeBuilder", "Shape Builder"},
    {Tool::rotate, "rotate", "Rotate"},
    {Tool::scale, "scale", "Scale"},
    {Tool::gradient, "gradient", "Gradient"},
    {Tool::eyedropper, "eyedropper", "Eyedropper"},
    {Tool::hand, "hand", "Hand"},
    {Tool::zoom, "zoom", "Zoom"},
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
}

void EditorSession::notify(bool documentToo)
{
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
    m_document = std::move(document);
    m_history.reset();
    m_selection.clear();
    m_pickedNodes.clear();
    const auto layers = m_document->layers();
    m_activeLayer = layers.empty() ? std::nullopt : std::optional(layers.back());
    viewport.fit(m_document->size);
    notify();
}

void EditorSession::closeDocument()
{
    m_interaction.reset();
    m_document.reset();
    m_history.reset();
    m_selection.clear();
    m_pickedNodes.clear();
    m_activeLayer.reset();
    notify();
}

void EditorSession::setArtboardSize(QSizeF size)
{
    if (!m_document || !(size.width() > 0 && size.height() > 0) || m_document->size == size)
        return;
    edit(QStringLiteral("Artboard Size"), [&](VectorDocument &document) { document.size = size; });
}

void EditorSession::setArtboardBackground(const QColor &color)
{
    if (!m_document || m_document->background == color)
        return;
    edit(QStringLiteral("Artboard Colour"), [&](VectorDocument &document) { document.background = color; });
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
    m_selection = std::move(kept);
    std::erase_if(m_pickedNodes, [&](const PickedNode &picked) {
        return !std::any_of(m_selection.begin(), m_selection.end(), [&](const QUuid &id) {
            return id == picked.object || (m_document && m_document->isAncestor(id, picked.object));
        });
    });
    if (!m_selection.empty() && m_document) {
        if (const auto layer = m_document->layerOf(m_selection.back()))
            m_activeLayer = layer;
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
    for (const QUuid &layer : m_document->layers()) {
        for (const QUuid &child : m_document->children(layer)) {
            if (m_document->isEffectivelyVisible(child) && !m_document->isEffectivelyLocked(child))
                ids.push_back(child);
        }
    }
    select(ids);
}

void EditorSession::deselectAll()
{
    m_pickedNodes.clear();
    select({});
    notify(false);
}

std::vector<QUuid> EditorSession::objectsIn(const QRectF &rect, bool deep) const
{
    std::vector<QUuid> result;
    if (!m_document)
        return result;
    const QRectF area = rect.normalized();
    for (const VectorObject &object : m_document->objects) {
        if (object.kind == ObjectKind::layer || !m_document->isEffectivelyVisible(object.id) || m_document->isEffectivelyLocked(object.id))
            continue;
        if (deep ? object.isContainer() : (!object.parentID || m_document->topLevelObject(object.id) != object.id))
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
        if (!object->isContainer()) {
            result.push_back(id);
            continue;
        }
        for (const QUuid &nested : m_document->descendants(id)) {
            const VectorObject *leaf = m_document->find(nested);
            if (leaf && !leaf->isContainer())
                result.push_back(nested);
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
        return;
    }
    std::erase_if(m_selection, [&](const QUuid &id) { return !m_document->find(id); });
    std::erase_if(m_pickedNodes, [&](const PickedNode &picked) {
        const VectorObject *object = m_document->find(picked.object);
        return !object || !object->path.node(picked.node);
    });
}

void EditorSession::beginEdit(const QString &name)
{
    m_history.begin(name, m_document, m_selection);
}

void EditorSession::endEdit()
{
    m_history.end(m_document, m_selection);
    notify();
}

void EditorSession::edit(const QString &name, const std::function<void(VectorDocument &)> &change)
{
    if (!m_document)
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
    m_document = snapshot.document;
    m_selection = snapshot.selection;
    m_pickedNodes.clear();
    pruneSelection();
    notify();
}

void EditorSession::undo()
{
    if (m_interaction)
        cancelInteraction();
    if (const auto snapshot = m_history.undo())
        restore(*snapshot);
}

void EditorSession::redo()
{
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
    if (!m_document)
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
            document.transform(id, transform, scaleStrokes, reflowAreaText);
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
    m_history.begin(interaction.name, m_document, interaction.selection);
    m_document = std::move(after);
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
    viewport.fit(m_document->size);
    notify(false);
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
        return object && object->kind == ObjectKind::group;
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
