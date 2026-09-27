#include "UI/PanelIcons.h"
#include "UI/ToolIcons.h"
#include <QPainterPath>
#include <cmath>

namespace {
void eye(QPainter &painter, bool slashed)
{
    QPainterPath lid;
    lid.moveTo(1.5, 9);
    lid.cubicTo(5, 3, 13, 3, 16.5, 9);
    lid.cubicTo(13, 15, 5, 15, 1.5, 9);
    painter.drawPath(lid);
    painter.drawEllipse(QPointF(9, 9), 2.6, 2.6);
    if (slashed)
        painter.drawLine(QPointF(3, 15), QPointF(15, 3));
}

// A padlock; open, its shackle swings up and aside.
void padlock(QPainter &painter, bool open)
{
    painter.drawRoundedRect(QRectF(3.5, 8, 11, 8), 1.5, 1.5);
    QPainterPath shackle;
    const double lift = open ? 2 : 0;
    shackle.moveTo(6, 8 - lift);
    shackle.lineTo(6, 5.5 - lift);
    shackle.arcTo(QRectF(6, 2 - lift, 6, 7), 180, -180);
    if (!open)
        shackle.lineTo(12, 8);
    painter.drawPath(shackle);
}

void chevron(QPainter &painter, bool down)
{
    if (down)
        painter.drawPolyline(QPolygonF{QPointF(4.5, 6.5), QPointF(9, 11), QPointF(13.5, 6.5)});
    else
        painter.drawPolyline(QPolygonF{QPointF(6.5, 4.5), QPointF(11, 9), QPointF(6.5, 13.5)});
}

void folder(QPainter &painter)
{
    QPainterPath shape;
    shape.moveTo(2, 4.5);
    shape.lineTo(7, 4.5);
    shape.lineTo(8.5, 6.5);
    shape.lineTo(16, 6.5);
    shape.lineTo(16, 14.5);
    shape.lineTo(2, 14.5);
    shape.closeSubpath();
    painter.drawPath(shape);
}

// A curve between two anchors, the pen's mark.
void path(QPainter &painter)
{
    QPainterPath curve;
    curve.moveTo(3, 14);
    curve.cubicTo(3, 4, 15, 14, 15, 4);
    painter.drawPath(curve);
    painter.fillRect(QRectF(1.5, 12.5, 3, 3), painter.pen().color());
    painter.fillRect(QRectF(13.5, 2.5, 3, 3), painter.pen().color());
}

// A framed picture: a sun over a hill.
void image(QPainter &painter)
{
    painter.drawRoundedRect(QRectF(2, 3.5, 14, 11), 1.5, 1.5);
    painter.drawEllipse(QPointF(6.5, 7.3), 1.4, 1.4);
    painter.drawPolyline(QPolygonF{QPointF(2.5, 13.5), QPointF(8, 9), QPointF(11, 11.5), QPointF(13, 10), QPointF(15.5, 12.5)});
}

void newLayer(QPainter &painter)
{
    painter.drawRoundedRect(QRectF(2.5, 2.5, 13, 13), 2, 2);
    painter.drawLine(QPointF(9, 6), QPointF(9, 12));
    painter.drawLine(QPointF(6, 9), QPointF(12, 9));
}

void trash(QPainter &painter)
{
    painter.drawLine(QPointF(3, 5), QPointF(15, 5));
    painter.drawLine(QPointF(7, 3), QPointF(11, 3));
    painter.drawPolyline(QPolygonF{QPointF(4.5, 5), QPointF(5.5, 15.5), QPointF(12.5, 15.5), QPointF(13.5, 5)});
    painter.drawLine(QPointF(7.5, 8), QPointF(7.8, 13));
    painter.drawLine(QPointF(10.5, 8), QPointF(10.2, 13));
}

void layers(QPainter &painter)
{
    for (int step = 0; step < 3; ++step) {
        const double y = 4 + step * 4;
        painter.drawPolygon(QPolygonF{QPointF(9, y - 3), QPointF(16, y), QPointF(9, y + 3), QPointF(2, y)});
    }
}

// Two bars against a line: an edge or a centre.
void align(QPainter &painter, bool vertical, double where)
{
    const QColor ink = painter.pen().color();
    painter.save();
    if (vertical) {
        painter.translate(9, 9);
        painter.rotate(90);
        painter.translate(-9, -9);
    }
    const double line = 2 + where * 14;
    painter.drawLine(QPointF(line, 1.5), QPointF(line, 16.5));
    const double longBar = 12, shortBar = 7;
    const double longLeft = where == 0 ? line + 1 : where == 1 ? line - 1 - longBar : line - longBar / 2;
    const double shortLeft = where == 0 ? line + 1 : where == 1 ? line - 1 - shortBar : line - shortBar / 2;
    painter.fillRect(QRectF(longLeft, 4, longBar, 3.5), ink);
    painter.fillRect(QRectF(shortLeft, 10.5, shortBar, 3.5), ink);
    painter.restore();
}

void distribute(QPainter &painter, bool vertical)
{
    const QColor ink = painter.pen().color();
    painter.save();
    if (vertical) {
        painter.translate(9, 9);
        painter.rotate(90);
        painter.translate(-9, -9);
    }
    for (const double x : {2.0, 7.5, 13.0})
        painter.fillRect(QRectF(x, 5, 3, 8), ink);
    painter.restore();
}

// Three bars and a line through the edge each is spaced by.
void distributeEdge(QPainter &painter, bool vertical, bool far)
{
    const QColor ink = painter.pen().color();
    painter.save();
    if (vertical) {
        painter.translate(9, 9);
        painter.rotate(90);
        painter.translate(-9, -9);
    }
    for (const double x : {2.0, 7.5, 13.0}) {
        painter.fillRect(QRectF(x, 6, 3, 9), ink);
        const double edge = far ? x + 3 : x;
        painter.drawLine(QPointF(edge, 2), QPointF(edge, 16));
    }
    painter.restore();
}

// Two bars with the gap between them marked.
void spacing(QPainter &painter, bool vertical)
{
    const QColor ink = painter.pen().color();
    painter.save();
    if (vertical) {
        painter.translate(9, 9);
        painter.rotate(90);
        painter.translate(-9, -9);
    }
    painter.fillRect(QRectF(2, 3, 3.5, 12), ink);
    painter.fillRect(QRectF(12.5, 3, 3.5, 12), ink);
    painter.drawLine(QPointF(6.5, 9), QPointF(11.5, 9));
    painter.drawLine(QPointF(6.5, 7), QPointF(6.5, 11));
    painter.drawLine(QPointF(11.5, 7), QPointF(11.5, 11));
    painter.restore();
}

// A square corner rounded off.
void corner(QPainter &painter)
{
    QPainterPath path(QPointF(3, 15));
    path.lineTo(QPointF(3, 9));
    path.cubicTo(QPointF(3, 5.7), QPointF(5.7, 3), QPointF(9, 3));
    path.lineTo(QPointF(15, 3));
    painter.drawPath(path);
}

// Two overlapping squares; the part the operation keeps is filled.
void pathfinder(QPainter &painter, int operation)
{
    QPainterPath back, front;
    back.addRect(QRectF(2, 2, 9.5, 9.5));
    front.addRect(QRectF(6.5, 6.5, 9.5, 9.5));
    const QPainterPath kept = operation == 0 ? back.united(front)
        : operation == 1                     ? back.subtracted(front)
        : operation == 2                     ? back.intersected(front)
                                             : back.united(front).subtracted(back.intersected(front));
    painter.fillPath(kept, painter.pen().color());
    QPen thin = painter.pen();
    thin.setWidthF(1);
    painter.setPen(thin);
    painter.drawPath(back);
    painter.drawPath(front);
}
}

