#include "IO/ZipReader.h"
#include <QTest>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {

void appendLE16(QByteArray &out, quint16 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
}

void appendLE32(QByteArray &out, quint32 value)
{
    out.append(static_cast<char>(value & 0xff));
    out.append(static_cast<char>((value >> 8) & 0xff));
    out.append(static_cast<char>((value >> 16) & 0xff));
    out.append(static_cast<char>((value >> 24) & 0xff));
}

#ifdef OMASTRATOR_HAVE_ZLIB
QByteArray rawDeflate(const QByteArray &input)
{
    QByteArray output;
    output.resize(input.size() + 64);
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    deflate(&stream, Z_FINISH);
    output.resize(static_cast<int>(stream.total_out));
    deflateEnd(&stream);
    return output;
}
#endif

struct ZipEntrySpec {
    QString name;
    QByteArray data;
    bool deflate = false;
};

// A minimal, hand-built zip: local headers plus data, then a central directory and EOCD record.
QByteArray buildZip(const QList<ZipEntrySpec> &specs)
{
    QByteArray out;
    struct Written {
        QString name;
        quint32 offset;
        quint32 crc;
        quint32 compressedSize;
        quint32 uncompressedSize;
        quint16 method;
    };
    QList<Written> written;

    for (const ZipEntrySpec &spec : specs) {
        const quint32 offset = static_cast<quint32>(out.size());
        const QByteArray nameUtf8 = spec.name.toUtf8();
        QByteArray payload = spec.data;
        quint16 method = 0;
#ifdef OMASTRATOR_HAVE_ZLIB
        if (spec.deflate) {
            payload = rawDeflate(spec.data);
            method = 8;
        }
#endif
        const quint32 crc = static_cast<quint32>(crc32(0, reinterpret_cast<const Bytef *>(spec.data.constData()),
                                                         static_cast<uInt>(spec.data.size())));

        appendLE32(out, 0x04034b50);
        appendLE16(out, 20);
        appendLE16(out, 0);
        appendLE16(out, method);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, crc);
        appendLE32(out, static_cast<quint32>(payload.size()));
        appendLE32(out, static_cast<quint32>(spec.data.size()));
        appendLE16(out, static_cast<quint16>(nameUtf8.size()));
        appendLE16(out, 0);
        out.append(nameUtf8);
        out.append(payload);

        written.append({spec.name, offset, crc, static_cast<quint32>(payload.size()),
                         static_cast<quint32>(spec.data.size()), method});
    }

    const quint32 centralDirectoryStart = static_cast<quint32>(out.size());
    for (const Written &entry : written) {
        const QByteArray nameUtf8 = entry.name.toUtf8();
        appendLE32(out, 0x02014b50);
        appendLE16(out, 20);
        appendLE16(out, 20);
        appendLE16(out, 0);
        appendLE16(out, entry.method);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, entry.crc);
        appendLE32(out, entry.compressedSize);
        appendLE32(out, entry.uncompressedSize);
        appendLE16(out, static_cast<quint16>(nameUtf8.size()));
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE16(out, 0);
        appendLE32(out, 0);
        appendLE32(out, entry.offset);
        out.append(nameUtf8);
    }
    const quint32 centralDirectorySize = static_cast<quint32>(out.size()) - centralDirectoryStart;

    appendLE32(out, 0x06054b50);
    appendLE16(out, 0);
    appendLE16(out, 0);
    appendLE16(out, static_cast<quint16>(written.size()));
    appendLE16(out, static_cast<quint16>(written.size()));
    appendLE32(out, centralDirectorySize);
    appendLE32(out, centralDirectoryStart);
    appendLE16(out, 0);
    return out;
}

}

class ZipReaderTests : public QObject {
    Q_OBJECT

private slots:
    void readsStoredEntries()
    {
        const QByteArray zip = buildZip({{QStringLiteral("document.json"), QByteArrayLiteral("{\"a\":1}"), false},
                                          {QStringLiteral("pages/page1.json"), QByteArrayLiteral("{\"b\":2}"), false}});
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.entries().size(), 2);
        QCOMPARE(reader.read(QStringLiteral("document.json")), QByteArrayLiteral("{\"a\":1}"));
        QCOMPARE(reader.read(QStringLiteral("pages/page1.json")), QByteArrayLiteral("{\"b\":2}"));
        QVERIFY(reader.read(QStringLiteral("missing.json")).isEmpty());
    }

