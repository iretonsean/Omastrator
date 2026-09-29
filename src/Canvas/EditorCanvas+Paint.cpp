#include "Canvas/EditorCanvasState.h"
#include "Rendering/VectorRenderer.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace {
// A soft shadow: fourteen rings stacking to 35% black at the edge.
void drawShadow(QPainter &painter, const QRectF &rect)
{
    constexpr int blur = 14;
    const QRectF shadow = rect.translated(0, 3);
    const double ring = 1 - std::pow(0.65, 1.0 / blur);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromRgbF(0, 0, 0, ring));
    for (int step = blur; step >= 1; --step)
        painter.drawRect(shadow.adjusted(-step, -step, step, step));
    painter.restore();
}

// A square handle centred on `at`, as Illustrator draws anchors and box handles.
void drawSquare(QPainter &painter, QPointF at, double size, const QColor &edge, const QColor &fill)
{
    painter.setPen(QPen(edge, 1));
    painter.setBrush(fill);
    painter.drawRect(QRectF(at.x() - size / 2, at.y() - size / 2, size, size));
}

QPen cosmetic(const QColor &color, double width = 1)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}

const QColor guideColor(255, 0, 170);
}

QColor EditorCanvas::State::accent() const
{
    return canvas.palette().color(QPalette::Highlight);
}

QColor EditorCanvas::State::layerColor(const QUuid &id) const
{
    if (const std::optional<VectorDocument> &document = session.document()) {
        if (const std::optional<QUuid> layer = document->layerOf(id)) {
            const VectorObject *object = document->find(*layer);
            if (object && object->layerColor.isValid())
                return object->layerColor;
        }
    }
    return accent();
}

void EditorCanvas::State::paint(QPainter &painter)
{
    // The pasteboard follows the theme in its deepest tone, behind the panels; the artboard is paper.
    painter.fillRect(canvas.rect(), canvas.palette().color(QPalette::Dark));
    const std::optional<VectorDocument> &document = session.document();
    if (!document)
        return;
    const QTransform toViewT = documentToView();
    const std::vector<Artboard> boards = document->allArtboards();
    for (const Artboard &board : boards)
        drawShadow(painter, toViewT.mapRect(board.rect));
    painter.save();
    painter.setTransform(toViewT);
    VectorRenderer::Options options;
    options.outlineMode = session.showsOutline;
    options.outlineWidth = 1;
    if (browserHost)
        options.livePicture = [host = browserHost](const QUuid &frame) { return host->picture(frame); };
    if (options.outlineMode) {
        for (const Artboard &board : boards)
            painter.fillRect(board.rect, Qt::white);
    }
    std::optional<VectorDocument> shown;
    if (text && text->inDocument && !text->preedit.isEmpty() && document->find(text->object.id)) {
        // The input method's preedit shows in the type itself, pushing the rest along.
        shown = *document;
        shown->find(text->object.id)->text = text->displayObject().text;
    }
    const VectorDocument &drawn = shown ? *shown : *document;
    if (const std::optional<QUuid> group = session.isolatedGroup(); group && drawn.find(*group))
        drawIsolated(painter, drawn, *group);
    else
        VectorRenderer::draw(painter, drawn, options);
    painter.restore();
    for (const Artboard &board : boards) {
        const QRectF artboard = toViewT.mapRect(board.rect);
        drawPixelGrid(painter, artboard);
        if (session.showsGrid)
            drawGrid(painter, artboard);
        // A hairline astride the artboard's edge.
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor::fromRgbF(0, 0, 0, 0.35), 1 / session.viewport.backingScale));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(artboard);
        painter.restore();
    }
    drawBrowserMessages(painter);
    drawArtboardLabels(painter);
    drawFrameLabels(painter);
    drawBrowserBars(painter);
    drawSignInStrip(painter);
    drawGuides(painter);
    drawOverlay(painter);
}

