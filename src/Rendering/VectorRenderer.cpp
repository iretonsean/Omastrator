#include "Rendering/VectorRenderer.h"
#include "Rendering/HslBlend.h"
#include "Document/StrokeGeometry.h"
#include <QPaintDevice>
#include <algorithm>
#include <cmath>

namespace {
QPainter::CompositionMode compositionMode(LayerBlendMode mode)
{
    switch (mode) {
    case LayerBlendMode::multiply:
        return QPainter::CompositionMode_Multiply;
    case LayerBlendMode::screen:
        return QPainter::CompositionMode_Screen;
    case LayerBlendMode::overlay:
        return QPainter::CompositionMode_Overlay;
    case LayerBlendMode::softLight:
        return QPainter::CompositionMode_SoftLight;
    case LayerBlendMode::darken:
        return QPainter::CompositionMode_Darken;
    case LayerBlendMode::lighten:
        return QPainter::CompositionMode_Lighten;
    case LayerBlendMode::difference:
        return QPainter::CompositionMode_Difference;
    case LayerBlendMode::colorDodge:
        return QPainter::CompositionMode_ColorDodge;
    case LayerBlendMode::colorBurn:
        return QPainter::CompositionMode_ColorBurn;
    default:
        return QPainter::CompositionMode_SourceOver;
    }
}

bool skipped(const VectorRenderer::Options &options, const QUuid &id)
{
    return std::find(options.skip.begin(), options.skip.end(), id) != options.skip.end();
}

// One entry of the stack: its opacity, and its blend where the device can.
void composite(QPainter &painter, const Paint &paint)
{
    painter.setOpacity(painter.opacity() * paint.opacity);
    if (paint.blendMode != LayerBlendMode::normal && painter.device() && painter.device()->devType() == QInternal::Image)
        painter.setCompositionMode(compositionMode(paint.blendMode));
}

void drawPaints(QPainter &painter, const VectorObject &object, const QPainterPath &path, const QRectF &bounds, bool fillable)
{
    if (object.hasSimpleAppearance()) {
        if (object.fill.isVisible() && fillable)
            painter.fillPath(path, object.fill.brush(bounds));
        if (object.stroke.isVisible())
            painter.strokePath(path, object.stroke.pen(bounds));
        return;
    }
    if (fillable) {
        for (const Paint &fill : object.fills()) {
            if (!fill.isVisible())
                continue;
            painter.save();
            composite(painter, fill);
            painter.fillPath(path, fill.brush(bounds));
            painter.restore();
        }
    }
    for (const StrokeStyle &stroke : object.strokes()) {
        if (!stroke.isVisible())
            continue;
        painter.save();
        composite(painter, stroke.paint);
        VectorRenderer::drawStroke(painter, path, stroke, bounds);
        painter.restore();
    }
}

void drawLeaf(QPainter &painter, const VectorObject &object, const VectorRenderer::Options &options)
{
    if (options.outlineMode) {
        QPen pen(Qt::black, options.outlineWidth);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        if (object.kind == ObjectKind::image) {
            const QPainterPath box = object.outline();
            painter.drawPath(box);
            painter.drawLine(box.boundingRect().topLeft(), box.boundingRect().bottomRight());
            painter.drawLine(box.boundingRect().topRight(), box.boundingRect().bottomLeft());
        } else {
            painter.drawPath(object.outline());
        }
        return;
    }
    switch (object.kind) {
    case ObjectKind::image: {
        painter.save();
        painter.setTransform(object.transform, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(QPointF(0, 0), QSizeF(object.image.size())), object.image);
        painter.restore();
        return;
    }
    case ObjectKind::text: {
        // Type is filled in its own coordinates so gradients follow the glyphs.
        painter.save();
        painter.setTransform(object.transform, true);
        const QPainterPath glyphs = object.text.outline();
        drawPaints(painter, object, glyphs, glyphs.boundingRect(), true);
        painter.restore();
        return;
    }
    case ObjectKind::path: {
        const QPainterPath path = object.path.painterPath();
        const QRectF bounds = path.boundingRect();
        const bool closed = std::any_of(object.path.contours.begin(), object.path.contours.end(),
                                        [](const Contour &c) { return c.closed || c.nodes.size() > 2; });
        drawPaints(painter, object, path, bounds, closed);
        return;
    }
    default:
        return;
    }
}

void drawChildren(QPainter &painter, const VectorDocument &document, const VectorObject &container,
                  const VectorRenderer::Options &options)
{
    const auto children = document.children(container.id);
    size_t first = 0;
    painter.save();
    if (container.isClipGroup && !children.empty()) {
        // The bottom child is the clip; it draws nothing itself.
        const VectorObject *clip = document.find(children.front());
        if (clip) {
            // A group clips with every outline in it.
            painter.setClipPath(document.outline(clip->id), Qt::IntersectClip);
            if (options.outlineMode)
                drawLeaf(painter, *clip, options);
        }
        first = 1;
    }
    for (size_t index = first; index < children.size(); ++index)
        VectorRenderer::drawObject(painter, document, children[index], options);
    painter.restore();
}
}

