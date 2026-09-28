#include "IO/PdfColorSpace.h"
#include "IO/PdfDocument.h"
#include <algorithm>
#include <cmath>

namespace Pdf {

namespace {
int channelByte(double v)
{
    return std::clamp(int(std::round(std::clamp(v, 0.0, 1.0) * 255)), 0, 255);
}
QColor rgbColor(double r, double g, double b)
{
    return QColor(channelByte(r), channelByte(g), channelByte(b));
}
QColor cmykColor(double c, double m, double y, double k)
{
    return rgbColor(1.0 - std::min(1.0, c + k), 1.0 - std::min(1.0, m + k), 1.0 - std::min(1.0, y + k));
}
// A D65 sRGB approximation; PDF Lab is nominally D50, but the difference is
// not visible enough to warrant a full chromatic-adaptation matrix here.
QColor labColor(double L, double a, double b)
{
    const double fy = (L + 16) / 116;
    const double fx = fy + a / 500;
    const double fz = fy - b / 200;
    const auto finv = [](double t) { return t > 6.0 / 29 ? t * t * t : 3 * (6.0 / 29) * (6.0 / 29) * (t - 4.0 / 29); };
    const double x = 0.9505 * finv(fx), y = 1.0 * finv(fy), z = 1.089 * finv(fz);
    const double r = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    const double g = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    const double bl = 0.0557 * x - 0.2040 * y + 1.0570 * z;
    const auto gamma = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
    };
    return rgbColor(gamma(r), gamma(g), gamma(bl));
}
}

ColorSpace ColorSpace::deviceGray()
{
    ColorSpace space;
    space.m_kind = Kind::gray;
    space.m_components = 1;
    return space;
}
ColorSpace ColorSpace::deviceRGB()
{
    ColorSpace space;
    space.m_kind = Kind::rgb;
    space.m_components = 3;
    return space;
}
ColorSpace ColorSpace::deviceCMYK()
{
    ColorSpace space;
    space.m_kind = Kind::cmyk;
    space.m_components = 4;
    return space;
}

QColor ColorSpace::toColor(const QList<double> &components) const
{
    const auto at = [&](int i) { return i < components.size() ? components[i] : 0.0; };
    switch (m_kind) {
    case Kind::gray:
        return rgbColor(at(0), at(0), at(0));
    case Kind::rgb:
        return rgbColor(at(0), at(1), at(2));
    case Kind::cmyk:
        return cmykColor(at(0), at(1), at(2), at(3));
    case Kind::lab:
        return labColor(std::clamp(at(0), 0.0, 100.0), std::clamp(at(1), m_labRange.value(0, -100.0), m_labRange.value(1, 100.0)),
                         std::clamp(at(2), m_labRange.value(2, -100.0), m_labRange.value(3, 100.0)));
    case Kind::indexed: {
        const int index = std::clamp(int(std::round(at(0))), 0, m_highValue);
        const int baseComponents = m_base ? m_base->componentCount() : 1;
        QList<double> baseValues;
        for (int i = 0; i < baseComponents; ++i) {
            const qsizetype byteIndex = qsizetype(index) * baseComponents + i;
            const uchar sample = byteIndex < m_lookup.size() ? uchar(m_lookup[byteIndex]) : 0;
            baseValues.append(sample / 255.0);
        }
        return m_base ? m_base->toColor(baseValues) : rgbColor(0, 0, 0);
    }
    case Kind::separation: {
        const QList<double> alternate = m_tintTransform.isValid() ? m_tintTransform.evaluate(components) : components;
        return m_base ? m_base->toColor(alternate) : rgbColor(1 - at(0), 1 - at(0), 1 - at(0));
    }
    case Kind::pattern:
        return m_base ? m_base->toColor(components) : QColor(Qt::black);
    }
    return QColor(Qt::black);
}

