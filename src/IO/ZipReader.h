#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

// Reads a zip archive held entirely in memory: the container for .sketch,
// .penpot and .fig. Supports stored and deflate entries through zlib only.
class ZipReader {
public:
    explicit ZipReader(const QByteArray &archive);

    bool isValid() const { return m_valid; }
    QStringList entries() const { return m_entries.keys(); }
    QByteArray read(const QString &name) const;

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 uncompressedSize = 0;
        quint32 localHeaderOffset = 0;
    };

    void parseCentralDirectory();

    QByteArray m_archive;
    QMap<QString, Entry> m_entries;
    bool m_valid = false;
};
