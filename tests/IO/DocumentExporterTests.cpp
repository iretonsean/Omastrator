#include "Document/PathOperations.h"
#include "IO/DocumentExporter.h"
#include <QFile>
#include <QImageReader>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

class DocumentExporterTests : public QObject {
    Q_OBJECT

private:
    // A 200 × 100 artboard, its left half red.
    static VectorDocument sample()
    {
        VectorDocument document = VectorDocument::blank({200, 100});
        document.background = QColor(0, 0, 255);
        VectorObject rect;
        rect.path = Shapes::rectangle({0, 0, 100, 100});
        rect.fill = Paint::solid(Qt::red);
        rect.stroke.paint = Paint::none();
        document.insert(rect, document.layers().front());
        return document;
    }

    static bool close(QColor a, QColor b, int tolerance = 4)
    {
        return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance
               && std::abs(a.blue() - b.blue()) <= tolerance && std::abs(a.alpha() - b.alpha()) <= tolerance;
    }

private slots:
    void formatComesFromTheExtension()
    {
        QCOMPARE(DocumentExporter::format(QStringLiteral("/a/b.pdf")), DocumentExporter::Format::pdf);
        QCOMPARE(DocumentExporter::format(QStringLiteral("x.PNG")), DocumentExporter::Format::png);
        QCOMPARE(DocumentExporter::format(QStringLiteral("x.jpg")), DocumentExporter::Format::jpeg);
        QCOMPARE(DocumentExporter::format(QStringLiteral("x.jpeg")), DocumentExporter::Format::jpeg);
        QCOMPARE(DocumentExporter::format(QStringLiteral("x.svg")), DocumentExporter::Format::svg);
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::format(QStringLiteral("x.gif")));
    }

    void pdfIsWrittenAsVectors()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.pdf"));
        DocumentExporter::writePdf(sample(), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF"));
        // The page is the artboard in points, and nothing was rasterized.
        const QRegularExpression mediaBox(QStringLiteral(R"(/MediaBox \[0 0 200(\.0+)? 100(\.0+)?\])"));
        QVERIFY(mediaBox.match(QString::fromLatin1(bytes)).hasMatch());
        QVERIFY(!bytes.contains("/Subtype /Image"));
    }

    void pngHasTheArtboardsPixels()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.png"));
        DocumentExporter::writePng(sample(), path, 2);
        const QImage image(path);
        QCOMPARE(image.size(), QSize(400, 200));
        QVERIFY(close(image.pixelColor(50, 100), Qt::red));
        QVERIFY(close(image.pixelColor(350, 100), QColor(0, 0, 255)));
    }

    void transparentPngLeavesOutThePaper()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("clear.png"));
        DocumentExporter::writePng(sample(), path, 1, true);
        const QImage image(path);
        QCOMPARE(image.size(), QSize(200, 100));
        QCOMPARE(image.pixelColor(150, 50).alpha(), 0);
        QVERIFY(close(image.pixelColor(50, 50), Qt::red));
    }

    void jpegIsFlattenedOntoWhite()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.jpg"));
        VectorDocument document = sample();
        document.background = Qt::transparent;
        DocumentExporter::writeJpeg(document, path, 1, 95);
        QImageReader reader(path);
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        const QImage image = reader.read();
        QCOMPARE(image.size(), QSize(200, 100));
        QVERIFY(close(image.pixelColor(50, 50), Qt::red, 12));
        QVERIFY(close(image.pixelColor(150, 50), Qt::white, 12));
    }

    void jpegQualityChangesTheSize()
    {
        QTemporaryDir dir;
        VectorDocument document = sample();
        VectorObject ellipse;
        ellipse.path = Shapes::ellipse({20, 10, 160, 80});
        ellipse.fill = Paint::radial(Qt::yellow, Qt::darkGreen);
        document.insert(ellipse, document.layers().front());
        DocumentExporter::writeJpeg(document, dir.filePath(QStringLiteral("low.jpg")), 2, 10);
        DocumentExporter::writeJpeg(document, dir.filePath(QStringLiteral("high.jpg")), 2, 100);
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("low.jpg"))).size() < QFileInfo(dir.filePath(QStringLiteral("high.jpg"))).size());
    }

    void hugeScaleIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePng(sample(), dir.filePath(QStringLiteral("big.png")), 1000));
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePng(sample(), dir.filePath(QStringLiteral("zero.png")), 0));
    }

    void unwritablePathIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePdf(sample(), QStringLiteral("/nonexistent-folder/out.pdf")));
    }
};

QTEST_MAIN(DocumentExporterTests)
#include "DocumentExporterTests.moc"
