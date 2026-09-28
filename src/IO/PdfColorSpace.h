#pragma once
#include "IO/PdfFunction.h"
#include "IO/PdfObject.h"
#include <QColor>
#include <QStringList>
#include <memory>

// Resolves a PDF color space to something that turns component values into a
// QColor: DeviceGray/RGB/CMYK, ICCBased (through /Alternate or /N), Indexed,
// Separation/DeviceN (through the tint transform function), CalGray/CalRGB
// (as their device equivalents) and Lab. Patterns are reported via
// `isPattern()`; the content interpreter paints them, not this class.
namespace Pdf {
class Document;

class ColorSpace {
public:
    static ColorSpace load(const Document &document, const Object &spaceObject, QStringList *warnings = nullptr);
    static ColorSpace deviceGray();
    static ColorSpace deviceRGB();
    static ColorSpace deviceCMYK();

    int componentCount() const { return m_components; }
    bool isPattern() const { return m_kind == Kind::pattern; }
    bool isCmyk() const { return m_kind == Kind::cmyk; }
    bool isIndexed() const { return m_kind == Kind::indexed; }
    const ColorSpace *patternBase() const { return m_base.get(); }
    QColor toColor(const QList<double> &components) const;
    QList<double> defaultComponents() const;

private:
    enum class Kind { gray, rgb, cmyk, lab, indexed, separation, pattern } m_kind = Kind::gray;
    int m_components = 1;
    std::shared_ptr<ColorSpace> m_base; // Indexed's base, Separation/DeviceN's alternate, or an uncolored pattern's base
    QByteArray m_lookup; // Indexed
    int m_highValue = 0; // Indexed
    Function m_tintTransform; // Separation/DeviceN
    QList<double> m_labRange{-100, 100, -100, 100}; // Lab's a*/b* range, from its dictionary
};

}
