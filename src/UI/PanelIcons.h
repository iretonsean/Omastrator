#pragma once
#include <QColor>
#include <QPainter>
#include <QPixmap>

// The panels' icons, drawn as the tool rail's are.
enum class PanelIcon {
    eye, eyeSlash, lock, unlock, chevronRight, chevronDown, layer, group, path, text, image, newLayer, trash, layers,
    alignLeft, alignHorizontalCenter, alignRight, alignTop, alignVerticalCenter, alignBottom,
    distributeHorizontal, distributeVertical, unite, minusFront, intersect, exclude,
};

namespace PanelIcons {
// Paints the icon into a `side` point square, one colour.
void paint(QPainter &painter, PanelIcon icon, QPointF origin, double side, const QColor &colour);
QPixmap pixmap(PanelIcon icon, double side, const QColor &colour, double ratio);
}