namespace VectorRenderer {
void drawStroke(QPainter &painter, const QPainterPath &path, const StrokeStyle &stroke, const QRectF &bounds)
{
    if (stroke.isPlain()) {
        painter.strokePath(path, stroke.pen(bounds));
        return;
    }
    // Inside and outside: a double-width stroke, clipped to one side of the path.
    const bool aligned = stroke.alignment != StrokeAlignment::center && StrokeGeometry::isClosed(path);
    StrokeStyle line = stroke;
    line.width = aligned ? stroke.width * 2 : stroke.width;
    QPainterPath along = StrokeGeometry::body(path, stroke);
    if (stroke.alignDashes && !stroke.dashes.empty()) {
        along = StrokeGeometry::alignedDashes(along, stroke.dashes);
        line.dashes.clear();
    }
    painter.save();
    if (aligned && stroke.alignment == StrokeAlignment::inside) {
        painter.setClipPath(path, Qt::IntersectClip);
    } else if (aligned) {
        const double margin = line.width * std::max(1.0, stroke.miterLimit) + 1;
        QPainterPath outside;
        outside.addRect(path.boundingRect().adjusted(-margin, -margin, margin, margin));
        outside.addPath(path);
        outside.setFillRule(Qt::OddEvenFill);
        painter.setClipPath(outside, Qt::IntersectClip);
    }
    painter.strokePath(along, line.pen(bounds));
    painter.restore();
    const QPainterPath heads = StrokeGeometry::heads(path, stroke);
    if (!heads.isEmpty())
        painter.fillPath(heads, stroke.paint.brush(bounds));
}

void drawObject(QPainter &painter, const VectorDocument &document, const QUuid &id, const Options &options)
{
    const VectorObject *object = document.find(id);
    if (!object || !object->isVisible || skipped(options, id))
        return;
    const bool raster = painter.device() && painter.device()->devType() == QInternal::Image;
    const bool isolated = !options.outlineMode && (object->opacity < 1 || object->blendMode != LayerBlendMode::normal);
    if (!isolated) {
        if (object->isContainer())
            drawChildren(painter, document, *object, options);
        else
            drawLeaf(painter, *object, options);
        return;
    }
    if (raster && HslBlend::handles(object->blendMode)) {
        // Hue, Saturation, Color and Luminosity: drawn aside, then blended by hand.
        QImage *target = static_cast<QImage *>(painter.device());
        QImage layer(target->size(), QImage::Format_RGBA8888_Premultiplied);
        layer.setDevicePixelRatio(target->devicePixelRatio());
        layer.fill(Qt::transparent);
        {
            QPainter aside(&layer);
            aside.setRenderHints(painter.renderHints());
            aside.setTransform(painter.transform());
            if (painter.hasClipping())
                aside.setClipPath(painter.clipPath());
            Options plain = options;
            VectorObject copy = *object;
            copy.blendMode = LayerBlendMode::normal;
            copy.opacity = 1;
            VectorDocument single = document;
            *single.find(id) = copy;
            drawObject(aside, single, id, plain);
        }
        // The raster engine paints straight into the image, so it reads current.
        const QImage backdrop = target->convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        QImage blended = HslBlend::blend(backdrop, layer, object->blendMode, object->opacity);
        const double ratio = target->devicePixelRatio();
        blended.setDevicePixelRatio(1);
        painter.save();
        painter.resetTransform();
        painter.setOpacity(1);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(QRectF(QPointF(0, 0), QSizeF(target->size()) / ratio), blended);
        painter.restore();
        return;
    }
    painter.save();
    painter.setOpacity(painter.opacity() * object->opacity);
    if (raster)
        painter.setCompositionMode(compositionMode(object->blendMode));
    if (object->isContainer())
        drawChildren(painter, document, *object, options);
    else
        drawLeaf(painter, *object, options);
    painter.restore();
}

void draw(QPainter &painter, const VectorDocument &document, const Options &options)
{
    painter.setRenderHint(QPainter::Antialiasing);
    if (options.drawBackground && !options.outlineMode)
        painter.fillRect(QRectF(QPointF(0, 0), document.size), document.background);
    for (const QUuid &layer : document.layers())
        drawObject(painter, document, layer, options);
}

QImage render(const VectorDocument &document, double scale, bool transparent)
{
    const QSize size(std::max(1, int(std::ceil(document.size.width() * scale))),
                     std::max(1, int(std::ceil(document.size.height() * scale))));
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(scale, scale);
    Options options;
    options.drawBackground = !transparent;
    draw(painter, document, options);
    painter.end();
    return image;
}
}
