#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>

// A zip archive read from memory: the central directory, then stored or
// deflated entries inflated with zlib. Nothing else (no encryption, no
// zip64, no multi-disk archives).
class ZipReader {
public:
    explicit ZipReader(const QByteArray &archive);
    bool isValid() const;
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
    bool valid = false;

    bool readCentralDirectory();
};
