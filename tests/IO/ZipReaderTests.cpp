#include "FigmaKiwiFixtures.h"
#include "IO/ZipReader.h"
#include <QTest>

class ZipReaderTests : public QObject {
    Q_OBJECT

private slots:
    void readsAStoredEntry()
    {
        const ZipReader zip(buildZipArchive({{QStringLiteral("hello.txt"), "Hello, world!", false}}));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.entries(), QStringList{QStringLiteral("hello.txt")});
        QCOMPARE(zip.read(QStringLiteral("hello.txt")), QByteArray("Hello, world!"));
    }

    void readsADeflatedEntry()
    {
        const QByteArray original = QByteArray("The quick brown fox jumps over the lazy dog. ").repeated(20);
        const ZipReader zip(buildZipArchive({{QStringLiteral("big.txt"), original, true}}));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.read(QStringLiteral("big.txt")), original);
    }

    void readsSeveralEntriesByName()
    {
        const ZipReader zip(buildZipArchive({{QStringLiteral("canvas.fig"), "kiwi-bytes", false}, {QStringLiteral("images/abcd.png"), "png-bytes", true}}));
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
