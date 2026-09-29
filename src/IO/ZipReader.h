#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <vector>

// A zip archive read from memory: the container for .sketch, .penpot and
// .fig. The central directory, then stored or deflated entries inflated
// with zlib. Nothing else (no encryption, no zip64, no multi-disk archives).
class ZipReader {
public:
    // Entries that claim to inflate past this are refused rather than allocated.
    static constexpr quint32 maximumEntrySize = 256u * 1024 * 1024;
    // Everything one reader hands out, across all its read() calls.
    static constexpr qint64 maximumTotalSize = 1024ll * 1024 * 1024;

    explicit ZipReader(const QByteArray &archive);
    bool isValid() const;
    // File entries in archive order, with backslashes turned into slashes.
    // Directory entries are left out.
    QStringList entries() const;
    QByteArray read(const QString &name) const;

private:
    struct Entry {
        QString name;
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        // Offset of the local file header, within `data`.
        quint32 localHeaderOffset = 0;
    };
    QByteArray data;
    std::vector<Entry> items;
    QHash<QString, int> index;
    mutable qint64 totalRead = 0;
    bool valid = false;

    bool readCentralDirectory();
};
