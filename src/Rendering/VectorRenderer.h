#pragma once
#include "Document/VectorDocument.h"
#include <QPainter>
#include <QSize>
#include <functional>

// Draws a document with QPainter in document coordinates: the canvas, PNG and
// JPEG exports and PDF share it. The painter's transform places the artboard.
namespace VectorRenderer {
struct Options {
    // The artboard's paper colour behind the objects.
    bool drawBackground = true;
    // Outline view: every path as a thin black line, no fills.
    bool outlineMode = false;
    // Outline width in device pixels for outline view.
    double outlineWidth = 1;
    // Objects to leave out, such as text being edited in place.
    std::vector<QUuid> skip;
    // A Browser View's live picture, when the canvas has one; without it (or when it returns a null image)
    // a frame draws its stored last picture, as every export does.
    std::function<QImage(const QUuid &)> livePicture;
};

void draw(QPainter &painter, const VectorDocument &document, const Options &options);
void drawObject(QPainter &painter, const VectorDocument &document, const QUuid &id, const Options &options);
// One stroke: plain through QPen, else with its alignment, aligned dashes and heads.
void drawStroke(QPainter &painter, const QPainterPath &path, const StrokeStyle &stroke, const QRectF &bounds);
// The artboard as an image `scale` device pixels per point.
QImage render(const VectorDocument &document, double scale, bool transparent);
}
