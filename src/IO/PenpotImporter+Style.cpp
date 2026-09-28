#include "IO/PenpotImporterParts.h"
#include <QHash>
#include <algorithm>

namespace {

Paint paintFrom(const QJsonObject &entry, const QString &colorKey, const QString &opacityKey, const QString &gradientKey,
                 const QString &imageKey, const QRectF &bounds, QStringList &warnings)
{
    if (entry.contains(gradientKey)) {
        const QJsonObject gradient = entry.value(gradientKey).toObject();
        Paint paint;
        paint.kind = gradient.value(QStringLiteral("type")).toString() == QLatin1String("radial") ? PaintKind::radialGradient
                                                                                                     : PaintKind::linearGradient;
        const double w = bounds.width() > 0 ? bounds.width() : 1;
        const double h = bounds.height() > 0 ? bounds.height() : 1;
        paint.start = QPointF((gradient.value(QStringLiteral("startX")).toDouble() - bounds.left()) / w,
                               (gradient.value(QStringLiteral("startY")).toDouble() - bounds.top()) / h);
        paint.end = QPointF((gradient.value(QStringLiteral("endX")).toDouble() - bounds.left()) / w,
                             (gradient.value(QStringLiteral("endY")).toDouble() - bounds.top()) / h);
        for (const QJsonValue &value : gradient.value(QStringLiteral("stops")).toArray()) {
            const QJsonObject stop = value.toObject();
            paint.stops.push_back({stop.value(QStringLiteral("offset")).toDouble(),
                                    PenpotImport::color(stop.value(QStringLiteral("color")).toString(),
                                                         stop.value(QStringLiteral("opacity")).toDouble(1))});
        }
        if (!paint.stops.empty())
            paint.color = paint.stops.front().color;
        return paint;
    }
    if (entry.contains(imageKey)) {
        warnings << QStringLiteral("Image and pattern fills were left out.");
        return Paint::none();
    }
    if (entry.contains(colorKey))
        return Paint::solid(PenpotImport::color(entry.value(colorKey).toString(), entry.value(opacityKey).toDouble(1)));
    return Paint::none();
}

Qt::PenCapStyle capFor(const QString &value)
{
    if (value == QLatin1String("round"))
        return Qt::RoundCap;
    if (value.contains(QLatin1String("square")) || value.contains(QLatin1String("triangle")) || value.contains(QLatin1String("arrow")))
        return Qt::SquareCap;
    return Qt::FlatCap;
}

}

namespace PenpotImport {

QColor color(const QString &hex, double opacity)
{
    QColor result(hex);
    if (!result.isValid())
        result = Qt::black;
    result.setAlphaF(float(std::clamp(opacity, 0.0, 1.0)));
    return result;
}

LayerBlendMode blendModeFor(const QString &value)
{
    static const QHash<QString, LayerBlendMode> modes{
        {QStringLiteral("darken"), LayerBlendMode::darken},         {QStringLiteral("multiply"), LayerBlendMode::multiply},
        {QStringLiteral("color-burn"), LayerBlendMode::colorBurn},  {QStringLiteral("lighten"), LayerBlendMode::lighten},
        {QStringLiteral("screen"), LayerBlendMode::screen},         {QStringLiteral("color-dodge"), LayerBlendMode::colorDodge},
        {QStringLiteral("overlay"), LayerBlendMode::overlay},       {QStringLiteral("soft-light"), LayerBlendMode::softLight},
        {QStringLiteral("hard-light"), LayerBlendMode::overlay},    // No match; closest of the overlay-family modes.
        {QStringLiteral("difference"), LayerBlendMode::difference}, {QStringLiteral("exclusion"), LayerBlendMode::difference},
        {QStringLiteral("hue"), LayerBlendMode::hue},                {QStringLiteral("saturation"), LayerBlendMode::saturation},
        {QStringLiteral("color"), LayerBlendMode::color},            {QStringLiteral("luminosity"), LayerBlendMode::luminosity}};
    return modes.value(value, LayerBlendMode::normal);
}

void applyStyle(const QJsonObject &shape, VectorObject &object, QStringList &warnings)
{
    const QRectF bounds(shape.value(QStringLiteral("x")).toDouble(), shape.value(QStringLiteral("y")).toDouble(),
                         shape.value(QStringLiteral("width")).toDouble(), shape.value(QStringLiteral("height")).toDouble());

    std::vector<Paint> fills;
    for (const QJsonValue &value : shape.value(QStringLiteral("fills")).toArray()) {
        fills.push_back(paintFrom(value.toObject(), QStringLiteral("fillColor"), QStringLiteral("fillOpacity"),
                                   QStringLiteral("fillColorGradient"), QStringLiteral("fillImage"), bounds, warnings));
    }
    if (!fills.empty())
        object.setFills(std::move(fills));

    std::vector<StrokeStyle> strokes;
    for (const QJsonValue &value : shape.value(QStringLiteral("strokes")).toArray()) {
        const QJsonObject entry = value.toObject();
        StrokeStyle stroke;
        stroke.paint = paintFrom(entry, QStringLiteral("strokeColor"), QStringLiteral("strokeOpacity"),
                                  QStringLiteral("strokeColorGradient"), QStringLiteral("strokeImage"), bounds, warnings);
        stroke.paint.isHidden = entry.value(QStringLiteral("hidden")).toBool(false);
        stroke.width = entry.value(QStringLiteral("strokeWidth")).toDouble(1);
        const QString style = entry.value(QStringLiteral("strokeStyle")).toString();
        if (style == QLatin1String("dashed"))
            stroke.dashes = {6, 4};
        else if (style == QLatin1String("dotted") || style == QLatin1String("mixed"))
            stroke.dashes = {1, 3};
        const QString alignment = entry.value(QStringLiteral("strokeAlignment")).toString();
        stroke.alignment = alignment == QLatin1String("inner") ? StrokeAlignment::inside
                            : alignment == QLatin1String("outer") ? StrokeAlignment::outside
                                                                    : StrokeAlignment::center;
        stroke.cap = capFor(entry.value(QStringLiteral("strokeCapStart")).toString());
        strokes.push_back(stroke);
    }
    if (!strokes.empty())
        object.setStrokes(std::move(strokes));

    bool hasShadow = false;
    for (const QJsonValue &value : shape.value(QStringLiteral("shadow")).toArray()) {
        if (!value.toObject().value(QStringLiteral("hidden")).toBool(false))
            hasShadow = true;
    }
    const QJsonObject blur = shape.value(QStringLiteral("blur")).toObject();
    const bool hasBlur = !blur.isEmpty() && !blur.value(QStringLiteral("hidden")).toBool(false);
    if (hasShadow)
        warnings << QStringLiteral("Shadows were left out.");
    if (hasBlur)
        warnings << QStringLiteral("Blur was left out.");
}

}
