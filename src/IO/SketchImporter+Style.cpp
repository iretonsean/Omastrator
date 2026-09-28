#include "IO/SketchImporterParts.h"
#include <QHash>
#include <algorithm>

namespace {

// 0=Color, 1=Gradient, 2=Pattern (image): only the first two have a match.
Paint paintFrom(const QJsonObject &entry, QStringList &warnings)
{
    const int fillType = entry.value(QStringLiteral("fillType")).toInt(0);
    Paint paint;
    if (fillType == 1) {
        const QJsonObject gradient = entry.value(QStringLiteral("gradient")).toObject();
        const int gradientType = gradient.value(QStringLiteral("gradientType")).toInt(0);
        paint.kind = gradientType == 1 ? PaintKind::radialGradient : PaintKind::linearGradient;
        if (gradientType == 2)
            warnings << QStringLiteral("Angular gradients became radial gradients.");
        // Already fractions of the shape's own frame, like Paint's start/end.
        paint.start = SketchImport::fractionPoint(gradient.value(QStringLiteral("from")).toString());
        paint.end = SketchImport::fractionPoint(gradient.value(QStringLiteral("to")).toString());
        for (const QJsonValue &value : gradient.value(QStringLiteral("stops")).toArray()) {
            const QJsonObject stop = value.toObject();
            paint.stops.push_back({stop.value(QStringLiteral("position")).toDouble(),
                                    SketchImport::color(stop.value(QStringLiteral("color")).toObject())});
        }
        if (!paint.stops.empty())
            paint.color = paint.stops.front().color;
    } else if (fillType == 0) {
        paint.kind = PaintKind::solid;
        paint.color = SketchImport::color(entry.value(QStringLiteral("color")).toObject());
    } else {
        warnings << QStringLiteral("Image and pattern fills were left out.");
        return Paint::none();
    }
    const QJsonObject context = entry.value(QStringLiteral("contextSettings")).toObject();
    paint.opacity = context.value(QStringLiteral("opacity")).toDouble(1);
    paint.blendMode = SketchImport::blendModeFor(context.value(QStringLiteral("blendMode")).toInt(0));
    paint.isHidden = !entry.value(QStringLiteral("isEnabled")).toBool(true);
    return paint;
}

Qt::PenCapStyle capStyleFor(int value)
{
    switch (value) {
    case 1: return Qt::RoundCap;
    case 2: return Qt::SquareCap;
    default: return Qt::FlatCap;
    }
}

Qt::PenJoinStyle joinStyleFor(int value)
{
    switch (value) {
    case 1: return Qt::RoundJoin;
    case 2: return Qt::BevelJoin;
    default: return Qt::MiterJoin;
    }
}

StrokeStyle strokeFrom(const QJsonObject &entry, const QJsonObject &borderOptions, QStringList &warnings)
{
    StrokeStyle stroke;
    stroke.paint = paintFrom(entry, warnings);
    stroke.width = entry.value(QStringLiteral("thickness")).toDouble(1);
    // 0=Center, 1=Inside, 2=Outside: the same order as StrokeAlignment.
    stroke.alignment = static_cast<StrokeAlignment>(std::clamp(entry.value(QStringLiteral("position")).toInt(0), 0, 2));
    stroke.cap = capStyleFor(borderOptions.value(QStringLiteral("lineCapStyle")).toInt(0));
    stroke.join = joinStyleFor(borderOptions.value(QStringLiteral("lineJoinStyle")).toInt(0));
    for (const QJsonValue &value : borderOptions.value(QStringLiteral("dashPattern")).toArray())
        stroke.dashes.push_back(value.toDouble());
    return stroke;
}

}

namespace SketchImport {

QColor color(const QJsonObject &json)
{
    const auto channel = [&](const char *key) { return std::clamp(json.value(QLatin1String(key)).toDouble(0), 0.0, 1.0); };
    QColor result;
    result.setRgbF(float(channel("red")), float(channel("green")), float(channel("blue")), float(channel("alpha")));
    return result;
}

LayerBlendMode blendModeFor(int value)
{
    switch (value) {
    case 1: return LayerBlendMode::darken;
    case 2: return LayerBlendMode::multiply;
    case 3: return LayerBlendMode::colorBurn;
    case 4: return LayerBlendMode::lighten;
    case 5: return LayerBlendMode::screen;
    case 6: return LayerBlendMode::colorDodge;
    case 7: return LayerBlendMode::overlay;
    case 8: return LayerBlendMode::softLight;
    case 9: return LayerBlendMode::overlay;    // Hard Light: no match, closest of the two overlay-family modes.
    case 10: return LayerBlendMode::difference;
    case 11: return LayerBlendMode::difference; // Exclusion: no match, closest available.
    case 12: return LayerBlendMode::hue;
    case 13: return LayerBlendMode::saturation;
    case 14: return LayerBlendMode::color;
    case 15: return LayerBlendMode::luminosity;
    default: return LayerBlendMode::normal;
    }
}

void applyStyle(const QJsonObject &style, VectorObject &object, QStringList &warnings)
{
    const QJsonObject borderOptions = style.value(QStringLiteral("borderOptions")).toObject();
    std::vector<Paint> fills;
    for (const QJsonValue &value : style.value(QStringLiteral("fills")).toArray())
        fills.push_back(paintFrom(value.toObject(), warnings));
    if (!fills.empty())
        object.setFills(std::move(fills));

    std::vector<StrokeStyle> strokes;
    for (const QJsonValue &value : style.value(QStringLiteral("borders")).toArray())
        strokes.push_back(strokeFrom(value.toObject(), borderOptions, warnings));
    if (!strokes.empty())
        object.setStrokes(std::move(strokes));

    const QJsonObject context = style.value(QStringLiteral("contextSettings")).toObject();
    object.opacity = std::clamp(context.value(QStringLiteral("opacity")).toDouble(1), 0.0, 1.0);
    object.blendMode = blendModeFor(context.value(QStringLiteral("blendMode")).toInt(0));

    const bool hasShadow = !style.value(QStringLiteral("shadows")).toArray().isEmpty()
                            || !style.value(QStringLiteral("innerShadows")).toArray().isEmpty();
    const bool hasBlur = style.value(QStringLiteral("blur")).toObject().value(QStringLiteral("isEnabled")).toBool(false);
    if (hasShadow)
        warnings << QStringLiteral("Shadows were left out.");
    if (hasBlur)
        warnings << QStringLiteral("Blur was left out.");
}

}
