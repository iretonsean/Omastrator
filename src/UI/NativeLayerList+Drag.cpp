#include "UI/NativeLayerList.h"
#include <QDrag>
#include <QDragEnterEvent>
#include <QPainter>
#include <QScrollBar>

const QString NativeLayerList::rowType = QStringLiteral("application/x-omastrator-layer-rows");
const QString NativeLayerList::sourceType = QStringLiteral("application/x-omastrator-layer-list");

void LayerColumn::paintEvent(QPaintEvent *)
{
    if (!indicator)
        return;
    QPainter painter(this);
    QColor accent = palette().color(QPalette::Highlight);
    if (!indicatorFills) {
        painter.fillRect(*indicator, accent);
        return;
    }
    accent.setAlphaF(0.3f);
    painter.fillRect(*indicator, accent);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
    painter.drawRect(indicator->adjusted(1, 1, -1, -1));
}

// The pressed row, with the selection it belongs to.
std::unique_ptr<QMimeData> NativeLayerList::dragData(const LayerCell &cell) const
{
    const bool whole = m_session.isSelected(cell.objectID());
    QStringList lines;
    for (const Row &row : m_rows) {
        if (row.id == cell.objectID() || (whole && m_session.isSelected(row.id)))
            lines << row.id.toString(QUuid::WithoutBraces);
    }
    auto data = std::make_unique<QMimeData>();
    data->setData(rowType, lines.join(QLatin1Char('\n')).toUtf8());
    data->setData(sourceType, m_dragToken.toUtf8());
    return data;
}

void NativeLayerList::startDrag(const LayerCell &cell)
{
    auto *drag = new QDrag(this);
    drag->setMimeData(dragData(cell).release());
    drag->setPixmap(const_cast<LayerCell &>(cell).grab());
    drag->exec(Qt::MoveAction, Qt::MoveAction);
}

// Dragged art in panel order; group contents ride along.
std::vector<QUuid> NativeLayerList::draggedObjects(const QMimeData &data) const
{
    if (!data.hasFormat(rowType) || QString::fromUtf8(data.data(sourceType)) != m_dragToken || !m_session.document())
        return {};
    const VectorDocument &document = *m_session.document();
    std::vector<QUuid> named;
    for (const QString &line : QString::fromUtf8(data.data(rowType)).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QUuid id = QUuid::fromString(line);
        const VectorObject *object = document.find(id);
        if (object && object->kind != ObjectKind::layer)
            named.push_back(id);
    }
    std::vector<QUuid> result;
    for (const Row &row : m_rows) {
        const bool listed = std::find(named.begin(), named.end(), row.id) != named.end();
        const bool carried = std::any_of(named.begin(), named.end(), [&](const QUuid &other) { return document.isAncestor(other, row.id); });
        if (listed && !carried)
            result.push_back(row.id);
    }
    return result;
}

std::vector<QUuid> NativeLayerList::draggedLayers(const QMimeData &data) const
{
    if (!data.hasFormat(rowType) || QString::fromUtf8(data.data(sourceType)) != m_dragToken || !m_session.document())
        return {};
    const VectorDocument &document = *m_session.document();
    std::vector<QUuid> named;
    for (const QString &line : QString::fromUtf8(data.data(rowType)).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const VectorObject *object = document.find(QUuid::fromString(line));
        if (object && object->kind == ObjectKind::layer)
            named.push_back(object->id);
    }
    std::vector<QUuid> result;
    for (const Row &row : m_rows) {
        if (std::find(named.begin(), named.end(), row.id) != named.end())
            result.push_back(row.id);
    }
    return result;
}

