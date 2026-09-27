#include "UI/ToolIcons.h"
#include <QPainterPath>
#include <cmath>
#include <numbers>

namespace {
// The Selection tool's arrow; Direct Selection draws it hollow.
QPainterPath arrow()
{
    QPainterPath path(QPointF(4, 1.8));
    path.lineTo(QPointF(4, 15));
    path.lineTo(QPointF(7.3, 11.9));
    path.lineTo(QPointF(9.7, 16.4));
    path.lineTo(QPointF(11.8, 15.3));
    path.lineTo(QPointF(9.5, 10.9));
    path.lineTo(QPointF(14, 10.6));
    path.closeSubpath();
    return path;
}

void select(QPainter &painter)
{
    painter.setBrush(painter.pen().color());
    painter.drawPath(arrow());
}

void directSelect(QPainter &painter)
{
    QPen pen = painter.pen();
    pen.setWidthF(1.2);
    painter.setPen(pen);
    painter.drawPath(arrow());
}

// A nib pointing down, its slit and breather hole.
void pen(QPainter &painter)
{
    QPainterPath nib(QPointF(9, 16.5));
    nib.lineTo(QPointF(4.2, 8.6));
    nib.lineTo(QPointF(6.4, 4.2));
    nib.lineTo(QPointF(11.6, 4.2));
    nib.lineTo(QPointF(13.8, 8.6));
    nib.closeSubpath();
    painter.drawPath(nib);
    painter.drawLine(QPointF(9, 16.5), QPointF(9, 10.4));
    painter.drawEllipse(QPointF(9, 9.2), 1.2, 1.2);
    painter.drawLine(QPointF(6, 1.8), QPointF(12, 1.8));
}

void pencil(QPainter &painter)
{
    QPainterPath body(QPointF(2.4, 15.6));
    body.lineTo(QPointF(4.4, 10.9));
    body.lineTo(QPointF(12.4, 2.9));
    body.lineTo(QPointF(15.1, 5.6));
    body.lineTo(QPointF(7.1, 13.6));
    body.closeSubpath();
    painter.drawPath(body);
    painter.drawLine(QPointF(4.4, 10.9), QPointF(7.1, 13.6));
    painter.drawLine(QPointF(10.6, 4.7), QPointF(13.3, 7.4));
}

void type(QPainter &painter)
{
    QPen pen = painter.pen();
    pen.setWidthF(1.8);
    painter.setPen(pen);
    painter.drawLine(QPointF(3.5, 3.5), QPointF(14.5, 3.5));
    painter.drawLine(QPointF(9, 3.5), QPointF(9, 15));
    painter.drawLine(QPointF(6.8, 15), QPointF(11.2, 15));
}

// A regular polygon or star about the square's centre.
QPolygonF ring(int corners, double outer, double inner)
{
    QPolygonF points;
    const int count = inner > 0 ? corners * 2 : corners;
    for (int index = 0; index < count; ++index) {
        const double radius = inner > 0 && index % 2 ? inner : outer;
        const double angle = -std::numbers::pi / 2 + index * 2 * std::numbers::pi / count;
        points << QPointF(9 + radius * std::cos(angle), 9.6 + radius * std::sin(angle));
    }
    return points;
}

// A three-quarter turn ending in an arrowhead.
void rotate(QPainter &painter)
{
    QPainterPath turn;
    turn.arcMoveTo(QRectF(3, 3, 12, 12), 90);
    turn.arcTo(QRectF(3, 3, 12, 12), 90, 270);
    painter.drawPath(turn);
    painter.drawPolyline(QPolygonF{QPointF(12, 6.5), QPointF(15, 9), QPointF(17.2, 6)});
}

void scale(QPainter &painter)
{
    painter.drawRect(QRectF(2.5, 2.5, 13, 13));
    painter.fillRect(QRectF(2.5, 10, 5.5, 5.5), painter.pen().color());
    painter.drawLine(QPointF(9, 9), QPointF(13, 5));
    painter.drawPolyline(QPolygonF{QPointF(9.8, 5), QPointF(13, 5), QPointF(13, 8.2)});
}

// A pipette: tip, glass, collar and bulb, on a diagonal.
void eyedropper(QPainter &painter)
{
    painter.save();
    painter.translate(9, 9);
    painter.rotate(-45);
    painter.drawLine(QPointF(-8.5, 0), QPointF(-5.5, 0));
    painter.drawRoundedRect(QRectF(-5.5, -1.8, 7.5, 3.6), 1, 1);
    painter.drawLine(QPointF(2, -3.2), QPointF(2, 3.2));
    painter.setBrush(painter.pen().color());
    painter.drawRoundedRect(QRectF(3, -2.4, 5.5, 4.8), 2.4, 2.4);
    painter.restore();
}

// A square shaded towards its right edge: clear, a tint, then solid.
void gradient(QPainter &painter)
{
    QColor tint = painter.pen().color();
    painter.drawRect(QRectF(2.5, 2.5, 13, 13));
    tint.setAlphaF(0.45);
    painter.fillRect(QRectF(7.5, 2.5, 4, 13), tint);
    painter.fillRect(QRectF(11.5, 2.5, 4, 13), painter.pen().color());
}

void hand(QPainter &painter)
{
    // Four fingers over a palm, a thumb at the side.
    const double tops[] = {4.6, 2.6, 2, 3.4};
    for (int finger = 0; finger < 4; ++finger)
        painter.drawLine(QPointF(5.4 + finger * 2.7, tops[finger]), QPointF(5.4 + finger * 2.7, 9));
    QPainterPath palm(QPointF(13.5, 9));
    palm.lineTo(QPointF(13.5, 12));
    palm.cubicTo(QPointF(13.5, 15), QPointF(11.4, 16.4), QPointF(9.2, 16.4));
    palm.cubicTo(QPointF(7, 16.4), QPointF(5.6, 15.2), QPointF(4.4, 13.2));
    palm.lineTo(QPointF(2.2, 9.6));
    palm.cubicTo(QPointF(1.6, 8.4), QPointF(3.2, 7.4), QPointF(4, 8.6));
    palm.lineTo(QPointF(5.4, 10.6));
    painter.drawPath(palm);
}

// Two overlapping circles, their overlap filled, and a plus: regions merge.
void shapeBuilder(QPainter &painter)
{
    QPainterPath left, right;
    left.addEllipse(QPointF(6.5, 7), 4.8, 4.8);
    right.addEllipse(QPointF(11.5, 7), 4.8, 4.8);
    QPainterPath plus;
    plus.moveTo(9, 13.2);
    plus.lineTo(9, 17.2);
    plus.moveTo(7, 15.2);
    plus.lineTo(11, 15.2);
    QPainterPathStroker stroker(painter.pen());
    // One fill: overlapping antialiased strokes would round the ink off its colour.
    const QPainterPath ink = stroker.createStroke(left).united(stroker.createStroke(right)).united(stroker.createStroke(plus)).united(left.intersected(right));
    painter.fillPath(ink, painter.pen().color());
}

// Two finger loops and crossed blades.
void scissors(QPainter &painter)
{
    painter.drawEllipse(QPointF(5, 13.4), 2.6, 2.6);
    painter.drawEllipse(QPointF(13, 13.4), 2.6, 2.6);
    painter.drawLine(QPointF(6.6, 11.3), QPointF(13.6, 2));
    painter.drawLine(QPointF(11.4, 11.3), QPointF(4.4, 2));
}

void zoom(QPainter &painter)
{
    painter.drawEllipse(QRectF(2, 2, 10.5, 10.5));
    painter.drawLine(QPointF(11.2, 11.2), QPointF(16, 16));
}

// A stroke that tapers to a point and bulges in the middle, standing for the
// Width tool's variable weight.
void width(QPainter &painter)
{
    QPainterPath shape(QPointF(2, 9));
    shape.lineTo(9, 3);
    shape.lineTo(16, 9);
    shape.lineTo(9, 15);
    shape.closeSubpath();
    // One antialiased pass: filling and stroking the same edge in the same
    // colour can round a boundary pixel a shade off at some angles.
    painter.fillPath(shape, painter.pen().color());
}
}