void EditorCanvas::State::drawGrid(QPainter &painter, const QRectF &artboard) const
{
    if (session.gridSpacing <= 0)
        return;
    // Lines at least eight points apart: coarser steps as the view zooms out.
    double spacing = session.gridSpacing;
    while (spacing * scale() < 8)
        spacing *= 2;
    const QRectF visible = artboard.intersected(QRectF(canvas.rect()));
    if (visible.isEmpty())
        return;
    const QPointF first = toDocument(visible.topLeft()), last = toDocument(visible.bottomRight());
    painter.save();
    painter.setClipRect(visible);
    const double hairline = 1 / session.viewport.backingScale;
    for (int major = 0; major < 2; ++major) {
        // Every tenth line is stronger, as Illustrator's gridline every.
        painter.setPen(QPen(major ? QColor(0, 0, 0, 60) : QColor(0, 0, 0, 25), hairline));
        const double step = major ? spacing * 10 : spacing;
        for (double x = std::ceil(first.x() / step) * step; x <= last.x(); x += step) {
            const double at = toView(QPointF(x, 0)).x();
            painter.drawLine(QPointF(at, visible.top()), QPointF(at, visible.bottom()));
        }
        for (double y = std::ceil(first.y() / step) * step; y <= last.y(); y += step) {
            const double at = toView(QPointF(0, y)).y();
            painter.drawLine(QPointF(visible.left(), at), QPointF(visible.right(), at));
        }
    }
    painter.restore();
}

