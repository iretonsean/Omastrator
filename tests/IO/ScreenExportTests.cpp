#include "Document/PathOperations.h"
#include "IO/ScreenExport.h"
#include <QDir>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

// Export for Screens (docs/QOL-RESEARCH.md P2-10): artboards and export assets, a batch of
// scales and formats, in one go.
class ScreenExportTests : public QObject {
    Q_OBJECT

private:
    static VectorObject rectangle(QRectF rect, const QString &name)
    {
        VectorObject object;
        object.name = name;
        object.path = Shapes::rectangle(rect);
        object.fill = Paint::solid(Qt::red);
        object.stroke.paint = Paint::none();
        return object;
    }

private slots:
    void scaleSuffixIsEmptyAtOneAndAtSignBelowAndAbove()
    {
        QCOMPARE(ScreenExport::scaleSuffix(1), QString());
        QCOMPARE(ScreenExport::scaleSuffix(2), QStringLiteral("@2x"));
        QCOMPARE(ScreenExport::scaleSuffix(0.5), QStringLiteral("@0.5x"));
        QCOMPARE(ScreenExport::scaleSuffix(3), QStringLiteral("@3x"));
    }

    void anUnexportedArtboardIsSkippedEvenWhenAskedFor()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.insert(rectangle({10, 10, 20, 20}, QStringLiteral("Art")), document.layers().front());
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("Phone"), QRectF(0, 0, 100, 100), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("Scratch"), QRectF(150, 0, 100, 100), Qt::white, false}});
        QTemporaryDir dir;
        ScreenExport::Settings settings;
        settings.folder = dir.path();
        settings.formats = {QStringLiteral("png"), QStringLiteral("svg"), QStringLiteral("pdf")};
        const QStringList written = ScreenExport::run(document, {0, 1}, {}, settings);
        QCOMPARE(written.size(), 3);
        for (const QString &path : written)
            QVERIFY(QFileInfo(path).fileName().startsWith(QLatin1String("Phone")));
        QVERIFY(!QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("Scratch.png"))));
    }

    void twoArtboardsExportToTwoPngsNamedAfterThem()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.insert(rectangle({10, 10, 20, 20}, QStringLiteral("Art")), document.layers().front());
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("Phone"), QRectF(0, 0, 100, 100), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("Desktop"), QRectF(150, 0, 100, 100), Qt::white}});
        QTemporaryDir dir;
        ScreenExport::Settings settings;
        settings.folder = dir.path();
        const QStringList written = ScreenExport::run(document, {0, 1}, {}, settings);
        QCOMPARE(written.size(), 2);
        QVERIFY(QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("Phone.png"))));
        QVERIFY(QFileInfo::exists(QDir(dir.path()).filePath(QStringLiteral("Desktop.png"))));
    }

    void twoAssetsAtOneAndTwoScalesGiveFourFilesWithAtSignTwoX()
    {
        VectorDocument document = VectorDocument::blank({200, 200});
        const QUuid layer = document.layers().front();
        VectorObject first = rectangle({0, 0, 20, 20}, QStringLiteral("Icon"));
        VectorObject second = rectangle({50, 50, 30, 30}, QStringLiteral("Logo"));
        document.insert(first, layer);
        document.insert(second, layer);
        document.exportAssets = {first.id, second.id};
        QTemporaryDir dir;
        ScreenExport::Settings settings;
        settings.scales = {1, 2};
        settings.formats = {QStringLiteral("png")};
        settings.folder = dir.path();
        const QStringList written = ScreenExport::run(document, {}, {first.id, second.id}, settings);
        QCOMPARE(written.size(), 4);
        for (const QString &name : {QStringLiteral("Icon.png"), QStringLiteral("Icon@2x.png"), QStringLiteral("Logo.png"), QStringLiteral("Logo@2x.png")})
            QVERIFY2(QFileInfo::exists(QDir(dir.path()).filePath(name)), qPrintable(name));
        // The @2x file is twice the pixel size of the plain one.
        QImageReader plain(QDir(dir.path()).filePath(QStringLiteral("Icon.png")));
        QImageReader doubled(QDir(dir.path()).filePath(QStringLiteral("Icon@2x.png")));
        QCOMPARE(doubled.size().width(), plain.size().width() * 2);
    }

    void vectorFormatsWriteOnceAtOneScaleRegardlessOfTheScaleList()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.insert(rectangle({0, 0, 20, 20}, QStringLiteral("Art")), document.layers().front());
        QTemporaryDir dir;
        ScreenExport::Settings settings;
        settings.scales = {1, 2, 3};
        settings.formats = {QStringLiteral("svg")};
        settings.folder = dir.path();
        const QStringList written = ScreenExport::run(document, {0}, {}, settings);
        QCOMPARE(written.size(), 1);
        QCOMPARE(QFileInfo(written.front()).fileName(), QStringLiteral("Artboard 1.svg"));
    }

    void aBadFolderFailsThatFormatWithoutStoppingTheRest()
    {
        VectorDocument document = VectorDocument::blank({50, 50});
        document.insert(rectangle({0, 0, 10, 10}, QStringLiteral("Art")), document.layers().front());
        ScreenExport::Settings settings;
        settings.formats = {QStringLiteral("png"), QStringLiteral("svg")};
        // No writable folder at all: every write fails, but run() itself doesn't throw.
        settings.folder = QStringLiteral("/nonexistent/definitely/not/here");
        const QStringList written = ScreenExport::run(document, {0}, {}, settings);
        QVERIFY(written.isEmpty());
    }
};

QTEST_MAIN(ScreenExportTests)
#include "ScreenExportTests.moc"
