#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <algorithm>

namespace {
constexpr double artboardHandleReach = 6;
constexpr double minimumArtboardSide = 4;

QPen artboardCosmetic(const QColor &color, double width = 1)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}

// A square handle centred on `at`, as the selection tool draws its own.
void drawArtboardHandle(QPainter &painter, QPointF at, double size, const QColor &edge, const QColor &fill)
{
    painter.setPen(QPen(edge, 1));
    painter.setBrush(fill);
    painter.drawRect(QRectF(at.x() - size / 2, at.y() - size / 2, size, size));
}
}

std::optional<QRectF> EditorCanvas::State::activeArtboardBox() const
{
    if (!session.hasDocument() || session.document()->artboardCount() == 0)
        return std::nullopt;
    return session.document()->artboard(session.activeArtboard()).rect;
}

std::optional<int> EditorCanvas::State::artboardHandleAt(QPointF view) const
{
    if (session.tool() != Tool::artboard)
        return std::nullopt;
    const std::optional<QRectF> box = activeArtboardBox();
    if (!box)
        return std::nullopt;
    std::optional<int> best;
    double bestDistance = artboardHandleReach;
    for (int index = 0; index < 8; ++index) {
        if (!handleShown(*box, index))
            continue;
        const double distance = QLineF(view, handlePoint(*box, index)).length();
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

SmartGuides EditorCanvas::State::artboardGuides(int index) const
{
    const VectorDocument &document = *session.document();
    const Artboard board = document.artboard(index);
    // Art riding along with the artboard can't be a target for it.
    return guidesExcluding(session.artboardMovesArt ? document.artCenteredIn(board.rect) : std::vector<QUuid>{}, board.id);
}

void EditorCanvas::State::beginArtboardResize(int index, int handle, QPointF view)
{
    // An implicit artboard's id isn't stable enough to look up again, so the index is kept instead.
    beginDrag(DragKind::artboard, view);
    drag->handle = handle;
    drag->artboardIndex = index;
    drag->object = session.document()->artboard(index).id;
    drag->startBounds = session.document()->artboard(index).rect;
    drag->guides = artboardGuides(index);
}

void EditorCanvas::State::beginArtboardMove(int index, QPointF view, Qt::KeyboardModifiers modifiers)
{
    // Under Select the artboard becomes the selection; the Artboard tool only makes it the active one.
    const bool selecting = session.tool() == Tool::select;
    if (selecting)
        session.selectArtboard(index);
    else
        session.setActiveArtboard(index);
    if (modifiers.testFlag(Qt::AltModifier)) {
        // A committed duplicate, then a plain move drag repositions it.
        const QUuid copy = session.duplicateArtboard(index);
        index = session.document() ? session.document()->artboardIndex(copy) : -1;
        if (index < 0)
            return;
        if (selecting)
            session.selectArtboard(index);
    }
    beginDrag(DragKind::artboard, view);
    drag->handle = -2;
    drag->artboardIndex = index;
    drag->object = session.document()->artboard(index).id;
    drag->startBounds = session.document()->artboard(index).rect;
    drag->guides = artboardGuides(index);
}

void EditorCanvas::State::artboardPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!session.hasDocument())
        return;
    const VectorDocument &document = *session.document();
    const QPointF point = toDocument(view);
    if (const std::optional<int> handle = artboardHandleAt(view)) {
        beginArtboardResize(session.activeArtboard(), *handle, view);
        return;
    }
    const int hit = document.artboardAt(point);
    if (hit < 0) {
        // Empty canvas: draw a new artboard from here.
        beginDrag(DragKind::artboard, view);
        drag->handle = -1;
        drag->startBounds = QRectF(point, QSizeF(0, 0));
        return;
    }
    beginArtboardMove(hit, view, modifiers);
}

void EditorCanvas::State::dragArtboard(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!drag->started || !session.hasDocument())
        return;
    const QPointF current = toDocument(view);
    if (drag->object.isNull()) {
        // Still drawing a new one: just grow the preview rect; it becomes real on release.
        drag->startBounds = QRectF(drag->pressDocument, current).normalized();
        return;
    }
    const int index = drag->artboardIndex;
    if (index < 0 || index >= session.document()->artboardCount())
        return;
    if (!drag->interacting) {
        session.beginInteraction(drag->handle >= 0 ? QStringLiteral("Resize Artboard") : QStringLiteral("Move Artboard"));
        drag->interacting = true;
    }
    QRectF rect = drag->startBounds;
    if (drag->handle >= 0) {
        // The same handles as an object's: snapped, Shift keeps the ratio, Alt works from the centre.
        rect = handleScale(drag->startBounds, drag->handle, view, modifiers).mapRect(drag->startBounds);
        if (rect.width() < minimumArtboardSide)
            rect.setWidth(minimumArtboardSide);
        if (rect.height() < minimumArtboardSide)
            rect.setHeight(minimumArtboardSide);
    } else {
        rect.translate(snapMovement(drag->guides, drag->startBounds, current - drag->pressDocument, modifiers.testFlag(Qt::ShiftModifier)));
    }
    session.previewArtboardRect(index, rect);
}

void EditorCanvas::State::finishArtboard()
{
    if (drag->object.isNull()) {
        if (drag->started && drag->startBounds.width() >= minimumArtboardSide && drag->startBounds.height() >= minimumArtboardSide)
            session.addArtboard(drag->startBounds);
        return;
    }
    if (drag->interacting && session.isInteracting())
        session.commitInteraction();
}