void EditorCanvas::State::drawOverlay(QPainter &painter) const
{
    const VectorDocument &document = *session.document();
    const QTransform toViewTransform = documentToView();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // Hover: the object a click would take, in its layer's colour.
    if (hovered && !drag && document.find(*hovered)) {
        painter.setPen(cosmetic(layerColor(*hovered), 1.5));
        painter.drawPath(toViewTransform.map(document.outline(*hovered)));
    }
    if (session.tool() == Tool::directSelect)
        drawDirectSelection(painter);
    else
        drawSelection(painter);
    drawArtboardTool(painter);
    drawPen(painter);
    drawBuilder(painter);
    drawGradient(painter);
    drawWidth(painter);
    if (drag && drag->kind == DragKind::pencil && drag->points.size() > 1) {
        QPolygonF line;
        for (const QPointF point : drag->points)
            line << toView(point);
        painter.setPen(cosmetic(accent(), 1.5));
        painter.drawPolyline(line);
    }
    if (drag && drag->kind == DragKind::textArea && drag->started) {
        painter.setPen(cosmetic(accent(), 1));
        painter.drawRect(QRectF(toView(drag->pressDocument), toView(drag->grabbed)).normalized());
    }
    // Area type shows its box while edited or selected; a red port marks hidden text.
    for (const QUuid &id : text ? std::vector<QUuid>{text->object.id} : session.selectedTexts()) {
        const VectorObject *object = text && text->object.id == id ? &text->object : document.find(id);
        if (!object || !object->text.area)
            continue;
        const QPolygonF box = (object->transform * toViewTransform).map(QPolygonF(object->text.frame()));
        painter.setPen(cosmetic(layerColor(id), 1));
        painter.drawPolygon(box);
        if (object->text.overflows()) {
            const QPointF port = box.at(2) + QPointF(-8, -8);
            const QRectF square(port - QPointF(5, 5), QSizeF(10, 10));
            painter.setPen(cosmetic(QColor(0xe5, 0x39, 0x35), 1.2));
            painter.setBrush(Qt::white);
            painter.drawRect(square);
            painter.drawLine(square.center() - QPointF(3, 0), square.center() + QPointF(3, 0));
            painter.drawLine(square.center() - QPointF(0, 3), square.center() + QPointF(0, 3));
            painter.setBrush(Qt::NoBrush);
        }
    }
    // Type on a Path: a bracket at the effective start of a selected path text.
    for (const QUuid &id : session.selectedTexts()) {
        const VectorObject *object = text && text->object.id == id ? &text->object : document.find(id);
        if (!object)
            continue;
        if (const auto bracket = pathBracketPoint(*object)) {
            // A tick across the path, at the point text would start from.
            const QPointF normal(-bracket->second.y(), bracket->second.x());
            const QPointF a = object->transform.map(bracket->first - normal * reach(6));
            const QPointF b = object->transform.map(bracket->first + normal * reach(6));
            painter.setPen(cosmetic(accent(), 1.5));
            painter.drawLine(toView(a), toView(b));
        }
    }
    // Threaded text: an in port and an out port on every selected area box, and a
    // line between two that are linked.
    for (const QUuid &id : session.selectedTexts()) {
        const VectorObject *object = document.find(id);
        if (!object || !object->text.area)
            continue;
        painter.setPen(cosmetic(accent(), 1.2));
        painter.setBrush(Qt::white);
        painter.drawRect(QRectF(toView(inPortAt(*object)) - QPointF(3, 3), QSizeF(6, 6)));
        const bool armed = linkArmedFrom == id;
        painter.setBrush(armed ? accent() : Qt::white);
        painter.drawRect(QRectF(toView(outPortAt(*object)) - QPointF(3, 3), QSizeF(6, 6)));
        painter.setBrush(Qt::NoBrush);
        if (!object->text.threadNext.isNull() && session.isSelected(object->text.threadNext)) {
            const VectorObject *next = document.find(object->text.threadNext);
            if (next)
                painter.drawLine(toView(outPortAt(*object)), toView(inPortAt(*next)));
        }
    }
    if (drag && (drag->kind == DragKind::marquee || drag->kind == DragKind::zoomRect) && drag->started) {
        const QRectF area = QRectF(drag->pressView, drag->lastView).normalized();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawRect(area);
        QPen dashed(Qt::black, 1);
        dashed.setDashPattern({3, 3});
        painter.setPen(dashed);
        painter.drawRect(area);
        painter.setRenderHint(QPainter::Antialiasing, true);
    }
    // Smart guides in magenta, as Illustrator draws them.
    painter.setPen(cosmetic(guideColor, 1));
    for (const QLineF &line : guideLines)
        painter.drawLine(toViewTransform.map(line));
    for (const QLineF &gap : guideGaps) {
        const QLineF shown = toViewTransform.map(gap);
        painter.drawLine(shown);
        // Ticks across each end mark an equal gap.
        const QPointF normal = QPointF(-shown.dy(), shown.dx()) / std::max(1e-9, shown.length()) * 4;
        painter.drawLine(shown.p1() - normal, shown.p1() + normal);
        painter.drawLine(shown.p2() - normal, shown.p2() + normal);
    }
    drawMeasurements(painter);
    drawReadout(painter);
    if (text)
        text->draw(painter, toViewTransform, caretShown && canvas.hasFocus(), accent());
    painter.restore();
}

void EditorCanvas::State::drawSelection(QPainter &painter) const
{
    const VectorDocument &document = *session.document();
    const QTransform toViewTransform = documentToView();
    if (text)
        return;
    for (const QUuid &id : session.selection()) {
        if (!document.find(id))
            continue;
        painter.setPen(cosmetic(layerColor(id)));
        painter.drawPath(toViewTransform.map(document.outline(id)));
    }
    if (!session.hasSelection())
        return;
    const QColor edge = layerColor(session.selection().back());
    // The key object: a heavier box, as Illustrator marks it.
    if (const std::optional<QUuid> key = session.keyObject(); key && document.find(*key)) {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(cosmetic(layerColor(*key), 3));
        const QRectF bounds = document.bounds(*key);
        painter.drawRect(QRectF(toView(bounds.topLeft()), toView(bounds.bottomRight())));
        painter.restore();
    }
    if (const std::optional<QRectF> box = selectionBox()) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(cosmetic(edge));
        painter.drawRect(QRectF(toView(box->topLeft()), toView(box->bottomRight())));
        for (int index = 0; index < 8; ++index) {
            if (handleShown(*box, index))
                drawSquare(painter, handlePoint(*box, index), 7, edge, Qt::white);
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
    } else if (session.tool() == Tool::rotate || session.tool() == Tool::scale) {
        // The reference point: the selection's centre.
        const QPointF center = toView(drag && (drag->kind == DragKind::rotate || drag->kind == DragKind::scaleTool)
                                          ? drag->center
                                          : session.selectionBounds().center());
        painter.setPen(cosmetic(edge, 1.5));
        painter.drawEllipse(center, 5, 5);
        painter.drawLine(center - QPointF(8, 0), center + QPointF(8, 0));
        painter.drawLine(center - QPointF(0, 8), center + QPointF(0, 8));
    }
}