namespace {
// An open circle with an arrowhead: rotation.
void rotate(QPainter &painter)
{
    QPainterPath arc;
    arc.arcMoveTo(QRectF(3, 3, 12, 12), 60);
    arc.arcTo(QRectF(3, 3, 12, 12), 60, 280);
    painter.drawPath(arc);
    const QPointF tip = arc.currentPosition();
    painter.drawPolyline(QPolygonF{tip + QPointF(-3.5, -1.5), tip, tip + QPointF(-0.5, -4)});
}

// Two chain links; apart, they're unlinked.
void chain(QPainter &painter, bool linked)
{
    const double gap = linked ? 0 : 2;
    painter.drawRoundedRect(QRectF(6, 1.5 - gap, 6, 8), 3, 3);
    painter.drawRoundedRect(QRectF(6, 8.5 + gap, 6, 8), 3, 3);
    if (linked)
        painter.drawLine(QPointF(9, 6.5), QPointF(9, 11.5));
}

// Two lines of text with a double arrow between them: leading.
void leadingGlyph(QPainter &painter)
{
    painter.drawLine(QPointF(7, 3), QPointF(16, 3));
    painter.drawLine(QPointF(7, 15), QPointF(16, 15));
    painter.drawLine(QPointF(3, 3.5), QPointF(3, 14.5));
    painter.drawPolyline(QPolygonF{QPointF(1.2, 5.5), QPointF(3, 3.5), QPointF(4.8, 5.5)});
    painter.drawPolyline(QPolygonF{QPointF(1.2, 12.5), QPointF(3, 14.5), QPointF(4.8, 12.5)});
}

// A letter between two bars with a double arrow under it: tracking.
void trackingGlyph(QPainter &painter)
{
    painter.drawLine(QPointF(2, 2), QPointF(2, 11));
    painter.drawLine(QPointF(16, 2), QPointF(16, 11));
    painter.drawPolyline(QPolygonF{QPointF(5.5, 11), QPointF(9, 2.5), QPointF(12.5, 11)});
    painter.drawLine(QPointF(6.8, 8), QPointF(11.2, 8));
    painter.drawLine(QPointF(3.5, 15), QPointF(14.5, 15));
    painter.drawPolyline(QPolygonF{QPointF(5.5, 13.2), QPointF(3.5, 15), QPointF(5.5, 16.8)});
    painter.drawPolyline(QPolygonF{QPointF(12.5, 13.2), QPointF(14.5, 15), QPointF(12.5, 16.8)});
}

void dots(QPainter &painter)
{
    painter.setBrush(painter.pen().color());
    for (const double x : {4.0, 9.0, 14.0})
        painter.drawEllipse(QPointF(x, 9), 1.3, 1.3);
}
}

