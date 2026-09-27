#include "Canvas/EditorCanvasState.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <cmath>
#include <numbers>

namespace {
enum class CursorKind {
    arrow,
    whiteArrow,
    builderMerge,
    builderErase,
    duplicate,
    pen,
    penStart,
    penClose,
    penContinue,
    penJoin,
    penAdd,
    penDelete,
    penConvert,
    pencil,
    cross,
    ibeam,
    eyedropper,
    openHand,
    closedHand,
    zoomIn,
    zoomOut,
    rotate,
    sizeHorizontal,
    sizeVertical,
    sizeForward,
    sizeBackward,
};

QPixmap cursorPixmap(QSize size, double ratio)
{
    QPixmap pixmap(size * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

// A black stroke over a white one, so it reads on any artwork.
void strokeHaloed(QPainter &painter, const QPainterPath &path, double width)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, width + 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(QPen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
}

// The pointer arrow, tip at the origin.
QPainterPath arrowPath()
{
    QPainterPath arrow(QPointF(0, 0));
    for (const QPointF point : {QPointF(0, 16.5), QPointF(3.9, 12.8), QPointF(6.6, 19), QPointF(9.2, 17.9), QPointF(6.6, 11.8), QPointF(11.8, 11.8)})
        arrow.lineTo(point);
    arrow.closeSubpath();
    return arrow;
}

// Selection is a black arrow, Direct Selection a white one.
QCursor arrowCursor(bool white, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 28), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath arrow = arrowPath().translated(3, 2);
    painter.setPen(QPen(white ? Qt::black : Qt::white, white ? 1.2 : 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, white ? Qt::white : Qt::black);
    if (white) {
        painter.setPen(QPen(Qt::black, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(arrow);
    }
    return QCursor(pixmap, 3, 2);
}

// Shape Builder: the arrow with a plus that merges, or a minus that erases.
QCursor builderCursor(bool erase, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 30), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath arrow = arrowPath().translated(3, 2);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrow);
    painter.fillPath(arrow, Qt::black);
    const QPointF badge(20, 22);
    QPainterPath mark;
    mark.moveTo(badge - QPointF(3.5, 0));
    mark.lineTo(badge + QPointF(3.5, 0));
    if (!erase) {
        mark.moveTo(badge - QPointF(0, 3.5));
        mark.lineTo(badge + QPointF(0, 3.5));
    }
    strokeHaloed(painter, mark, 1.5);
    return QCursor(pixmap, 3, 2);
}

// Two arrows, back to front: Alt-drag copies.
QCursor duplicateCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 32), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(4, 3);
    for (const auto &[offset, fill, outline] : {std::tuple(5.0, Qt::white, Qt::black), std::tuple(0.0, Qt::black, Qt::white)}) {
        const QPainterPath arrow = arrowPath().translated(tip + QPointF(offset, offset));
        painter.setPen(QPen(outline, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(arrow);
        painter.fillPath(arrow, fill);
    }
    return QCursor(pixmap, 4, 3);
}

// The pen nib, tip at the hot spot; a badge says what a click does.
QCursor penCursor(CursorKind kind, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(28, 28), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPointF tip(3, 3);
    QPainterPath nib(tip);
    nib.lineTo(tip + QPointF(5, 13));
    nib.lineTo(tip + QPointF(9, 17));
    nib.lineTo(tip + QPointF(17, 9));
    nib.lineTo(tip + QPointF(13, 5));
    nib.closeSubpath();
    nib.moveTo(tip);
    nib.lineTo(tip + QPointF(7, 7));
    nib.addEllipse(tip + QPointF(8, 8), 1.4, 1.4);
    painter.fillPath(nib, Qt::white);
    strokeHaloed(painter, nib, 1.2);
    const QPointF badge = tip + QPointF(19, 19);
    QPainterPath mark;
    if (kind == CursorKind::penStart) {
        mark.moveTo(badge - QPointF(2.5, 2.5));
        mark.lineTo(badge + QPointF(2.5, 2.5));
        mark.moveTo(badge + QPointF(-2.5, 2.5));
        mark.lineTo(badge + QPointF(2.5, -2.5));
    } else if (kind == CursorKind::penClose) {
        mark.addEllipse(badge, 2.8, 2.8);
    } else if (kind == CursorKind::penContinue || kind == CursorKind::penJoin) {
        // A slash: the path goes on from here; joining adds the end it meets.
        mark.moveTo(badge + QPointF(-2.5, 3));
        mark.lineTo(badge + QPointF(2.5, -3));
        if (kind == CursorKind::penJoin)
            mark.addRect(QRectF(badge + QPointF(2, 1), QSizeF(3, 3)));
    } else if (kind == CursorKind::penAdd || kind == CursorKind::penDelete) {
        mark.moveTo(badge - QPointF(3, 0));
        mark.lineTo(badge + QPointF(3, 0));
        if (kind == CursorKind::penAdd) {
            mark.moveTo(badge - QPointF(0, 3));
            mark.lineTo(badge + QPointF(0, 3));
        }
    } else if (kind == CursorKind::penConvert) {
        // A caret: the anchor turns between corner and smooth.
        mark.moveTo(badge + QPointF(-3, 2));
        mark.lineTo(badge + QPointF(0, -2.5));
        mark.lineTo(badge + QPointF(3, 2));
    }
    if (!mark.isEmpty())
        strokeHaloed(painter, mark, 1.3);
    return QCursor(pixmap, int(tip.x()), int(tip.y()));
}

QCursor pencilCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // A pencil laid at 45°, point at the bottom left.
    QPainterPath body(QPointF(2, 21));
    body.lineTo(QPointF(4, 15));
    body.lineTo(QPointF(16, 3));
    body.lineTo(QPointF(20, 7));
    body.lineTo(QPointF(8, 19));
    body.closeSubpath();
    body.moveTo(QPointF(4, 15));
    body.lineTo(QPointF(8, 19));
    painter.fillPath(body, Qt::white);
    strokeHaloed(painter, body, 1.2);
    return QCursor(pixmap, 2, 21);
}

QCursor eyedropperCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // The dropper stands turned 45°, bulb at the top right, tip at the bottom left.
    const QTransform turned = QTransform().translate(12, 12).rotate(45);
    QPainterPath tube(QPointF(-1.9, -4));
    tube.lineTo(QPointF(-1.9, 5.8));
    tube.lineTo(QPointF(0, 9.3));
    tube.lineTo(QPointF(1.9, 5.8));
    tube.lineTo(QPointF(1.9, -4));
    tube.moveTo(QPointF(-4, -4));
    tube.lineTo(QPointF(4, -4));
    QPainterPath bulb;
    bulb.addRect(QRectF(-2.6, -9.5, 5.2, 4.9));
    strokeHaloed(painter, turned.map(tube), 1.5);
    painter.fillPath(turned.map(bulb), Qt::black);
    const QPointF tip = turned.map(QPointF(0, 9.3));
    return QCursor(pixmap, int(std::round(tip.x())), int(std::round(tip.y())));
}

// A haloed magnifier with a plus or a minus.
QCursor zoomCursor(bool out, double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    const QRectF glyph(2, 2, 20, 20);
    const QPointF center(glyph.left() + glyph.width() * 0.41, glyph.top() + glyph.height() * 0.40);
    const double radius = glyph.width() * 0.27;
    QPainterPath shape;
    shape.addEllipse(center, radius, radius);
    const QPointF grip = center + QPointF(radius, radius) * std::sqrt(0.5);
    shape.moveTo(grip);
    shape.lineTo(glyph.bottomRight() - QPointF(1, 1));
    shape.moveTo(center - QPointF(radius * 0.55, 0));
    shape.lineTo(center + QPointF(radius * 0.55, 0));
    if (!out) {
        shape.moveTo(center - QPointF(0, radius * 0.55));
        shape.lineTo(center + QPointF(0, radius * 0.55));
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(shape);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawEllipse(center, radius, radius);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.6, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(shape);
    return QCursor(pixmap, 10, 10);
}

// A quarter-turn arrow: dragging here rotates.
QCursor rotateCursor(double ratio)
{
    QPixmap pixmap = cursorPixmap(QSize(24, 24), ratio);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath arc;
    arc.arcMoveTo(QRectF(5, 5, 14, 14), 180);
    arc.arcTo(QRectF(5, 5, 14, 14), 180, -180);
    // Arrowheads at both ends.
    for (const auto &[end, direction] : {std::pair(QPointF(5, 12), QPointF(0, 1)), std::pair(QPointF(19, 12), QPointF(0, 1))}) {
        arc.moveTo(end + QPointF(-3, 0));
        arc.lineTo(end + direction * 3);
        arc.lineTo(end + QPointF(3, 0));
    }
    strokeHaloed(painter, arc, 1.4);
    return QCursor(pixmap, 12, 12);
}

QCursor build(CursorKind kind, double ratio)
{
    switch (kind) {
    case CursorKind::arrow:
        return arrowCursor(false, ratio);
    case CursorKind::whiteArrow:
        return arrowCursor(true, ratio);
    case CursorKind::builderMerge:
    case CursorKind::builderErase:
        return builderCursor(kind == CursorKind::builderErase, ratio);
    case CursorKind::duplicate:
        return duplicateCursor(ratio);
    case CursorKind::pen:
    case CursorKind::penStart:
    case CursorKind::penClose:
    case CursorKind::penContinue:
    case CursorKind::penJoin:
    case CursorKind::penAdd:
    case CursorKind::penDelete:
    case CursorKind::penConvert:
        return penCursor(kind, ratio);
    case CursorKind::pencil:
        return pencilCursor(ratio);
    case CursorKind::cross:
        return QCursor(Qt::CrossCursor);
    case CursorKind::ibeam:
        return QCursor(Qt::IBeamCursor);
    case CursorKind::eyedropper:
        return eyedropperCursor(ratio);
    case CursorKind::openHand:
        return QCursor(Qt::OpenHandCursor);
    case CursorKind::closedHand:
        return QCursor(Qt::ClosedHandCursor);
    case CursorKind::zoomIn:
    case CursorKind::zoomOut:
        return zoomCursor(kind == CursorKind::zoomOut, ratio);
    case CursorKind::rotate:
        return rotateCursor(ratio);
    case CursorKind::sizeHorizontal:
        return QCursor(Qt::SizeHorCursor);
    case CursorKind::sizeVertical:
        return QCursor(Qt::SizeVerCursor);
    case CursorKind::sizeForward:
        return QCursor(Qt::SizeFDiagCursor);
    case CursorKind::sizeBackward:
        return QCursor(Qt::SizeBDiagCursor);
    }
    return QCursor(Qt::ArrowCursor);
}

CursorKind handleCursor(int handle)
{
    switch (handle) {
    case 0:
    case 4:
        return CursorKind::sizeForward;
    case 2:
    case 6:
        return CursorKind::sizeBackward;
    case 1:
    case 5:
        return CursorKind::sizeVertical;
    default:
        return CursorKind::sizeHorizontal;
    }
}
}

void EditorCanvas::State::updateCursor()
{
    const Tool tool = session.tool();
    const bool alt = modifiers.testFlag(Qt::AltModifier);
    CursorKind kind = CursorKind::arrow;
    if (drag && drag->kind == DragKind::pan) {
        kind = CursorKind::closedHand;
    } else if (drag && drag->kind == DragKind::scale) {
        kind = handleCursor(drag->handle);
    } else if (drag && drag->kind == DragKind::rotate && tool == Tool::select) {
        kind = CursorKind::rotate;
    } else if (spaceHeld || tool == Tool::hand) {
        kind = CursorKind::openHand;
    } else if (text && hover && textBox().contains(toDocument(*hover))) {
        kind = CursorKind::ibeam;
    } else {
        switch (tool) {
        case Tool::select:
            if (hover && !drag) {
                if (const std::optional<int> handle = handleAt(*hover))
                    kind = handleCursor(*handle);
                else if (inRotateZone(*hover))
                    kind = CursorKind::rotate;
                else if (alt && hovered)
                    kind = CursorKind::duplicate;
            } else if (drag && drag->duplicate) {
                kind = CursorKind::duplicate;
            }
            break;
        case Tool::directSelect:
            kind = CursorKind::whiteArrow;
            break;
        case Tool::pen:
            // The badge says what a click would do.
            kind = pen ? CursorKind::pen : CursorKind::penStart;
            if (hover && !drag) {
                switch (penTargetAt(*hover, modifiers).action) {
                case PenAction::close:
                    kind = CursorKind::penClose;
                    break;
                case PenAction::resume:
                    kind = CursorKind::penContinue;
                    break;
                case PenAction::join:
                    kind = CursorKind::penJoin;
                    break;
                case PenAction::convert:
                    kind = CursorKind::penConvert;
                    break;
                case PenAction::removeAnchor:
                    kind = CursorKind::penDelete;
                    break;
                case PenAction::addAnchor:
                    kind = CursorKind::penAdd;
                    break;
                case PenAction::draw:
                    break;
                }
            }
            break;
        case Tool::shapeBuilder:
            kind = alt ? CursorKind::builderErase : CursorKind::builderMerge;
            break;
        case Tool::pencil:
            kind = CursorKind::pencil;
            break;
        case Tool::text:
            kind = CursorKind::ibeam;
            break;
        case Tool::eyedropper:
            kind = CursorKind::eyedropper;
            break;
        case Tool::zoom:
            kind = alt ? CursorKind::zoomOut : CursorKind::zoomIn;
            break;
        default:
            kind = CursorKind::cross;
            break;
        }
    }
    const double ratio = canvas.devicePixelRatioF();
    const int key = int(kind) * 1000 + int(std::round(ratio * 100));
    if (key == cursorKey)
        return;
    cursorKey = key;
    canvas.setCursor(build(kind, ratio));
}
