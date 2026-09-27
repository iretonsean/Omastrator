#include "Document/LayerAppearance.h"
#include <algorithm>
#include <cmath>
#include <array>
#include <utility>

namespace {
const std::array<std::pair<LayerBlendMode, const char *>, 14> names{{
    {LayerBlendMode::normal, "Normal"}, {LayerBlendMode::multiply, "Multiply"}, {LayerBlendMode::screen, "Screen"},
    {LayerBlendMode::overlay, "Overlay"}, {LayerBlendMode::softLight, "Soft Light"}, {LayerBlendMode::darken, "Darken"}, {LayerBlendMode::lighten, "Lighten"},
    {LayerBlendMode::difference, "Difference"}, {LayerBlendMode::colorDodge, "Color Dodge"},
    {LayerBlendMode::colorBurn, "Color Burn"}, {LayerBlendMode::hue, "Hue"}, {LayerBlendMode::saturation, "Saturation"},
    {LayerBlendMode::color, "Color"}, {LayerBlendMode::luminosity, "Luminosity"},
}};
}

QString rawValue(LayerBlendMode mode)
{
    return QString::fromLatin1(names.at(size_t(mode)).second);
}

std::optional<LayerBlendMode> layerBlendMode(const QString &rawValue)
{
    for (const auto &[mode, name] : names) {
        if (rawValue == QLatin1String(name))
            return mode;
    }
    return std::nullopt;
}
