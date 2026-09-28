#include "FigmaKiwiFixtures.h"
#include "IO/ZipReader.h"
#include <QTest>

namespace {
void appendU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
}

void appendU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xff));
    out.append(char((v >> 8) & 0xff));
    out.append(char((v >> 16) & 0xff));
    out.append(char((v >> 24) & 0xff));
}

struct Entry {
    QString name;
    QByteArray data;
    bool deflate = false;
};

// A minimal zip archive: stored or raw-deflated entries, no CRC (ZipReader
// doesn't check it), no zip64.
QByteArray buildZip(const std::vector<Entry> &entries)
{
    QByteArray data;
    struct Written {
        QString name;
        quint32 offset;
        quint32 compressedSize;
        quint32 uncompressedSize;
        quint16 method;
    };
    std::vector<Written> written;
    for (const Entry &entry : entries) {
        const QByteArray compressed = entry.deflate ? deflateRaw(entry.data) : entry.data;
        const quint16 method = entry.deflate ? 8 : 0;
        const quint32 offset = quint32(data.size());
        appendU32(data, 0x04034b50);
        appendU16(data, 20); // version needed
        appendU16(data, 0); // flags
        appendU16(data, method);
        appendU16(data, 0); // mod time
        appendU16(data, 0); // mod date
        appendU32(data, 0); // crc32 (unchecked by ZipReader)
        appendU32(data, quint32(compressed.size()));
        appendU32(data, quint32(entry.data.size()));
        const QByteArray name = entry.name.toUtf8();
        appendU16(data, quint16(name.size()));
        appendU16(data, 0); // extra length
        data += name;
        data += compressed;
        written.push_back({entry.name, offset, quint32(compressed.size()), quint32(entry.data.size()), method});
    }
    const quint32 centralStart = quint32(data.size());
    for (const Written &w : written) {
        appendU32(data, 0x02014b50);
        appendU16(data, 20); // version made by
        appendU16(data, 20); // version needed
        appendU16(data, 0); // flags
        appendU16(data, w.method);
        appendU16(data, 0);
        appendU16(data, 0);
        appendU32(data, 0); // crc32
        appendU32(data, w.compressedSize);
        appendU32(data, w.uncompressedSize);
        const QByteArray name = w.name.toUtf8();
        appendU16(data, quint16(name.size()));
        appendU16(data, 0); // extra
        appendU16(data, 0); // comment
        appendU16(data, 0); // disk start
        appendU16(data, 0); // internal attrs
        appendU32(data, 0); // external attrs
        appendU32(data, w.offset);
        data += name;
    }
    const quint32 centralSize = quint32(data.size()) - centralStart;
    appendU32(data, 0x06054b50);
    appendU16(data, 0);
    appendU16(data, 0);
    appendU16(data, quint16(written.size()));
    appendU16(data, quint16(written.size()));
    appendU32(data, centralSize);
    appendU32(data, centralStart);
    appendU16(data, 0); // comment length
    return data;
}
}

class ZipReaderTests : public QObject {
    Q_OBJECT

private slots:
    void readsAStoredEntry()
    {
        const ZipReader zip(buildZip({{QStringLiteral("hello.txt"), "Hello, world!", false}}));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.entries(), QStringList{QStringLiteral("hello.txt")});
        QCOMPARE(zip.read(QStringLiteral("hello.txt")), QByteArray("Hello, world!"));
    }

    void readsADeflatedEntry()
    {
        const QByteArray original = QByteArray("The quick brown fox jumps over the lazy dog. ").repeated(20);
        const ZipReader zip(buildZip({{QStringLiteral("big.txt"), original, true}}));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.read(QStringLiteral("big.txt")), original);
    }

    void readsSeveralEntriesByName()
    {
        const ZipReader zip(buildZip({{QStringLiteral("canvas.fig"), "kiwi-bytes", false}, {QStringLiteral("images/abcd.png"), "png-bytes", true}}));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.entries().size(), 2);
        QCOMPARE(zip.read(QStringLiteral("canvas.fig")), QByteArray("kiwi-bytes"));
        QCOMPARE(zip.read(QStringLiteral("images/abcd.png")), QByteArray("png-bytes"));
        QVERIFY(zip.read(QStringLiteral("missing")).isEmpty());
    }

    void rejectsGarbage()
    {
        const ZipReader zip(QByteArray("not a zip file at all"));
        QVERIFY(!zip.isValid());
    }
};

QTEST_MAIN(ZipReaderTests)
#include "ZipReaderTests.moc"
