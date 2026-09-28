#include "IO/ZipReader.h"
#include "Logging.h"
#include <QDataStream>
#include <QtEndian>

#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {
constexpr quint32 endOfCentralDirectorySignature = 0x06054b50;
constexpr quint32 centralDirectorySignature = 0x02014b50;
constexpr quint32 localFileHeaderSignature = 0x04034b50;
// A comment can push the record up to 64 KB before the end.
constexpr qsizetype maximumCommentSize = 65536 + 22;

quint16 le16(const char *p)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(p));
}

quint32 le32(const char *p)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(p));
}

#ifdef OMASTRATOR_HAVE_ZLIB
QByteArray inflateRaw(const char *src, quint32 compressedSize, quint32 uncompressedSize)
{
    QByteArray out(int(uncompressedSize), Qt::Uninitialized);
    if (uncompressedSize == 0)
        return {};
    z_stream stream{};
    // Negative window bits: raw deflate, no zlib header (what zip entries use).
    if (inflateInit2(&stream, -15) != Z_OK)
        return {};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(src));
    stream.avail_in = compressedSize;
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);
    if (result != Z_STREAM_END)
        return {};
    return out;
}
#endif
}

ZipReader::ZipReader(const QByteArray &archive) : data(archive)
{
    valid = readCentralDirectory();
}

bool ZipReader::isValid() const
{
    return valid;
}

QStringList ZipReader::entries() const
{
    QStringList names;
    for (const Entry &entry : items)
        names << entry.name;
    return names;
}

bool ZipReader::readCentralDirectory()
{
    if (data.size() < 22)
        return false;
    // Find the end-of-central-directory record, searching back from the end
    // (a trailing comment can push it away from the last 22 bytes).
    const qsizetype searchFrom = std::max(qsizetype(0), data.size() - maximumCommentSize);
    qsizetype eocd = -1;
    for (qsizetype i = data.size() - 22; i >= searchFrom; --i) {
        if (le32(data.constData() + i) == endOfCentralDirectorySignature) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return false;
    const quint16 entryCount = le16(data.constData() + eocd + 10);
    const quint32 directorySize = le32(data.constData() + eocd + 12);
    const quint32 directoryOffset = le32(data.constData() + eocd + 16);
    if (qint64(directoryOffset) + directorySize > data.size())
        return false;
    qsizetype at = directoryOffset;
    items.clear();
    items.reserve(entryCount);
    for (quint16 i = 0; i < entryCount; ++i) {
        if (at + 46 > data.size() || le32(data.constData() + at) != centralDirectorySignature)
            return false;
        Entry entry;
        entry.method = le16(data.constData() + at + 10);
        entry.compressedSize = le32(data.constData() + at + 20);
        entry.uncompressedSize = le32(data.constData() + at + 24);
        const quint16 nameLength = le16(data.constData() + at + 28);
        const quint16 extraLength = le16(data.constData() + at + 30);
        const quint16 commentLength = le16(data.constData() + at + 32);
        entry.localHeaderOffset = le32(data.constData() + at + 42);
        if (at + 46 + nameLength > data.size())
            return false;
        entry.name = QString::fromUtf8(data.constData() + at + 46, nameLength);
        items.push_back(entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

QByteArray ZipReader::read(const QString &name) const
{
    if (!valid)
        return {};
    const Entry *found = nullptr;
    for (const Entry &entry : items) {
        if (entry.name == name) {
            found = &entry;
            break;
        }
    }
    if (!found)
        return {};
    const qsizetype header = found->localHeaderOffset;
    if (header + 30 > data.size() || le32(data.constData() + header) != localFileHeaderSignature)
        return {};
    const quint16 nameLength = le16(data.constData() + header + 26);
    const quint16 extraLength = le16(data.constData() + header + 28);
    const qsizetype dataStart = header + 30 + nameLength + extraLength;
    if (dataStart + found->compressedSize > data.size())
        return {};
    const char *compressed = data.constData() + dataStart;
    if (found->method == 0)
        return QByteArray(compressed, int(found->compressedSize));
    if (found->method == 8) {
#ifdef OMASTRATOR_HAVE_ZLIB
        return inflateRaw(compressed, found->compressedSize, found->uncompressedSize);
#else
        qCWarning(lcIO) << "zip entry" << name << "needs deflate, but this build has no zlib";
        return {};
#endif
    }
    qCWarning(lcIO) << "zip entry" << name << "uses unsupported compression method" << found->method;
    return {};
}