void EditorCanvas::State::drawDirectSelection(QPainter &painter) const
{
    const VectorDocument &document = *session.document();
    const QTransform toViewTransform = documentToView();
    for (const QUuid &id : session.selectedLeaves()) {
        const VectorObject *object = document.find(id);
        if (!object)
            continue;
        const QColor edge = layerColor(id);
        painter.setPen(cosmetic(edge));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(toViewTransform.map(object->outline()));
        if (object->kind != ObjectKind::path)
            continue;
        const VectorPath &path = object->path;
        for (int c = 0; c < int(path.contours.size()); ++c) {
            for (int n = 0; n < int(path.contours[size_t(c)].nodes.size()); ++n) {
                const PathNode &node = path.contours[size_t(c)].nodes[size_t(n)];
                if (!isPicked(id, {c, n}))
                    continue;
                // Picked anchors show their handles: a line and a dot.
                for (const QPointF handle : {node.in, node.out}) {
                    if (handle == node.anchor)
                        continue;
                    painter.setPen(cosmetic(edge));
                    painter.drawLine(toView(node.anchor), toView(handle));
                    painter.setBrush(edge);
                    painter.drawEllipse(toView(handle), 3, 3);
                    painter.setBrush(Qt::NoBrush);
                }
            }
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (int c = 0; c < int(path.contours.size()); ++c) {
            for (int n = 0; n < int(path.contours[size_t(c)].nodes.size()); ++n) {
                const bool picked = isPicked(id, {c, n});
                drawSquare(painter, toView(path.contours[size_t(c)].nodes[size_t(n)].anchor), picked ? 6 : 5, edge, picked ? edge : Qt::white);
            }
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
    }
    // Live corners: a small ring per corner, dragged for its radius.
    for (const CornerWidget &widget : cornerWidgets()) {
        painter.setPen(cosmetic(layerColor(widget.object), 1));
        painter.setBrush(Qt::white);
        painter.drawEllipse(widget.view, 3.5, 3.5);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(widget.view, 1.2, 1.2);
    }
}

void EditorCanvas::State::drawPen(QPainter &painter) const
{
    const Contour *contour = penContour();
    if (!contour || contour->nodes.empty())
        return;
    const QColor edge = layerColor(pen->object);
    const PathNode &last = contour->nodes.back();
    // The next segment as it would land, until the pointer presses.
    if (hover && !drag && !contour->closed) {
        QPainterPath band(toView(last.anchor));
        const QPointF to = nearPenStart(*hover) ? toView(contour->nodes.front().anchor) : *hover;
        const QPointF in = nearPenStart(*hover) ? toView(contour->nodes.front().in) : to;
        band.cubicTo(toView(last.out), in, to);
        painter.setPen(cosmetic(edge));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(band);
    }
    const PathNode &shaped = pen->closing ? contour->nodes.front() : last;
    for (const QPointF handle : {shaped.in, shaped.out}) {
        if (handle == shaped.anchor)
            continue;
        painter.setPen(cosmetic(edge));
        painter.drawLine(toView(shaped.anchor), toView(handle));
        painter.setBrush(edge);
        painter.drawEllipse(toView(handle), 3, 3);
        painter.setBrush(Qt::NoBrush);
    }
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (size_t index = 0; index < contour->nodes.size(); ++index) {
        const bool current = index + 1 == contour->nodes.size();
        drawSquare(painter, toView(contour->nodes[index].anchor), 6, edge, current ? edge : Qt::white);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
}
