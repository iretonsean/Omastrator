#include "IO/ImageImporter.h"
#include <QTemporaryDir>
#include <QTest>

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
};

QTEST_MAIN(ImageImporterTests)
#include "ImageImporterTests.moc"
