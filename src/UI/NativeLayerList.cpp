#include "UI/NativeLayerList.h"
#include "UI/KeyboardShortcuts.h"
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

namespace {
void unfold(const VectorDocument &document, const QUuid &id, int depth, std::vector<NativeLayerList::Row> &rows)
{
    rows.push_back({id, depth});
    const VectorObject *object = document.find(id);
    if (!object || !object->isContainer() || !object->isExpanded)
        return;
    // Children are stored bottom-up; the panel lists the top first.
    const std::vector<QUuid> children = document.children(id);
    for (auto child = children.rbegin(); child != children.rend(); ++child)
        unfold(document, *child, depth + 1, rows);
}
}

NativeLayerList::NativeLayerList(EditorSession &session, QWidget *parent)
    : QScrollArea(parent), m_session(session), m_column(new LayerColumn(this))
{
    setObjectName(QStringLiteral("layersList"));
    setAccessibleName(QStringLiteral("Layers"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setAcceptDrops(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    auto *rows = new QVBoxLayout(m_column);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(1);
    rows->addStretch(1);
    setWidget(m_column);
    m_column->setAutoFillBackground(false);
    connect(&m_session, &EditorSession::changed, this, &NativeLayerList::update);
    m_edgeScroll.setInterval(50);
    connect(&m_edgeScroll, &QTimer::timeout, this, [this] { autoscroll(m_dragPoint); });
    update();
}

std::vector<NativeLayerList::Row> NativeLayerList::rows(const VectorDocument &document)
{
    std::vector<Row> result;
    const std::vector<QUuid> layers = document.layers();
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer)
        unfold(document, *layer, 0, result);
    return result;
}

void NativeLayerList::update()
{
    const std::optional<VectorDocument> &document = m_session.document();
    std::vector<Row> next = document ? rows(*document) : std::vector<Row>();
    // Cells are made anew only when the ids change.
    const bool same = std::equal(next.begin(), next.end(), m_rows.begin(), m_rows.end(),
                                 [](const Row &a, const Row &b) { return a.id == b.id && a.depth == b.depth; });
    m_rows = std::move(next);
    if (!same) {
        auto *layout = static_cast<QVBoxLayout *>(m_column->layout());
        // Later: the rows may change under a row's own click.
        for (LayerCell *cell : m_cells) {
            layout->removeWidget(cell);
            cell->hide();
            cell->deleteLater();
        }
        m_cells.clear();
        for (size_t row = 0; row < m_rows.size(); ++row) {
            auto *cell = new LayerCell(*this);
            layout->insertWidget(int(row), cell);
            // A layout shows late children from the event loop.
            cell->show();
            m_cells.push_back(cell);
        }
        layout->activate();
    }
    for (size_t row = 0; row < m_rows.size(); ++row) {
        const VectorObject &object = *document->find(m_rows[row].id);
        const std::optional<QUuid> layer = document->layerOf(object.id);
        const QColor colour = layer ? document->find(*layer)->layerColor : QColor();
        m_cells[row]->configure(object, m_rows[row].depth, document->isEffectivelyVisible(object.id), colour);
    }
}

bool NativeLayerList::isHighlighted(const QUuid &id) const
{
    if (m_session.isSelected(id))
        return true;
    // With nothing selected, the active layer is highlighted.
    const VectorObject *object = m_session.document() ? m_session.document()->find(id) : nullptr;
    return object && object->kind == ObjectKind::layer && !m_session.hasSelection() && m_session.activeLayer() == id;
}

int NativeLayerList::rowAt(QPoint listPoint) const
{
    const QPoint inColumn = m_column->mapFrom(viewport(), viewport()->mapFrom(this, listPoint));
    for (size_t row = 0; row < m_cells.size(); ++row) {
        if (m_cells[row]->geometry().contains(inColumn))
            return int(row);
    }
    return -1;
}

void NativeLayerList::clickRow(const LayerCell &cell, Qt::KeyboardModifiers modifiers)
{
    setFocus(Qt::MouseFocusReason);
    const QUuid id = cell.objectID();
    if (cell.isLayer()) {
        // A layer row targets the layer for new objects.
        m_session.setActiveLayer(id);
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier)))
            m_session.deselectAll();
        m_anchor = std::nullopt;
        return;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        m_session.toggleSelected(id);
        m_anchor = id;
        return;
    }
    const auto position = [this](const QUuid &of) {
        return int(std::find_if(m_rows.begin(), m_rows.end(), [&](const Row &row) { return row.id == of; }) - m_rows.begin());
    };
    if (modifiers.testFlag(Qt::ShiftModifier) && m_anchor && position(*m_anchor) < int(m_rows.size())) {
        // Every object row from the anchor to here.
        const int from = std::min(position(*m_anchor), position(id)), to = std::max(position(*m_anchor), position(id));
        std::vector<QUuid> range;
        for (int row = from; row <= to; ++row) {
            if (m_session.document()->find(m_rows[size_t(row)].id)->kind != ObjectKind::layer)
                range.push_back(m_rows[size_t(row)].id);
        }
        m_session.select(range);
        return;
    }
    m_anchor = id;
    // Selected already, a press keeps the selection for a drag.
    if (!m_session.isSelected(id))
        m_session.select({id});
}

void NativeLayerList::mousePressEvent(QMouseEvent *event)
{
    // Below every row: the selection lets go.
    if (event->button() == Qt::LeftButton && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
        m_session.deselectAll();
    setFocus(Qt::MouseFocusReason);
}

void NativeLayerList::keyPressEvent(QKeyEvent *event)
{
    // Remapped keys arrive as the keys they stand for.
    const std::unique_ptr<QKeyEvent> typed = ShortcutSettings::shared().canvasEvent(*event);
    if (!typed)
        return;
    const ShortcutChord chord(*typed);
    const int key = typed->key();
    if (const std::optional<Tool> tool = ShortcutDefinition::tool(chord)) {
        m_session.selectTool(*tool);
    } else if (chord == ShortcutChord(QStringLiteral("x"))) {
        m_session.swapFillAndStroke();
    } else if (chord == ShortcutChord(QStringLiteral("d"))) {
        m_session.resetDefaultColors();
    } else if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && !typed->modifiers()) {
        m_session.deleteSelection();
    } else if ((key == Qt::Key_Up || key == Qt::Key_Down) && !(typed->modifiers() & ~Qt::KeypadModifier)) {
        // The row above or below the last selected one.
        const QUuid from = m_session.hasSelection() ? m_session.selection().back() : m_session.activeLayer().value_or(QUuid());
        const auto at = std::find_if(m_rows.begin(), m_rows.end(), [&](const Row &row) { return row.id == from; });
        const int next = int(at - m_rows.begin()) + (key == Qt::Key_Up ? -1 : 1);
        if (at == m_rows.end() || next < 0 || next >= int(m_rows.size()))
            return;
        clickRow(*m_cells[size_t(next)], Qt::NoModifier);
        ensureWidgetVisible(m_cells[size_t(next)], 0, 0);
    } else {
        QScrollArea::keyPressEvent(event);
    }
}

// A new palette: icons redraw once the widgets have it.
void NativeLayerList::changeEvent(QEvent *event)
{
    QScrollArea::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        QMetaObject::invokeMethod(this, &NativeLayerList::update, Qt::QueuedConnection);
}
