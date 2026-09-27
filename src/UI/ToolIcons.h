#pragma once
#include "Document/EditorSession.h"
#include <QColor>
#include <QPainter>
#include <QPixmap>

// The tool rail's icons, drawn: icon themes differ by distro.
namespace ToolIcons {
// Every icon sits in an 18 point square.
inline constexpr int points = 18;
// Paints the tool's icon into a square, in one colour.
void paint(QPainter &painter, Tool tool, QPointF origin, double side, const QColor &colour);
QPixmap pixmap(Tool tool, double side, const QColor &colour, double ratio);
}