std::optional<LayerDropTarget> NativeLayerList::layerDropTarget(const std::vector<QUuid> &layers, QPoint inColumn) const
{
    const VectorDocument &document = *m_session.document();
    std::optional<LayerDropTarget> target;
    for (size_t each = 0; each < m_cells.size() && !target; ++each) {
        const QRect box = m_cells[each]->geometry();
        if (inColumn.y() > box.bottom() + 1)
            continue;
        const VectorObject &object = *document.find(m_rows[each].id);
        // Only a layer's own row takes a layer: above its top half, below its bottom.
        if (object.kind != ObjectKind::layer)
            return std::nullopt;
        if (inColumn.y() < box.center().y()) {
            target = LayerDropTarget{.parent = QUuid(), .anchor = object.id, .above = true, .atTop = false, .row = int(each), .fills = false};
        } else {
            // Below the layer's last unfolded row.
            size_t end = each + 1;
            while (end < m_rows.size() && m_rows[end].depth > 0)
                ++end;
            target = LayerDropTarget{.parent = QUuid(), .anchor = object.id, .above = false, .atTop = false, .row = int(end), .fills = false};
        }
    }
    // Below every row: under the bottom layer.
    if (!target)
        target = LayerDropTarget{.parent = QUuid(), .anchor = document.layers().front(), .above = false, .atTop = false,
                                 .row = int(m_cells.size()), .fills = false};
    if (std::find(layers.begin(), layers.end(), *target->anchor) != layers.end())
        return std::nullopt;
    return target;
}

std::optional<LayerDropTarget> NativeLayerList::dropTarget(const QMimeData &data, QPoint listPoint) const
{
    if (m_cells.empty())
        return std::nullopt;
    const QPoint inColumn = m_column->mapFrom(viewport(), viewport()->mapFrom(this, listPoint));
    if (const std::vector<QUuid> layers = draggedLayers(data); !layers.empty())
        return layerDropTarget(layers, inColumn);
    const std::vector<QUuid> ids = draggedObjects(data);
    if (ids.empty())
        return std::nullopt;
    const VectorDocument &document = *m_session.document();
    std::optional<LayerDropTarget> target;
    for (size_t each = 0; each < m_cells.size() && !target; ++each) {
        const QRect box = m_cells[each]->geometry();
        if (inColumn.y() > box.bottom() + 1)
            continue;
        const VectorObject &object = *document.find(m_rows[each].id);
        const bool middle = std::abs(inColumn.y() - box.center().y()) <= box.height() / 4;
        // A layer or a group's middle takes rows on top.
        if (object.kind == ObjectKind::layer || (object.isContainer() && middle))
            target = LayerDropTarget{.parent = object.id, .anchor = std::nullopt, .above = false, .atTop = true, .row = int(each), .fills = true};
        else if (inColumn.y() < box.center().y())
            target = LayerDropTarget{.parent = object.parentID.value(), .anchor = object.id, .above = true, .atTop = false, .row = int(each), .fills = false};
        else
            target = LayerDropTarget{.parent = object.parentID.value(), .anchor = object.id, .above = false, .atTop = false, .row = int(each) + 1, .fills = false};
    }
    // Below every row: the bottom of the bottom layer.
    if (!target)
        target = LayerDropTarget{.parent = document.layers().front(), .anchor = std::nullopt, .above = false, .atTop = false,
                                 .row = int(m_cells.size()), .fills = false};
    for (const QUuid &id : ids) {
        if (id == target->parent || document.isAncestor(id, target->parent) || target->anchor == id)
            return std::nullopt;
    }
    return target;
}

bool NativeLayerList::acceptLayerDrop(const std::vector<QUuid> &layers, const LayerDropTarget &target)
{
    // The order the layers end in, bottom-up; then each is moved to its place.
    std::vector<QUuid> order = m_session.document()->layers();
    std::erase_if(order, [&](const QUuid &id) { return std::find(layers.begin(), layers.end(), id) != layers.end(); });
    const auto anchor = std::find(order.begin(), order.end(), *target.anchor);
    if (anchor == order.end())
        return false;
    const auto at = target.above ? anchor + 1 : anchor;
    order.insert(at, layers.rbegin(), layers.rend());
    m_session.beginEdit(layers.size() > 1 ? QStringLiteral("Move Layers") : QStringLiteral("Move Layer"));
    bool moved = false;
    for (size_t index = 0; index < order.size(); ++index) {
        if (m_session.document()->layers()[index] != order[index])
            moved = m_session.moveLayer(order[index], int(index)) || moved;
    }
    m_session.endEdit();
    return moved;
}

