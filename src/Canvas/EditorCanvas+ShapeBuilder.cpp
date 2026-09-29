#include "Canvas/EditorCanvasState.h"
#include <QPainter>

namespace {
QPen hairline(const QColor &color, double width)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}

// Illustrator's highlight: a grey dot screen over the region.
QBrush mesh()
{
    return QBrush(QColor(80, 80, 80, 190), Qt::Dense5Pattern);
}
const QColor eraseInk(214, 48, 49);
}

const ShapeBuilder::Arrangement &EditorCanvas::State::arrangement() const
{
    static const ShapeBuilder::Arrangement none;
    if (!session.document())
        return none;
    std::vector<QUuid> leaves = session.selectedLeaves();
    if (!built || built->leaves != leaves || built->options != session.shapeBuilder)
        built = Built{leaves, session.shapeBuilder, ShapeBuilder::arrange(*session.document(), leaves, session.shapeBuilder)};
    return built->arrangement;
}

void EditorCanvas::State::builderPress(QPointF view, Qt::KeyboardModifiers modifiers)
{
    const QPointF point = toDocument(view);
    building = {};
    // Shift-drag draws a marquee; otherwise the press is the first thing touched.
    const bool marquee = modifiers.testFlag(Qt::ShiftModifier);
    if (!marquee)
        arrangement().touchAlong(point, point, reach(4), building.regions, building.edges);
    beginDrag(DragKind::shapeBuilder, view);
    drag->additive = marquee;
    drag->points = {point};
    session.beginInteraction(QStringLiteral("Shape Builder"));
    drag->interacting = true;
}

void EditorCanvas::State::dragBuilder(QPointF view, Qt::KeyboardModifiers)
{
    if (drag->additive)
        return;
    const QPointF point = toDocument(view);
    if (session.shapeBuilder.freeformSelection) {
        arrangement().touchAlong(drag->points.back(), point, reach(4), building.regions, building.edges);
        drag->points.push_back(point);
    } else {
        // A straight line from the press: what it crosses now, not what it crossed before.
        building = {};
        arrangement().touchAlong(drag->pressDocument, point, reach(4), building.regions, building.edges);
        drag->points = {drag->pressDocument, point};
    }
}

ShapeBuilder::Gesture EditorCanvas::State::builderTouched() const
{
    if (drag && drag->kind == DragKind::shapeBuilder && drag->additive && drag->started) {
        ShapeBuilder::Gesture marquee;
        arrangement().touchIn(QRectF(drag->pressDocument, toDocument(drag->lastView)), marquee.regions, marquee.edges);
        return marquee;
    }
    return building;
}

void EditorCanvas::State::finishBuilder(Qt::KeyboardModifiers modifiers)
{
    ShapeBuilder::Gesture gesture = builderTouched();
    gesture.erase = modifiers.testFlag(Qt::AltModifier);
    gesture.click = !drag->started;
    if (drag->additive && !drag->started) {
        gesture.regions.clear();
        gesture.edges.clear();
        arrangement().touchAlong(drag->pressDocument, drag->pressDocument, reach(4), gesture.regions, gesture.edges);
    }
    // A click on an edge acts on the edge alone, when edges take clicks at all.
    if (gesture.click && !gesture.edges.empty() && (gesture.erase || session.shapeBuilder.clickingStrokeSplits)) {
        gesture.edges.erase(gesture.edges.begin() + 1, gesture.edges.end());
        gesture.regions.clear();
    }
    building = {};
    if (!session.isInteracting())
        return;
    // Building replaces the document, which drops the cached arrangement: hand over a copy.
    const ShapeBuilder::Arrangement current = arrangement();
    session.previewShapeBuild(current, gesture);
    session.commitInteraction();
}

void EditorCanvas::State::updateBuilderHover(std::optional<QPointF> view)
{
    std::optional<int> region, edge;
    if (view && !drag && !spaceHeld && !text && session.tool() == Tool::shapeBuilder && session.hasDocument()) {
        const QPointF point = toDocument(*view);
        edge = arrangement().edgeAt(point, reach(4));
        if (!edge)
            region = arrangement().regionAt(point);
    }
    if (region != builderRegion || edge != builderEdge) {
        builderRegion = region;
        builderEdge = edge;
        canvas.update();
    }
}

void EditorCanvas::State::drawBuilder(QPainter &painter) const
{
    if (session.tool() != Tool::shapeBuilder || text || !session.document())
        return;
    const ShapeBuilder::Arrangement &shown = arrangement();
    const QTransform toViewTransform = documentToView();
    const ShapeBuilderOptions &options = session.shapeBuilder;
    const bool dragging = drag && drag->kind == DragKind::shapeBuilder;
    std::vector<int> regions, edges;
    if (dragging) {
        const ShapeBuilder::Gesture touched = builderTouched();
        regions = touched.regions;
        edges = touched.edges;
    } else if (!drag) {
        if (builderRegion)
            regions.push_back(*builderRegion);
        if (builderEdge)
            edges.push_back(*builderEdge);
    }
    const QColor ink = modifiers.testFlag(Qt::AltModifier) ? eraseInk : accent();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const int index : regions) {
        if (index < 0 || size_t(index) >= shown.regions.size())
            continue;
        const QPainterPath area = toViewTransform.map(shown.regions[size_t(index)].area);
        if (options.highlightFill)
            painter.fillPath(area, mesh());
        if (options.highlightStroke) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(hairline(ink, 1.5));
            painter.drawPath(area);
        }
    }
    painter.setBrush(Qt::NoBrush);
    for (const int index : edges) {
        if (index < 0 || size_t(index) >= shown.edges.size())
            continue;
        painter.setPen(hairline(ink, options.highlightStroke ? 3 : 1.5));
        painter.drawPath(toViewTransform.map(VectorPath{{shown.edges[size_t(index)].path}, Qt::WindingFill}.painterPath()));
    }
    // The drag's own trail: the path the pointer took, the straight line, or the marquee.
    if (dragging && drag->started) {
        if (drag->additive) {
            QPen dashed(Qt::black, 1);
            dashed.setDashPattern({3, 3});
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(dashed);
            painter.drawRect(QRectF(drag->pressView, drag->lastView).normalized());
        } else if (drag->points.size() > 1) {
            QPolygonF trail;
            for (const QPointF point : drag->points)
                trail << toView(point);
            painter.setPen(hairline(ink, 1));
            painter.drawPolyline(trail);
        }
    }
    painter.restore();
}
