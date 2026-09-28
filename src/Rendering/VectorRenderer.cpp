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

void drawPaints(QPainter &painter, const VectorObject &object, const QPainterPath &path, const QRectF &bounds, bool fillable,
                bool strokable = true)
{
    if (object.hasSimpleAppearance()) {
        if (object.fill.isVisible() && fillable)
            painter.fillPath(path, object.fill.brush(bounds));
        if (object.stroke.isVisible() && strokable)
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
    for (const StrokeStyle &stroke : strokable ? object.strokes() : std::vector<StrokeStyle>{}) {
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
        const QRectF bounds = glyphs.boundingRect();
        const auto pieces = object.text.runs.empty() ? decltype(object.text.fills()){} : object.text.fills();
        if (std::none_of(pieces.begin(), pieces.end(), [](const auto &piece) { return piece.first.has_value(); })) {
            drawPaints(painter, object, glyphs, bounds, true);
        } else {
            // Runs with a colour of their own; the rest take the object's fills, and the strokes cover every glyph.
            for (const auto &[color, piece] : pieces) {
                if (color)
                    painter.fillPath(piece, QBrush(*color));
                else
                    drawPaints(painter, object, piece, bounds, true, false);
            }
            drawPaints(painter, object, glyphs, bounds, false);
        }
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

// How many device pixels a mask group rasterizes per document unit: sharp on the
// canvas and PNG, and never so coarse that a print PDF's embedded patch looks soft.
double maskRasterScale(const QPainter &painter)
{
    double scale = std::sqrt(std::abs(painter.transform().determinant()));
    if (painter.device() && painter.device()->devType() == QInternal::Image)
        scale *= painter.device()->devicePixelRatioF();
    else
        scale = std::max(scale, 3.0);
    return std::clamp(scale, 0.25, 8.0);
}

// `ids` drawn alone, in `bounds` (document coordinates), at `scale` pixels per unit.
QImage renderIsolated(const VectorDocument &document, const std::vector<QUuid> &ids, const QRectF &bounds, double scale,
                      const QPainter &like, const VectorRenderer::Options &options)
{
    const QSize size(std::max(1, int(std::ceil(bounds.width() * scale))), std::max(1, int(std::ceil(bounds.height() * scale))));
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter local(&image);
    local.setRenderHints(like.renderHints());
    local.setRenderHint(QPainter::Antialiasing);
    local.scale(scale, scale);
    local.translate(-bounds.left(), -bounds.top());
    for (const QUuid &id : ids)
        VectorRenderer::drawObject(local, document, id, options);
    local.end();
    return image;
}

// `content`'s alpha, multiplied by the mask's luminance (P2-9); `clip` off treats
// what the mask never covers as fully visible instead of fully hidden.
void applyLuminanceMask(QImage &content, const QImage &maskImage, const OpacityMask &mask)
{
    const QImage source = maskImage.convertToFormat(QImage::Format_RGBA8888);
    QImage target = content.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < target.height(); ++y) {
        auto *row = target.scanLine(y);
        const auto *maskRow = source.constScanLine(y);
        for (int x = 0; x < target.width(); ++x) {
            uchar *pixel = row + x * 4;
            const uchar *maskPixel = maskRow + x * 4;
            const double coverage = maskPixel[3] / 255.0;
            const double luminance = (0.2126 * maskPixel[0] + 0.7152 * maskPixel[1] + 0.0722 * maskPixel[2]) / 255.0;
            double effective = mask.clip ? luminance * coverage : (coverage > 0 ? luminance : 1.0);
            if (mask.inverted)
                effective = 1.0 - effective;
            pixel[3] = uchar(std::clamp(pixel[3] * effective, 0.0, 255.0));
        }
    }
    content = target.convertToFormat(QImage::Format_ARGB32);
}

void drawChildren(QPainter &painter, const VectorDocument &document, const VectorObject &container,
                  const VectorRenderer::Options &options)
{
    const auto children = document.children(container.id);
    size_t first = 0;
    painter.save();
    if (container.kind == ObjectKind::frame) {
        // A frame: its fills under its children, clipped to its box unless clipping is off, its strokes over them.
        const QPainterPath box = container.path.painterPath();
        if (options.outlineMode)
            drawLeaf(painter, container, options);
        else
            drawPaints(painter, container, box, box.boundingRect(), true, false);
        painter.save();
        if (container.clipsContent)
            painter.setClipPath(box, Qt::IntersectClip);
        for (const QUuid &child : children)
            VectorRenderer::drawObject(painter, document, child, options);
        painter.restore();
        if (!options.outlineMode)
            drawPaints(painter, container, box, box.boundingRect(), false, true);
        painter.restore();
        return;
    }
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
    } else if (container.mask && !options.outlineMode && children.size() >= 2) {
        // The top child is the mask; everything under it is what it masks.
        const QRectF bounds = document.bounds(container.id, true);
        if (bounds.width() > 0 && bounds.height() > 0) {
            const double scale = maskRasterScale(painter);
            const std::vector<QUuid> content(children.begin(), children.end() - 1);
            QImage rendered = renderIsolated(document, content, bounds, scale, painter, options);
            const QImage maskImage = renderIsolated(document, {children.back()}, bounds, scale, painter, options);
            applyLuminanceMask(rendered, maskImage, *container.mask);
            painter.drawImage(bounds, rendered);
            painter.restore();
            return;
        }
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
    if (!stroke.widthPoints.empty()) {
        // Width tool (P2-7): no single pen width draws this, so it fills the outline instead.
        painter.fillPath(StrokeGeometry::area(path, stroke), stroke.paint.brush(bounds));
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
    if (options.drawBackground && !options.outlineMode) {
        for (const Artboard &board : document.allArtboards())
            painter.fillRect(board.rect, board.background);
    }
    for (const QUuid &layer : document.layers())
        drawObject(painter, document, layer, options);
}

QImage render(const VectorDocument &document, double scale, bool transparent)
{
    // Several artboards: render the first one alone, moved to the origin.
    const VectorDocument page = document.artboards.empty() ? document : document.artboardDocument(0);
    const QSize size(std::max(1, int(std::ceil(page.size.width() * scale))),
                     std::max(1, int(std::ceil(page.size.height() * scale))));
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(scale, scale);
    Options options;
    options.drawBackground = !transparent;
    draw(painter, page, options);
    painter.end();
    return image;
}
}