bool NativeLayerList::acceptDrop(const QMimeData &data, QPoint listPoint)
{
    const std::optional<LayerDropTarget> target = dropTarget(data, listPoint);
    if (!target)
        return false;
    if (const std::vector<QUuid> layers = draggedLayers(data); !layers.empty())
        return acceptLayerDrop(layers, *target);
    std::vector<QUuid> order = draggedObjects(data);
    // Rows that each land nearest the anchor go bottom-up.
    if ((target->anchor && !target->above) || (!target->anchor && target->atTop))
        std::reverse(order.begin(), order.end());
    m_session.beginEdit(order.size() > 1 ? QStringLiteral("Move Objects") : QStringLiteral("Move Object"));
    bool moved = false;
    for (const QUuid &id : order) {
        int index = target->atTop ? -1 : 0;
        if (target->anchor) {
            std::vector<QUuid> siblings = m_session.document()->children(target->parent);
            std::erase(siblings, id);
            const int at = int(std::find(siblings.begin(), siblings.end(), *target->anchor) - siblings.begin());
            index = target->above ? at + 1 : at;
        }
        moved = m_session.moveObject(id, target->parent, index) || moved;
    }
    m_session.select(draggedObjects(data));
    m_session.endEdit();
    return moved;
}

void NativeLayerList::showIndicator(const std::optional<LayerDropTarget> &target)
{
    m_column->indicator = std::nullopt;
    if (target && target->fills) {
        m_column->indicator = m_cells[size_t(target->row)]->geometry();
        m_column->indicatorFills = true;
    } else if (target) {
        const int y = target->row >= int(m_cells.size()) ? m_cells.back()->geometry().bottom() + 1 : m_cells[size_t(target->row)]->geometry().top() - 1;
        m_column->indicator = QRect(0, y - 1, m_column->width(), 2);
        m_column->indicatorFills = false;
    }
    m_column->update();
}

void NativeLayerList::dragEnterEvent(QDragEnterEvent *event)
{
    const std::optional<LayerDropTarget> target = dropTarget(*event->mimeData(), viewport()->mapToParent(event->position().toPoint()));
    showIndicator(target);
    if (!draggedObjects(*event->mimeData()).empty() || !draggedLayers(*event->mimeData()).empty()) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
}

void NativeLayerList::dragMoveEvent(QDragMoveEvent *event)
{
    // Near an edge the list scrolls, as a table does.
    m_dragPoint = event->position().toPoint();
    autoscroll(m_dragPoint);
    const std::optional<LayerDropTarget> target = dropTarget(*event->mimeData(), viewport()->mapToParent(m_dragPoint));
    showIndicator(target);
    if (!target) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

void NativeLayerList::autoscroll(QPoint inViewport)
{
    const int margin = 16;
    const int overshoot = inViewport.y() < margin ? inViewport.y() - margin : std::max(0, inViewport.y() - (viewport()->height() - margin));
    if (overshoot != 0)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + overshoot);
    if (overshoot != 0 && !m_edgeScroll.isActive())
        m_edgeScroll.start();
    else if (overshoot == 0)
        m_edgeScroll.stop();
}

void NativeLayerList::dragLeaveEvent(QDragLeaveEvent *)
{
    m_edgeScroll.stop();
    showIndicator(std::nullopt);
}

void NativeLayerList::dropEvent(QDropEvent *event)
{
    m_edgeScroll.stop();
    showIndicator(std::nullopt);
    if (!acceptDrop(*event->mimeData(), viewport()->mapToParent(event->position().toPoint())))
        return;
    event->setDropAction(Qt::MoveAction);
    event->accept();
}
