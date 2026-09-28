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

// Tools that pack on Windows sometimes name entries with backslashes.
QString normalizeName(QString name)
{
    return name.replace(QLatin1Char('\\'), QLatin1Char('/'));
}

#ifdef OMASTRATOR_HAVE_ZLIB
QByteArray inflateRaw(const char *src, quint32 compressedSize, quint32 uncompressedSize)
{
    if (uncompressedSize == 0)
        return {};
    QByteArray out(qsizetype(uncompressedSize), Qt::Uninitialized);
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
    out.truncate(qsizetype(stream.total_out));
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
    index.clear();
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
        entry.name = normalizeName(QString::fromUtf8(data.constData() + at + 46, nameLength));
        if (!entry.name.isEmpty() && !entry.name.endsWith(QLatin1Char('/'))) {
            // The first entry of a name wins, as the old linear scan did.
            if (!index.contains(entry.name))
                index.insert(entry.name, int(items.size()));
            items.push_back(entry);
        }
        at += 46 + nameLength + extraLength + commentLength;
    }
    return true;
}

QByteArray ZipReader::read(const QString &name) const
{
    if (!valid)
        return {};
    const auto hit = index.constFind(normalizeName(name));
    if (hit == index.constEnd())
        return {};
    const Entry *found = &items[size_t(hit.value())];
    const qsizetype header = found->localHeaderOffset;
    if (header + 30 > data.size() || le32(data.constData() + header) != localFileHeaderSignature)
        return {};
    const quint16 nameLength = le16(data.constData() + header + 26);
    const quint16 extraLength = le16(data.constData() + header + 28);
    const qsizetype dataStart = header + 30 + nameLength + extraLength;
    if (dataStart + found->compressedSize > data.size())
        return {};
    const quint32 claimed = found->uncompressedSize;
    if (found->method == 0 && found->uncompressedSize != found->compressedSize) {
        qCWarning(lcIO) << "zip entry" << name << "is stored but its sizes disagree; refusing it";
        return {};
    }
    if (claimed > maximumEntrySize || found->compressedSize > maximumEntrySize) {
        qCWarning(lcIO) << "zip entry" << name << "claims" << claimed << "bytes; refusing it";
        return {};
    }
    // Deflate can't expand past about 1032:1, so a bigger claim is a lie or a bomb.
    if (found->method == 8 && qint64(found->uncompressedSize) > qint64(found->compressedSize) * 1032 + 1024) {
        qCWarning(lcIO) << "zip entry" << name << "claims an impossible deflate ratio; refusing it";
        return {};
    }
    if (totalRead + qint64(claimed) > maximumTotalSize) {
        qCWarning(lcIO) << "zip entry" << name << "would take this archive past its total size limit; refusing it";
        return {};
    }
    const char *compressed = data.constData() + dataStart;
    if (found->method == 0) {
        totalRead += claimed;
        return QByteArray(compressed, qsizetype(found->compressedSize));
    }
    if (found->method == 8) {
#ifdef OMASTRATOR_HAVE_ZLIB
        const QByteArray out = inflateRaw(compressed, found->compressedSize, found->uncompressedSize);
        totalRead += out.size();
        return out;
#else
        qCWarning(lcIO) << "zip entry" << name << "needs deflate, but this build has no zlib";
        return {};
#endif
    }
    qCWarning(lcIO) << "zip entry" << name << "uses unsupported compression method" << found->method;
    return {};
}
