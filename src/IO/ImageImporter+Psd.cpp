#include "IO/FileError.h"
#include "IO/ImageImporterParts.h"
#include <QHash>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// A cursor over the PSD/PSB byte stream; every read is bounds-checked since a
// truncated or malformed file must fail cleanly, not read past the buffer.
class Cursor {
public:
    explicit Cursor(const QByteArray &data) : m_data(data) {}

    quint16 u16()
    {
        need(2);
        const quint16 value = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(m_data.constData() + m_pos));
        m_pos += 2;
        return value;
    }

    quint32 u32()
    {
        need(4);
        const quint32 value = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(m_data.constData() + m_pos));
        m_pos += 4;
        return value;
    }

    quint64 u64()
    {
        need(8);
        const quint64 value = qFromBigEndian<quint64>(reinterpret_cast<const uchar *>(m_data.constData() + m_pos));
        m_pos += 8;
        return value;
    }

    quint8 byte()
    {
        need(1);
        return quint8(m_data[m_pos++]);
    }

    void skip(qint64 count)
    {
        need(count);
        m_pos += count;
    }

    const char *pointer() const { return m_data.constData() + m_pos; }
    qint64 remaining() const { return qint64(m_data.size()) - m_pos; }

private:
    void need(qint64 count) const
    {
        if (count < 0 || m_pos + count > m_data.size())
            throw FileError(QStringLiteral("This PSD is truncated or damaged."));
    }

    const QByteArray &m_data;
    qint64 m_pos = 0;
};

// PackBits: n in [0,127] copies the next n+1 bytes; n in [-127,-1] repeats the
// next byte 1-n times; n == -128 is a no-op.
void unpackBits(Cursor &cursor, char *out, qint64 outSize)
{
    qint64 written = 0;
    while (written < outSize) {
        const qint8 n = qint8(cursor.byte());
        if (n >= 0) {
            const qint64 count = qint64(n) + 1;
            if (written + count > outSize)
                throw FileError(QStringLiteral("This PSD's compressed image data is malformed."));
            const char *from = cursor.pointer();
            cursor.skip(count);
            std::memcpy(out + written, from, size_t(count));
            written += count;
        } else if (n != -128) {
            const qint64 count = 1 - qint64(n);
            if (written + count > outSize)
                throw FileError(QStringLiteral("This PSD's compressed image data is malformed."));
            const quint8 value = cursor.byte();
            std::memset(out + written, value, size_t(count));
            written += count;
        }
    }
}

}

namespace ImageImport {

bool isPsd(const QByteArray &head)
{
    return head.size() >= 4 && head.startsWith("8BPS");
}

QImage readPsdComposite(const QByteArray &data)
{
    Cursor cursor(data);
    if (cursor.u32() != 0x38425053 /* "8BPS" */)
        throw FileError(QStringLiteral("This is not a PSD file."));
    const quint16 version = cursor.u16();
    if (version != 1 && version != 2)
        throw FileError(QStringLiteral("This is not a PSD file."));
    const bool isPsb = version == 2;
    cursor.skip(6);
    const quint16 channels = cursor.u16();
    const quint32 height = cursor.u32();
    const quint32 width = cursor.u32();
    const quint16 depth = cursor.u16();
    const quint16 colorMode = cursor.u16();

    if (width == 0 || height == 0 || width > 30'000 || height > 30'000)
        throw FileError(QStringLiteral("This PSD has no usable size."));
    if (depth != 8)
        throw FileError(QStringLiteral("Only 8-bit-per-channel PSDs can be placed; this one is %1-bit.").arg(depth));

    static const QHash<quint16, QString> modeNames{{0, QStringLiteral("Bitmap")},  {2, QStringLiteral("indexed-color")},
                                                     {7, QStringLiteral("multichannel")}, {8, QStringLiteral("duotone")},
                                                     {9, QStringLiteral("Lab")}};
    const bool isRgb = colorMode == 3, isGray = colorMode == 1, isCmyk = colorMode == 4;
    if (!isRgb && !isGray && !isCmyk) {
        throw FileError(QStringLiteral("%1 PSDs aren't supported for placing; convert to RGB and re-save.")
                             .arg(modeNames.value(colorMode, QStringLiteral("This kind of"))));
    }

    // Color mode data, then image resources: both length-prefixed blocks to skip whole.
    cursor.skip(cursor.u32());
    cursor.skip(cursor.u32());
    // Layer and mask info: a 32-bit length in PSD, 64-bit in PSB.
    cursor.skip(isPsb ? qint64(cursor.u64()) : qint64(cursor.u32()));

    const quint16 compression = cursor.u16();
    if (compression != 0 && compression != 1)
        throw FileError(QStringLiteral("This PSD's image data is ZIP-compressed, which isn't supported for placing."));

    const int baseChannels = isRgb ? 3 : isGray ? 1 : 4;
    const bool hasAlpha = channels > baseChannels;
    const qint64 planeSize = qint64(width) * qint64(height);
    std::vector<QByteArray> planes(size_t(baseChannels + (hasAlpha ? 1 : 0)));
    const int planeCount = int(planes.size());

    if (compression == 0) {
        for (int c = 0; c < planeCount; ++c) {
            QByteArray plane(int(planeSize), Qt::Uninitialized);
            std::memcpy(plane.data(), cursor.pointer(), size_t(planeSize));
            cursor.skip(planeSize);
            planes[size_t(c)] = std::move(plane);
        }
    } else {
        // The byte-count table covers every scanline of every channel, channel-major; PackBits
        // is self-delimiting, so only the cursor needs to skip past the table, not its values.
        const qint64 rowCount = qint64(planeCount) * height;
        for (qint64 i = 0; i < rowCount; ++i) {
            if (isPsb)
                cursor.u32();
            else
                cursor.u16();
        }
        for (int c = 0; c < planeCount; ++c) {
            QByteArray plane(int(planeSize), Qt::Uninitialized);
            for (quint32 row = 0; row < height; ++row) {
                unpackBits(cursor, plane.data() + qint64(row) * width, width);
            }
            planes[size_t(c)] = std::move(plane);
        }
    }

    QImage image(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
    for (quint32 y = 0; y < height; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(int(y)));
        for (quint32 x = 0; x < width; ++x) {
            const qint64 i = qint64(y) * width + x;
            int r, g, b;
            if (isRgb) {
                r = quint8(planes[0][i]);
                g = quint8(planes[1][i]);
                b = quint8(planes[2][i]);
            } else if (isGray) {
                r = g = b = quint8(planes[0][i]);
            } else {
                // CMYK stores ink amounts inverted (255 = no ink); a plain, uncalibrated conversion.
                const int c = 255 - quint8(planes[0][i]);
                const int m = 255 - quint8(planes[1][i]);
                const int yy = 255 - quint8(planes[2][i]);
                const int k = 255 - quint8(planes[3][i]);
                r = 255 - std::min(255, c + k);
                g = 255 - std::min(255, m + k);
                b = 255 - std::min(255, yy + k);
            }
            const int a = hasAlpha ? quint8(planes[size_t(baseChannels)][i]) : 255;
            row[x] = qPremultiply(qRgba(r, g, b, a));
        }
    }
    return image;
}

}