QList<double> ColorSpace::defaultComponents() const
{
    switch (m_kind) {
    case Kind::gray:
        return {0};
    case Kind::rgb:
        return {0, 0, 0};
    case Kind::cmyk:
        return {0, 0, 0, 1};
    case Kind::lab:
        return {0, 0, 0};
    case Kind::indexed:
        return {0};
    case Kind::separation: {
        QList<double> values;
        for (int i = 0; i < m_components; ++i)
            values.append(1.0);
        return values;
    }
    case Kind::pattern:
        return {};
    }
    return {0};
}

ColorSpace ColorSpace::load(const Document &document, const Object &spaceObject, QStringList *warnings)
{
    const Object resolved = document.resolve(spaceObject);

    QByteArray family;
    Array familyArray;
    if (resolved.isName()) {
        family = resolved.toNameValue();
    } else if (resolved.isArray()) {
        familyArray = resolved.toArray();
        if (!familyArray.isEmpty())
            family = document.resolve(familyArray[0]).toNameValue();
    }

    if (family == "DeviceGray" || family == "CalGray" || family == "G")
        return deviceGray();
    if (family == "DeviceRGB" || family == "CalRGB" || family == "RGB")
        return deviceRGB();
    if (family == "DeviceCMYK" || family == "CMYK")
        return deviceCMYK();

    if (family == "ICCBased" && familyArray.size() > 1) {
        const Object stream = document.resolve(familyArray[1]);
        int n = int(document.resolve(stream.at(QStringLiteral("N"))).toInt(0));
        if (n == 0) {
            const Object alternate = stream.at(QStringLiteral("Alternate"));
            if (!alternate.isNull())
                return load(document, alternate, warnings);
            n = 3; // no /N and no /Alternate: assume RGB, the common case
        }
        if (n == 1)
            return deviceGray();
        if (n == 4)
            return deviceCMYK();
        return deviceRGB();
    }

    if (family == "Lab" && familyArray.size() > 1) {
        ColorSpace space;
        space.m_kind = Kind::lab;
        space.m_components = 3;
        const Dict dict = document.resolve(familyArray[1]).toDict();
        const Array range = document.resolve(dict.value(QStringLiteral("Range"))).toArray();
        if (range.size() == 4)
            space.m_labRange = {range[0].toReal(-100), range[1].toReal(100), range[2].toReal(-100), range[3].toReal(100)};
        return space;
    }

    if ((family == "Indexed" || family == "I") && familyArray.size() > 3) {
        ColorSpace space;
        space.m_kind = Kind::indexed;
        space.m_components = 1;
        space.m_base = std::make_shared<ColorSpace>(load(document, familyArray[1], warnings));
        space.m_highValue = int(document.resolve(familyArray[2]).toInt(0));
        const Object lookupObject = document.resolve(familyArray[3]);
        if (lookupObject.isStream())
            space.m_lookup = document.streamData(lookupObject).bytes;
        else if (lookupObject.isString())
            space.m_lookup = lookupObject.toStringValue();
        return space;
    }

    if (family == "Separation" && familyArray.size() > 2) {
        ColorSpace space;
        space.m_kind = Kind::separation;
        space.m_components = 1;
        space.m_base = std::make_shared<ColorSpace>(load(document, familyArray[2], warnings));
        if (familyArray.size() > 3)
            space.m_tintTransform = Function::load(document, familyArray[3]);
        return space;
    }
    if (family == "DeviceN" && familyArray.size() > 2) {
        ColorSpace space;
        space.m_kind = Kind::separation;
        space.m_components = std::max(1, int(document.resolve(familyArray[1]).toArray().size()));
        space.m_base = std::make_shared<ColorSpace>(load(document, familyArray[2], warnings));
        if (familyArray.size() > 3)
            space.m_tintTransform = Function::load(document, familyArray[3]);
        return space;
    }

    if (family == "Pattern") {
        ColorSpace space;
        space.m_kind = Kind::pattern;
        space.m_components = 1;
        if (familyArray.size() > 1)
            space.m_base = std::make_shared<ColorSpace>(load(document, familyArray[1], warnings));
        return space;
    }

    if (warnings && !family.isEmpty())
        *warnings << QStringLiteral("An unrecognised color space (%1) was treated as RGB.").arg(QString::fromLatin1(family));
    return deviceRGB();
}

}