void EditorCanvas::State::drawArtboardTool(QPainter &painter) const
{
    // The Artboard tool always shows the active one; the Select tool, only once it's selected.
    const bool selected = session.tool() == Tool::select && session.artboardSelected() && !text;
    if ((session.tool() != Tool::artboard && !selected) || !session.hasDocument())
        return;
    if (drag && drag->kind == DragKind::artboard && drag->object.isNull() && drag->started) {
        // A dashed preview of the artboard being drawn.
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        const QRectF area(toView(drag->startBounds.topLeft()), toView(drag->startBounds.bottomRight()));
        QPen dashed(accent(), 1);
        dashed.setDashPattern({4, 3});
        painter.setPen(dashed);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area);
        painter.restore();
        return;
    }
    const std::optional<QRectF> box = activeArtboardBox();
    if (!box)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor edge = accent();
    painter.setPen(artboardCosmetic(edge));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(toView(box->topLeft()), toView(box->bottomRight())));
    for (int index = 0; index < 8; ++index) {
        if (handleShown(*box, index))
            drawArtboardHandle(painter, handlePoint(*box, index), 7, edge, Qt::white);
    }
    painter.restore();
}

std::vector<std::pair<QUuid, QRectF>> EditorCanvas::State::frameLabels() const
{
    std::vector<std::pair<QUuid, QRectF>> labels;
    if (!session.hasDocument())
        return labels;
    const VectorDocument &document = *session.document();
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    for (const QUuid &layer : document.layers()) {
        for (const QUuid &id : document.children(layer)) {
            const VectorObject *object = document.find(id);
            if (!object || object->kind != ObjectKind::frame || !document.isEffectivelyVisible(id))
                continue;
            if (object->showsPage() && browserHost) {
                // A Browser View's name is part of its bar.
                for (const BrowserBarLayout &layout : browserBars()) {
                    if (layout.frame == id)
                        labels.emplace_back(id, layout.name);
                }
                continue;
            }
            const QRectF box = object->path.painterPath().boundingRect();
            const QPointF at = toView(box.topLeft()) - QPointF(0, 5);
            const double width = std::min(metrics.horizontalAdvance(object->name) + 2, std::max(24.0, toView(box.topRight()).x() - at.x()));
            labels.emplace_back(id, QRectF(at.x(), at.y() - metrics.height(), width, metrics.height()));
        }
    }
    return labels;
}

void EditorCanvas::State::drawFrameLabels(QPainter &painter) const
{
    const auto labels = frameLabels();
    if (labels.empty())
        return;
    QFont font = canvas.font();
    font.setPixelSize(11);
    painter.save();
    painter.setFont(font);
    for (const auto &[id, rect] : labels) {
        // The bar draws a Browser View's own.
        if (session.document()->find(id)->showsPage() && browserHost)
            continue;
        painter.setPen(session.isSelected(id) ? accent() : canvas.palette().color(QPalette::PlaceholderText));
        const QString name = QFontMetricsF(font).elidedText(session.document()->find(id)->name, Qt::ElideRight, rect.width());
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignBottom, name);
    }
    painter.restore();
}

std::optional<QUuid> EditorCanvas::State::frameLabelAt(QPointF view) const
{
    const auto labels = frameLabels();
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        if (it->second.adjusted(-2, -2, 2, 2).contains(view) && !session.document()->isEffectivelyLocked(it->first))
            return it->first;
    }
    return std::nullopt;
}

namespace {
// A board that doesn't export says so on its label.
QString labelText(const Artboard &board)
{
    return board.exported ? board.name : board.name + QStringLiteral(" · not exported");
}
}

std::vector<std::pair<int, QRectF>> EditorCanvas::State::artboardLabels() const
{
    std::vector<std::pair<int, QRectF>> labels;
    if (!session.hasDocument())
        return labels;
    const std::vector<Artboard> boards = session.document()->allArtboards();
    const Tool tool = session.tool();
    if (boards.size() < 2 && tool != Tool::artboard && tool != Tool::select)
        return labels;
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    const auto frames = frameLabels();
    for (int index = 0; index < int(boards.size()); ++index) {
        const Artboard &board = boards[size_t(index)];
        const QPointF at = toView(board.rect.topLeft()) - QPointF(0, 6);
        QRectF rect(at - QPointF(0, metrics.height()), QSizeF(metrics.horizontalAdvance(labelText(board)) + 2, metrics.height()));
        // A frame at the artboard's corner has its own name there: this one stacks above it.
        for (int tries = 0; tries < 8; ++tries) {
            const bool taken = std::any_of(frames.begin(), frames.end(), [&](const auto &frame) { return frame.second.intersects(rect); });
            if (!taken)
                break;
            rect.translate(0, -(metrics.height() + 2));
        }
        labels.emplace_back(index, rect);
    }
    return labels;
}

std::optional<int> EditorCanvas::State::artboardLabelAt(QPointF view) const
{
    const auto labels = artboardLabels();
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        if (it->second.adjusted(-2, -2, 2, 2).contains(view))
            return it->first;
    }
    return std::nullopt;
}

void EditorCanvas::State::drawArtboardLabels(QPainter &painter) const
{
    const auto labels = artboardLabels();
    if (labels.empty())
        return;
    QFont font = canvas.font();
    font.setPixelSize(11);
    painter.save();
    painter.setFont(font);
    for (const auto &[index, rect] : labels) {
        painter.setPen(index == session.activeArtboard() ? accent() : canvas.palette().color(QPalette::PlaceholderText));
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignBottom, labelText(session.document()->artboard(index)));
    }
    painter.restore();
}