void ToolIcons::paint(QPainter &painter, Tool tool, QPointF origin, double side, const QColor &colour)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(origin);
    painter.scale(side / points, side / points);
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    // An opaque background would fill dashes.
    painter.setBackgroundMode(Qt::TransparentMode);
    switch (tool) {
    case Tool::select: select(painter); break;
    case Tool::directSelect: directSelect(painter); break;
    case Tool::pen: pen(painter); break;
    case Tool::pencil: pencil(painter); break;
    case Tool::text: type(painter); break;
    case Tool::line: painter.drawLine(QPointF(3, 15), QPointF(15, 3)); break;
    case Tool::rectangle: painter.drawRect(QRectF(2.5, 4.5, 13, 9)); break;
    case Tool::roundedRectangle: painter.drawRoundedRect(QRectF(2.5, 4.5, 13, 9), 3.5, 3.5); break;
    case Tool::ellipse: painter.drawEllipse(QRectF(2, 4, 14, 10)); break;
    case Tool::polygon: painter.drawPolygon(ring(6, 7.2, 0)); break;
    case Tool::star: painter.drawPolygon(ring(5, 7.8, 3.3)); break;
    case Tool::shapeBuilder: shapeBuilder(painter); break;
    case Tool::scissors: scissors(painter); break;
    case Tool::rotate: rotate(painter); break;
    case Tool::scale: scale(painter); break;
    case Tool::gradient: gradient(painter); break;
    case Tool::width: width(painter); break;
    case Tool::eyedropper: eyedropper(painter); break;
    case Tool::hand: hand(painter); break;
    case Tool::zoom: zoom(painter); break;
    }
    painter.restore();
}

QPixmap ToolIcons::pixmap(Tool tool, double side, const QColor &colour, double ratio)
{
    QPixmap result(QSize(int(std::ceil(side * ratio)), int(std::ceil(side * ratio))));
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    paint(painter, tool, QPointF(0, 0), side, colour);
    return result;
}
