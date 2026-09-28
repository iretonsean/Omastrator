#include "IO/PdfContent.h"
#include "IO/PdfDocument.h"
#include <algorithm>

namespace Pdf {

namespace {
ColorSpace resolveNamedColorSpace(const Document &document, const Object &value, const Dict &resources, QStringList *warnings)
{
    if (value.isName()) {
        const QByteArray name = value.toNameValue();
        static const QSet<QByteArray> deviceNames{"DeviceGray", "DeviceRGB", "DeviceCMYK", "G", "RGB", "CMYK", "CalGray", "CalRGB"};
        if (!deviceNames.contains(name)) {
            const Dict csResources = document.resolve(resources.value(QStringLiteral("ColorSpace"))).toDict();
            const Object definition = csResources.value(QString::fromLatin1(name));
            if (!definition.isNull())
                return ColorSpace::load(document, definition, warnings);
        }
    }
    return ColorSpace::load(document, value, warnings);
}
}

QImage Interpreter::decodeImageObject(const Object &imageObject, const Dict &resources)
{
    const Dict &dict = imageObject.toDict();
    const int width = int(m_document.resolve(dict.value(QStringLiteral("Width"))).toInt(0));
    const int height = int(m_document.resolve(dict.value(QStringLiteral("Height"))).toInt(0));
    if (width <= 0 || height <= 0)
        return QImage();

    const bool isMask = m_document.resolve(dict.value(QStringLiteral("ImageMask"))).toBool(false);
    const PdfFilters::Decoded decoded = m_document.streamData(imageObject);
    if (m_warnings)
        *m_warnings << decoded.warnings;
    const Array decodeArray = m_document.resolve(dict.value(QStringLiteral("Decode"))).toArray();

    QImage image;
    if (!decoded.imageFilter.isEmpty()) {
        if (decoded.imageFilter == "DCTDecode") {
            image = QImage::fromData(decoded.bytes, "JPEG");
        } else if (decoded.imageFilter == "JPXDecode") {
            image = QImage::fromData(decoded.bytes, "JP2");
            if (image.isNull())
                warnOnce(QStringLiteral("jpx"), QStringLiteral("A JPEG2000 image could not be read and was left out."));
        } else if (decoded.imageFilter == "CCITTFaxDecode") {
            warnOnce(QStringLiteral("ccitt"), QStringLiteral("A fax-compressed image could not be read and was left out."));
        }
        if (image.isNull())
            return QImage();
        image = image.convertToFormat(QImage::Format_ARGB32);
    } else if (isMask) {
        const bool invert = decodeArray.size() == 2 && decodeArray[0].toReal(0) == 1;
        const QColor paintColor = m_state.fillSpace.toColor(m_state.fillComponents);
        image = QImage(width, height, QImage::Format_ARGB32);
        const qint64 rowBytes = (qint64(width) + 7) / 8;
        for (int y = 0; y < height; ++y) {
            const qint64 rowStart = qint64(y) * rowBytes;
            for (int x = 0; x < width; ++x) {
                const qint64 byteIndex = rowStart + x / 8;
                const int bit = byteIndex < decoded.bytes.size() ? (uchar(decoded.bytes[byteIndex]) >> (7 - (x % 8))) & 1 : 0;
                const bool paint = invert ? bit == 1 : bit == 0;
                image.setPixelColor(x, y, paint ? paintColor : QColor(0, 0, 0, 0));
            }
        }
    } else {
        const ColorSpace space = resolveNamedColorSpace(m_document, dict.value(QStringLiteral("ColorSpace")), resources, m_warnings);
        if (space.isCmyk())
            warnOnce(QStringLiteral("cmyk"), QStringLiteral("CMYK colors were converted to RGB directly, without a color profile."));
        const int bitsPerComponent = int(m_document.resolve(dict.value(QStringLiteral("BitsPerComponent"))).toInt(8));
        const int components = std::max(1, space.componentCount());
        const qint64 rowBytes = (qint64(width) * components * bitsPerComponent + 7) / 8;
        const double maxValue = double((quint64(1) << std::clamp(bitsPerComponent, 1, 32)) - 1);
        image = QImage(width, height, QImage::Format_ARGB32);

        for (int y = 0; y < height; ++y) {
            qint64 bitPos = qint64(y) * rowBytes * 8;
            for (int x = 0; x < width; ++x) {
                QList<double> values;
                for (int c = 0; c < components; ++c) {
                    quint64 raw = 0;
                    for (int b = 0; b < bitsPerComponent; ++b) {
                        const qint64 byteIndex = bitPos / 8;
                        const int bitIndex = 7 - int(bitPos % 8);
                        const int bit = byteIndex < decoded.bytes.size() ? (uchar(decoded.bytes[byteIndex]) >> bitIndex) & 1 : 0;
                        raw = (raw << 1) | quint64(bit);
                        ++bitPos;
                    }
                    double component;
                    if (space.isIndexed()) {
                        component = double(raw);
                        if (decodeArray.size() >= 2)
                            component = decodeArray[0].toReal(0) + (double(raw) / maxValue) * (decodeArray[1].toReal(maxValue) - decodeArray[0].toReal(0));
                    } else {
                        const double d0 = decodeArray.size() >= (c + 1) * 2 ? decodeArray[c * 2].toReal(0) : 0.0;
                        const double d1 = decodeArray.size() >= (c + 1) * 2 ? decodeArray[c * 2 + 1].toReal(1) : 1.0;
                        component = d0 + (maxValue > 0 ? double(raw) / maxValue : 0) * (d1 - d0);
                    }
                    values.append(component);
                }
                image.setPixelColor(x, y, space.toColor(values));
            }
        }
    }

    // Soft mask / stencil mask: recolor as alpha, resizing to the base image if needed.
    const Object smaskObject = m_document.resolve(dict.value(QStringLiteral("SMask")));
    const Object maskObject = m_document.resolve(dict.value(QStringLiteral("Mask")));
    QImage alphaSource;
    if (smaskObject.isStream())
        alphaSource = decodeImageObject(smaskObject, resources);
    else if (maskObject.isStream())
        alphaSource = decodeImageObject(maskObject, resources);
    else if (maskObject.isArray())
        warnOnce(QStringLiteral("colorkey-mask"), QStringLiteral("A color-key mask was left out."));

    if (!alphaSource.isNull() && !image.isNull()) {
        if (alphaSource.size() != image.size())
            alphaSource = alphaSource.scaled(image.size());
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor pixel = image.pixelColor(x, y);
                const QColor alphaPixel = alphaSource.pixelColor(x, y);
                pixel.setAlpha(alphaSource.hasAlphaChannel() ? alphaPixel.alpha() : alphaPixel.red());
                image.setPixelColor(x, y, pixel);
            }
        }
    }
    return image;
}

}