void PanelIcons::paint(QPainter &painter, PanelIcon icon, QPointF origin, double side, const QColor &colour)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(origin);
    painter.scale(side / 18, side / 18);
    painter.setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.setBackgroundMode(Qt::TransparentMode);
    switch (icon) {
    case PanelIcon::eye: eye(painter, false); break;
    case PanelIcon::eyeSlash: eye(painter, true); break;
    case PanelIcon::lock: padlock(painter, false); break;
    case PanelIcon::unlock: padlock(painter, true); break;
    case PanelIcon::chevronRight: chevron(painter, false); break;
    case PanelIcon::chevronDown: chevron(painter, true); break;
    case PanelIcon::layer: layers(painter); break;
    case PanelIcon::group: folder(painter); break;
    case PanelIcon::path: path(painter); break;
    // The rail's T, a little smaller.
    case PanelIcon::text: ToolIcons::paint(painter, Tool::text, QPointF(1, 1), 16, colour); break;
    case PanelIcon::image: image(painter); break;
    case PanelIcon::newLayer: newLayer(painter); break;
    case PanelIcon::trash: trash(painter); break;
    case PanelIcon::layers: layers(painter); break;
    case PanelIcon::alignLeft: align(painter, false, 0); break;
    case PanelIcon::alignHorizontalCenter: align(painter, false, 0.5); break;
    case PanelIcon::alignRight: align(painter, false, 1); break;
    case PanelIcon::alignTop: align(painter, true, 0); break;
    case PanelIcon::alignVerticalCenter: align(painter, true, 0.5); break;
    case PanelIcon::alignBottom: align(painter, true, 1); break;
    case PanelIcon::distributeHorizontal: distribute(painter, false); break;
    case PanelIcon::distributeVertical: distribute(painter, true); break;
    case PanelIcon::distributeLeft: distributeEdge(painter, false, false); break;
    case PanelIcon::distributeRight: distributeEdge(painter, false, true); break;
    case PanelIcon::distributeTop: distributeEdge(painter, true, false); break;
    case PanelIcon::distributeBottom: distributeEdge(painter, true, true); break;
    case PanelIcon::spaceHorizontal: spacing(painter, false); break;
    case PanelIcon::spaceVertical: spacing(painter, true); break;
    case PanelIcon::cornerRadius: corner(painter); break;
    case PanelIcon::unite: pathfinder(painter, 0); break;
    case PanelIcon::minusFront: pathfinder(painter, 1); break;
    case PanelIcon::intersect: pathfinder(painter, 2); break;
    case PanelIcon::exclude: pathfinder(painter, 3); break;
    case PanelIcon::rotate: rotate(painter); break;
    case PanelIcon::link: chain(painter, true); break;
    case PanelIcon::unlink: chain(painter, false); break;
    case PanelIcon::more: dots(painter); break;
    case PanelIcon::leading: leadingGlyph(painter); break;
    case PanelIcon::tracking: trackingGlyph(painter); break;
    }
    painter.restore();
}

QPixmap PanelIcons::pixmap(PanelIcon icon, double side, const QColor &colour, double ratio)
{
    QPixmap result(QSize(int(std::ceil(side * ratio)), int(std::ceil(side * ratio))));
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    paint(painter, icon, QPointF(0, 0), side, colour);
    return result;
}
