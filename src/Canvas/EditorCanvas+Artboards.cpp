#include "Canvas/EditorCanvasState.h"
#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <algorithm>
#include <array>

namespace {
// The same eight unit points EditorCanvas+Selection.cpp uses for its handles.
constexpr std::array<QPointF, 8> artboardHandleUnits{QPointF(0, 0), QPointF(0.5, 0), QPointF(1, 0), QPointF(1, 0.5),
                                                     QPointF(1, 1), QPointF(0.5, 1), QPointF(0, 1), QPointF(0, 0.5)};
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
    if (!session.hasDocument())
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

void EditorCanvas::State::artboardPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    if (!session.hasDocument())
        return;
    const VectorDocument &document = *session.document();
    const QPointF point = toDocument(view);
    if (const std::optional<int> handle = artboardHandleAt(view)) {
        // Resizing the active artboard: the opposite corner stays put. An implicit
        // artboard's id isn't stable enough to look up again, so the index is kept instead.
        const int index = session.activeArtboard();
        beginDrag(DragKind::artboard, view);
        drag->handle = *handle;
        drag->artboardIndex = index;
        drag->object = document.artboard(index).id;
        drag->startBounds = *activeArtboardBox();
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
    session.setActiveArtboard(hit);
    if (modifiers.testFlag(Qt::AltModifier)) {
        // A committed duplicate, then a plain move drag repositions it.
        const QUuid copy = session.duplicateArtboard(hit);
        const int copyIndex = session.document() ? session.document()->artboardIndex(copy) : -1;
        if (copyIndex < 0)
            return;
        beginDrag(DragKind::artboard, view);
        drag->handle = -2;
        drag->artboardIndex = copyIndex;
        drag->object = copy;
        drag->startBounds = session.document()->artboard(copyIndex).rect;
        return;
    }
    beginDrag(DragKind::artboard, view);
    drag->handle = -2;
    drag->artboardIndex = hit;
    drag->object = document.artboard(hit).id;
    drag->startBounds = document.artboard(hit).rect;
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
        const QPointF unit = artboardHandleUnits[size_t(drag->handle)];
        double left = rect.left(), right = rect.right(), top = rect.top(), bottom = rect.bottom();
        if (unit.x() == 0)
            left = current.x();
        else if (unit.x() == 1)
            right = current.x();
        if (unit.y() == 0)
            top = current.y();
        else if (unit.y() == 1)
            bottom = current.y();
        rect = QRectF(QPointF(std::min(left, right), std::min(top, bottom)), QPointF(std::max(left, right), std::max(top, bottom)));
        if (rect.width() < minimumArtboardSide)
            rect.setWidth(minimumArtboardSide);
        if (rect.height() < minimumArtboardSide)
            rect.setHeight(minimumArtboardSide);
        // Shift on a corner: keep the artboard's own proportions.
        if (modifiers.testFlag(Qt::ShiftModifier) && unit.x() != 0.5 && unit.y() != 0.5 && drag->startBounds.height() > 0) {
            const double ratio = drag->startBounds.width() / drag->startBounds.height();
            const double height = rect.width() / std::max(ratio, 1e-6);
            if (unit.y() == 0)
                rect.setTop(rect.bottom() - height);
            else
                rect.setHeight(height);
        }
    } else {
        const QPointF delta = current - drag->pressDocument;
        rect.translate(delta);
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
    if (session.tool() != Tool::artboard || !session.hasDocument())
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

void EditorCanvas::State::drawArtboardLabels(QPainter &painter) const
{
    if (!session.hasDocument())
        return;
    const std::vector<Artboard> boards = session.document()->allArtboards();
    if (boards.size() < 2 && session.tool() != Tool::artboard)
        return;
    QFont font = canvas.font();
    font.setPixelSize(11);
    const QFontMetricsF metrics(font);
    painter.save();
    painter.setFont(font);
    for (int index = 0; index < int(boards.size()); ++index) {
        const Artboard &board = boards[size_t(index)];
        const QPointF at = toView(board.rect.topLeft()) - QPointF(0, 6);
        const bool active = index == session.activeArtboard();
        painter.setPen(active ? accent() : canvas.palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(at - QPointF(0, metrics.height()), QSizeF(400, metrics.height())), Qt::AlignLeft | Qt::AlignBottom, board.name);
    }
    painter.restore();
}
