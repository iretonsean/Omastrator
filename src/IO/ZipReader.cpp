#include "IO/ZipReader.h"
#include <QDataStream>
#include <QtEndian>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {
constexpr quint32 kEocdSignature = 0x06054b50;
constexpr quint32 kCentralDirectorySignature = 0x02014b50;
constexpr quint32 kLocalFileHeaderSignature = 0x04034b50;

quint16 readLE16(const QByteArray &data, int offset) {
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 readLE32(const QByteArray &data, int offset) {
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

// Zip entries can be named with backslashes by tools that pack on Windows.
QString normalizeName(const QString &name) {
    QString normalized = name;
    return normalized.replace('\\', '/');
}
}

ZipReader::ZipReader(const QByteArray &archive) : m_archive(archive) {
    parseCentralDirectory();
}

void ZipReader::parseCentralDirectory() {
    // The end-of-central-directory record sits at the end of the file, after an optional
    // comment of up to 65535 bytes, so search backward for its signature.
    const int minEocdSize = 22;
    if (m_archive.size() < minEocdSize) return;
    const int searchStart = qMax(0, m_archive.size() - minEocdSize - 65535);
    int eocdOffset = -1;
    for (int i = m_archive.size() - minEocdSize; i >= searchStart; --i) {
        if (readLE32(m_archive, i) == kEocdSignature) {
            eocdOffset = i;
            break;
        }
    }
    if (eocdOffset < 0) return;

    const quint16 totalEntries = readLE16(m_archive, eocdOffset + 10);
    const quint32 cdOffset = readLE32(m_archive, eocdOffset + 16);
    if (cdOffset >= static_cast<quint32>(m_archive.size())) return;

    int pos = static_cast<int>(cdOffset);
    for (int i = 0; i < totalEntries; ++i) {
        if (pos + 46 > m_archive.size()) break;
        if (readLE32(m_archive, pos) != kCentralDirectorySignature) break;

        Entry entry;
        entry.method = readLE16(m_archive, pos + 10);
        entry.compressedSize = readLE32(m_archive, pos + 20);
        entry.uncompressedSize = readLE32(m_archive, pos + 24);
        const quint16 nameLength = readLE16(m_archive, pos + 28);
        const quint16 extraLength = readLE16(m_archive, pos + 30);
        const quint16 commentLength = readLE16(m_archive, pos + 32);
        entry.localHeaderOffset = readLE32(m_archive, pos + 42);

        const int nameOffset = pos + 46;
        if (nameOffset + nameLength > m_archive.size()) break;
        const QString name = normalizeName(QString::fromUtf8(m_archive.constData() + nameOffset, nameLength));
        if (!name.isEmpty() && !name.endsWith('/')) m_entries.insert(name, entry);

        pos = nameOffset + nameLength + extraLength + commentLength;
    }
    m_valid = !m_entries.isEmpty();
}

QByteArray ZipReader::read(const QString &name) const {
    const auto it = m_entries.constFind(normalizeName(name));
    if (it == m_entries.constEnd()) return {};
    const Entry &entry = it.value();

    const quint32 offset = entry.localHeaderOffset;
    if (static_cast<qint64>(offset) + 30 > m_archive.size()) return {};
    if (readLE32(m_archive, offset) != kLocalFileHeaderSignature) return {};

    const quint16 nameLength = readLE16(m_archive, offset + 26);
    const quint16 extraLength = readLE16(m_archive, offset + 28);
    const qint64 dataOffset = static_cast<qint64>(offset) + 30 + nameLength + extraLength;
    if (dataOffset + entry.compressedSize > m_archive.size()) return {};

    const char *dataStart = m_archive.constData() + dataOffset;
    if (entry.method == 0) return QByteArray(dataStart, static_cast<int>(entry.compressedSize));
    if (entry.method != 8) return {};  // only stored and deflate are supported

#ifdef OMASTRATOR_HAVE_ZLIB
    QByteArray output;
    output.resize(static_cast<int>(entry.uncompressedSize));
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(dataStart));
    stream.avail_in = entry.compressedSize;
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    // Raw deflate stream: negative window bits skip the zlib header zip entries don't have.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return {};
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END) return {};
    return output;
#else
    return {};
#endif
}
