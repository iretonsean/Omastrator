#include "IO/ImageImporter.h"
#include <QTemporaryDir>
#include <QTest>

namespace {

void appendBE16(QByteArray &out, quint16 value)
{
    out.append(char(value >> 8));
    out.append(char(value & 0xff));
}

void appendBE32(QByteArray &out, quint32 value)
{
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

// A minimal raw (uncompressed) 8-bit RGB or RGBA PSD: header, empty
// color-mode/resources/layer sections, then planar composite pixel data.
QByteArray buildPsd(int width, int height, const QList<QByteArray> &planes)
{
    QByteArray out;
    out.append("8BPS");
    appendBE16(out, 1);
    out.append(QByteArray(6, '\0'));
    appendBE16(out, quint16(planes.size()));
    appendBE32(out, quint32(height));
    appendBE32(out, quint32(width));
    appendBE16(out, 8);
    appendBE16(out, 3); // RGB
    appendBE32(out, 0); // color mode data
    appendBE32(out, 0); // image resources
    appendBE32(out, 0); // layer and mask info
    appendBE16(out, 0); // raw composite
    for (const QByteArray &plane : planes)
        out.append(plane);
    return out;
}

}

class ImageImporterTests : public QObject {
    Q_OBJECT

private slots:
    void readsAPng()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("dot.png"));
        QImage source(3, 2, QImage::Format_RGB32);
        source.fill(QColor(10, 200, 30));
        QVERIFY(source.save(path));
        const QImage image = ImageImporter::read(path);
        QCOMPARE(image.size(), QSize(3, 2));
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(image.pixelColor(1, 1), QColor(10, 200, 30));
    }

    void failuresAreFileErrors()
    {
        QTemporaryDir dir;
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(dir.filePath(QStringLiteral("missing.png"))));
        const QString junk = dir.filePath(QStringLiteral("junk.png"));
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not an image");
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(junk));
    }

    void svgIsVector()
    {
        QVERIFY(ImageImporter::isVector(QStringLiteral("/x/Logo.SVG")));
        QVERIFY(!ImageImporter::isVector(QStringLiteral("/x/photo.png")));
        const QStringList filters = ImageImporter::nameFilters();
        QVERIFY(filters.front().contains(QStringLiteral("*.svg")));
        QVERIFY(filters.front().contains(QStringLiteral("*.png")));
        QVERIFY(filters.front().contains(QStringLiteral("*.jpg")));
    }

    void psdCompositeIsPlacedFlattened()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("art.psd"));
        // 2x1: pixel 0 red, pixel 1 green, each channel a full plane.
        const QByteArray red("\xff\x00", 2), green("\x00\xff", 2), blue("\x00\x00", 2);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(buildPsd(2, 1, {red, green, blue}));
        file.close();

        QStringList warnings;
        const QImage image = ImageImporter::read(path, &warnings);
        QCOMPARE(image.size(), QSize(2, 1));
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(1, 0), QColor(0, 255, 0));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.front().contains(QStringLiteral("flattened")));

        const QStringList filters = ImageImporter::nameFilters();
        QVERIFY(filters.front().contains(QStringLiteral("*.psd")));
    }

    void psdWithAlphaChannel()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("alpha.psd"));
        const QByteArray red("\xff", 1), green("\x00", 1), blue("\x00", 1), alpha("\x80", 1);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(buildPsd(1, 1, {red, green, blue, alpha}));
        file.close();
        const QImage image = ImageImporter::read(path);
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0x80);
    }

    void notAPsdOrHeicFallsThroughToQImageReader()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("dot.png"));
        QImage source(2, 2, QImage::Format_RGB32);
        source.fill(Qt::blue);
        QVERIFY(source.save(path));
        const QImage image = ImageImporter::read(path);
        QCOMPARE(image.size(), QSize(2, 2));
    }

    void psdRleCompositeDecodes()
    {
        // PackBits: byte n=2 means "copy the next 3 bytes literally".
        QByteArray red, green, blue;
        red.append(char(2));
        red.append("\xff\x80\x40", 3);
        green.append(char(2));
        green.append("\x00\x00\x00", 3);
        blue.append(char(2));
        blue.append("\x00\x00\x00", 3);

        QByteArray out;
        out.append("8BPS");
        appendBE16(out, 1);
        out.append(QByteArray(6, '\0'));
        appendBE16(out, 3);
        appendBE32(out, 1); // height
        appendBE32(out, 3); // width
        appendBE16(out, 8);
        appendBE16(out, 3);
        appendBE32(out, 0);
        appendBE32(out, 0);
        appendBE32(out, 0);
        appendBE16(out, 1); // RLE
        for (const QByteArray &plane : {red, green, blue})
            appendBE16(out, quint16(plane.size())); // byte-count table, one row per channel
        for (const QByteArray &plane : {red, green, blue})
            out.append(plane);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("rle.psd"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(out);
        file.close();
        const QImage image = ImageImporter::read(path);
        QCOMPARE(image.size(), QSize(3, 1));
        QCOMPARE(image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(1, 0), QColor(0x80, 0, 0));
        QCOMPARE(image.pixelColor(2, 0), QColor(0x40, 0, 0));
    }

    void indexedPsdIsRefused()
    {
        QByteArray out;
        out.append("8BPS");
        appendBE16(out, 1);
        out.append(QByteArray(6, '\0'));
        appendBE16(out, 1);
        appendBE32(out, 1);
        appendBE32(out, 1);
        appendBE16(out, 8);
        appendBE16(out, 2); // indexed
        appendBE32(out, 0);
        appendBE32(out, 0);
        appendBE32(out, 0);
        appendBE16(out, 0);
        out.append(char(0));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("indexed.psd"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(out);
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(path));
    }

    // A header that claims 3000 x 3000 and then ends must fail, not read past the buffer.
    void truncatedRawPsdIsAFileError()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("cut.psd"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(buildPsd(3000, 3000, {QByteArray(10, 'x')}));
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(path));
    }

    void hugePsdIsRefusedBeforeAllocating()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("huge.psd"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(buildPsd(30000, 30000, {QByteArray(1, 'x'), QByteArray(1, 'x'), QByteArray(1, 'x'), QByteArray(1, 'x')}));
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(path));
    }

    void heicMagicBytesAreDetected()
    {
        // A minimal ISO-BMFF ftyp box announcing the "heic" brand; no real
        // image inside, so this exercises detection and the decode-failure
        // path rather than a full decode (see docs/import/quick-wins.md).
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("photo.heic"));
        QByteArray data;
        appendBE32(data, 24);
        data.append("ftyp");
        data.append("heic");
        appendBE32(data, 0);
        data.append("heic");
        data.append("mif1");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QVERIFY_THROWS_EXCEPTION(FileError, ImageImporter::read(path));
    }
};

QTEST_MAIN(ImageImporterTests)
#include "ImageImporterTests.moc"