#ifdef OMASTRATOR_HAVE_ZLIB
    void readsDeflatedEntries()
    {
        const QByteArray longText = QByteArray("hello world, ").repeated(50);
        const QByteArray zip = buildZip({{QStringLiteral("document.json"), longText, true}});
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.read(QStringLiteral("document.json")), longText);
    }
#endif

    void backslashesAreNormalized()
    {
        const QByteArray zip = buildZip({{QStringLiteral("pages\\page1.json"), QByteArrayLiteral("x"), false}});
        ZipReader reader(zip);
        QCOMPARE(reader.read(QStringLiteral("pages/page1.json")), QByteArrayLiteral("x"));
    }

    void listsEntriesInArchiveOrderWithoutDirectories()
    {
        const QByteArray zip = buildZip({{QStringLiteral("canvas.fig"), QByteArrayLiteral("kiwi-bytes"), false},
                                          {QStringLiteral("images/"), QByteArray(), false},
                                          {QStringLiteral("images/abcd.png"), QByteArrayLiteral("png-bytes"), false}});
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.entries(), (QStringList{QStringLiteral("canvas.fig"), QStringLiteral("images/abcd.png")}));
    }

    // A hostile archive can claim any uncompressed size; it must not become an allocation.
    void refusesAnEntryThatClaimsToBeHuge()
    {
        QByteArray zip = buildZip({{QStringLiteral("document.json"), QByteArrayLiteral("{}"), false}});
        const qsizetype central = zip.indexOf(QByteArrayLiteral("PK\x01\x02"));
        QVERIFY(central > 0);
        // The central directory's uncompressed size sits 24 bytes in.
        for (int i = 0; i < 4; ++i)
            zip[central + 24 + i] = char(0xff);
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QVERIFY(reader.read(QStringLiteral("document.json")).isEmpty());
    }

#ifdef OMASTRATOR_HAVE_ZLIB
    // The entry inflates fine and the claim is under every cap, so only the ratio check can refuse it.
    void refusesADeflateEntryWithAnImpossibleRatio()
    {
        QByteArray zip = buildZip({{QStringLiteral("a.bin"), QByteArray(100 * 1024, '\0'), true}});
        const qsizetype central = zip.indexOf(QByteArrayLiteral("PK\x01\x02"));
        QVERIFY(central > 0);
        const quint32 claim = 4u * 1024 * 1024;
        for (int i = 0; i < 4; ++i)
            zip[central + 24 + i] = char((claim >> (8 * i)) & 0xff);
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QVERIFY(reader.read(QStringLiteral("a.bin")).isEmpty());
    }
#endif

    void refusesAStoredEntryWhoseSizesDisagree()
    {
        QByteArray zip = buildZip({{QStringLiteral("a.bin"), QByteArray(64, 'x'), false}});
        const qsizetype central = zip.indexOf(QByteArrayLiteral("PK\x01\x02"));
        // 65 instead of 64: under every cap, wrong only against the compressed size.
        zip[central + 24] = char(65);
        ZipReader reader(zip);
        QVERIFY(reader.isValid());
        QVERIFY(reader.read(QStringLiteral("a.bin")).isEmpty());
    }

    void readsManyEntriesByName()
    {
        QList<ZipEntrySpec> specs;
        for (int i = 0; i < 300; ++i)
            specs.append({QStringLiteral("objects/%1.json").arg(i), QByteArray::number(i), false});
        ZipReader reader(buildZip(specs));
        QVERIFY(reader.isValid());
        QCOMPARE(reader.entries().size(), 300);
        QCOMPARE(reader.read(QStringLiteral("objects/299.json")), QByteArrayLiteral("299"));
        QCOMPARE(reader.read(QStringLiteral("objects/0.json")), QByteArrayLiteral("0"));
    }

#ifdef OMASTRATOR_HAVE_ZLIB
    // A reader stops handing out bytes once its total passes the cap, even when each entry is fine.
    void stopsAtTheTotalLimit()
    {
        const QByteArray big(64 * 1024 * 1024, '\0');
        ZipReader reader(buildZip({{QStringLiteral("zeros.bin"), big, true}}));
        QVERIFY(reader.isValid());
        int served = 0;
        while (served < 64 && !reader.read(QStringLiteral("zeros.bin")).isEmpty())
            ++served;
        QVERIFY(served >= 15);
        QVERIFY(served <= 16);
    }
#endif

    void garbageIsInvalid()
    {
        ZipReader reader(QByteArrayLiteral("not a zip file"));
        QVERIFY(!reader.isValid());
        QVERIFY(reader.entries().isEmpty());
    }
};

QTEST_MAIN(ZipReaderTests)
#include "ZipReaderTests.moc"
