#include "IO/FigmaMapperInternal.h"
#include <QImage>
#include <QtMath>

namespace FigmaMap {
namespace {
StrokeAlignment mapStrokeAlign(const QString &value)
{
    if (value == QLatin1String("INSIDE"))
        return StrokeAlignment::inside;
    if (value == QLatin1String("OUTSIDE"))
        return StrokeAlignment::outside;
    return StrokeAlignment::center;
}

Qt::PenCapStyle strokeCap(const QString &value)
{
    if (value == QLatin1String("ROUND"))
        return Qt::RoundCap;
    if (value == QLatin1String("SQUARE") || value.contains(QLatin1String("ARROW")) || value.contains(QLatin1String("FILLED")))
        return Qt::SquareCap;
    return Qt::FlatCap;
}

Qt::PenJoinStyle strokeJoin(const QString &value)
{
    if (value == QLatin1String("ROUND"))
        return Qt::RoundJoin;
    if (value == QLatin1String("BEVEL"))
        return Qt::BevelJoin;
    return Qt::MiterJoin;
}

// Figma's gradient handles are three points in the object's own 0..1 box: the
// start, the end, and one marking the gradient's width (see the REST Paint type).
Paint gradientPaint(PaintKind kind, const QVariantMap &paint)
{
    Paint result;
    result.kind = kind;
    for (const QVariant &entry : list(paint, "stops"))
        result.stops.push_back({num(entry.toMap(), "position"), color(map(entry.toMap(), "color"))});
    if (result.stops.empty())
        result.stops = {{0, Qt::black}, {1, Qt::white}};
    const QVariantList handles = list(paint, "gradientHandlePositions");
    QPointF start{0, 0.5}, end{1, 0.5};
    if (handles.size() >= 2) {
        start = point(handles[0].toMap());
        end = point(handles[1].toMap());
    } else {
        const QTransform t = matrix(paint.value(QStringLiteral("transform")));
        if (!t.isIdentity()) {
            start = t.map(QPointF(0, 0.5));
            end = t.map(QPointF(1, 0.5));
        }
    }
    result.start = start;
    result.end = end;
    return result;
}

Paint mapPaint(Context &ctx, const QVariantMap &paint)
{
    const QString type = str(paint, "type");
    const double opacity = num(paint, "opacity", 1);
    Paint result;
    if (type == QLatin1String("SOLID")) {
        result = Paint::solid(color(map(paint, "color"), opacity));
    } else if (type == QLatin1String("GRADIENT_LINEAR")) {
        result = gradientPaint(PaintKind::linearGradient, paint);
    } else if (type == QLatin1String("GRADIENT_RADIAL") || type == QLatin1String("GRADIENT_DIAMOND")) {
        if (type == QLatin1String("GRADIENT_DIAMOND"))
            ctx.warn(QStringLiteral("Diamond gradients came in as radial ones."));
        result = gradientPaint(PaintKind::radialGradient, paint);
    } else if (type == QLatin1String("GRADIENT_ANGULAR")) {
        ctx.warn(QStringLiteral("Angular gradients came in as radial ones."));
        result = gradientPaint(PaintKind::radialGradient, paint);
    } else if (type == QLatin1String("IMAGE")) {
        ctx.warn(QStringLiteral("Image fills on a shape with other fills, or a non-rectangular shape, were left out."));
        return Paint::none();
    } else {
        ctx.warn(QStringLiteral("Pattern fills were left out."));
        return Paint::none();
    }
    result.isHidden = !boolean(paint, "visible", true);
    return result;
}
}

std::optional<QImage> soleImageFill(Context &ctx, const QVariantMap &node)
{
    const QVariantList fills = list(node, "fillPaints");
    if (fills.size() != 1)
        return std::nullopt;
    const QVariantMap paint = fills.front().toMap();
    if (str(paint, "type") != QLatin1String("IMAGE") || !boolean(paint, "visible", true))
        return std::nullopt;
    const QVariantMap imageField = map(paint, "image");
    const QByteArray hashBytes = imageField.value(QStringLiteral("hash")).toByteArray();
    const QString hash = QString::fromLatin1(hashBytes.toHex());
    const auto found = ctx.imagesByHash.find(hash);
    if (found == ctx.imagesByHash.end() || found->second.isEmpty()) {
        ctx.warn(QStringLiteral("An image fill's bytes weren't available, so it was left out."));
        return std::nullopt;
    }
    QImage image;
    if (!image.loadFromData(found->second)) {
        ctx.warn(QStringLiteral("An image fill couldn't be decoded, so it was left out."));
        return std::nullopt;
    }
    ctx.warn(QStringLiteral("Image fills became separate image layers, so any shape they were cropped to was lost."));
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

void applyFillsAndStrokes(Context &ctx, const QVariantMap &node, VectorObject &object)
{
    std::vector<Paint> fills;
    for (const QVariant &entry : list(node, "fillPaints")) {
        const Paint paint = mapPaint(ctx, entry.toMap());
        if (paint.kind != PaintKind::none)
            fills.push_back(paint);
    }
    if (!fills.empty())
        object.setFills(fills);

    std::vector<StrokeStyle> strokes;
    const double weight = num(node, "strokeWeight", 1);
    const StrokeAlignment align = mapStrokeAlign(str(node, "strokeAlign"));
    const Qt::PenCapStyle cap = strokeCap(str(node, "strokeCap"));
    const Qt::PenJoinStyle join = strokeJoin(str(node, "strokeJoin"));
    std::vector<double> dashes;
    for (const QVariant &d : list(node, "dashPattern"))
        dashes.push_back(d.toDouble());
    if (dashes.empty()) {
        for (const QVariant &d : list(node, "strokeDashes"))
            dashes.push_back(d.toDouble());
    }
    for (const QVariant &entry : list(node, "strokePaints")) {
        const Paint paint = mapPaint(ctx, entry.toMap());
        if (paint.kind == PaintKind::none)
            continue;
        StrokeStyle stroke;
        stroke.paint = paint;
        stroke.width = weight;
        stroke.alignment = align;
        stroke.cap = cap;
        stroke.join = join;
        stroke.dashes = dashes;
        strokes.push_back(stroke);
    }
    if (!strokes.empty())
        object.setStrokes(strokes);
    if (node.contains(QStringLiteral("borderStrokeWeightsIndependent")) && boolean(node, "borderStrokeWeightsIndependent"))
        ctx.warn(QStringLiteral("Per-side stroke weights aren't supported yet; the top weight was used all round."));
}

// Omastrator has no effects yet (docs/FIGMA-AUDIT.md): every visible one is left out.
void applyEffects(Context &ctx, const QVariantMap &node, VectorObject &object)
{
    Q_UNUSED(object);
    for (const QVariant &entry : list(node, "effects")) {
        if (boolean(entry.toMap(), "visible", true)) {
            ctx.warn(QStringLiteral("Shadows and blurs aren't supported yet, so they were left out."));
            return;
        }
    }
}
}
