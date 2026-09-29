#include "Document/EditorSession.h"
#include "Document/PathOperations.h"
#include "IO/DocumentExporter.h"
#include "IO/PdfImporter.h"
#include "IO/SvgExporter.h"
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

    void pdfHasOnePagePerArtboardAcrossPages()
    {
        EditorSession session;
        session.createDocument({200, 100});
        session.addArtboard(QRectF(300, 0, 120, 80));
        session.addPage(QStringLiteral("Second"));
        session.renameArtboard(0, QStringLiteral("Wide"));
        session.addArtboard(QRectF(300, 0, 50, 60));
        session.setCurrentPage(session.document()->allPages()[0].id);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("all.pdf"));
        DocumentExporter::writePdf(*session.document(), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromLatin1(file.readAll());
        // Four artboards, four pages, sized page 1's two then page 2's two, whichever page is current.
        QRegularExpression box(QStringLiteral(R"(/MediaBox \[0 0 (\d+)(?:\.0+)? (\d+)(?:\.0+)?\])"));
        QStringList sizes;
        for (auto it = box.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            sizes << match.captured(1) + QLatin1Char('x') + match.captured(2);
        }
        QCOMPARE(sizes, (QStringList{"200x100", "120x80", "200x100", "50x60"}));
    }

    void aFlaggedArtboardOnPageTwoIsLeftOutOfTheAllPagesPdf()
    {
        EditorSession session;
        session.createDocument({200, 100});
        session.addArtboard(QRectF(300, 0, 120, 80));
        session.addPage(QStringLiteral("Second"));
        session.addArtboard(QRectF(300, 0, 50, 60));
        session.setArtboardExported(1, false);
        // Page 1 is the current page, so the flag is on a page the export isn't showing.
        session.setCurrentPage(session.document()->allPages()[0].id);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("all.pdf"));
        DocumentExporter::writePdf(*session.document(), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromLatin1(file.readAll());
        QRegularExpression box(QStringLiteral(R"(/MediaBox \[0 0 (\d+)(?:\.0+)? (\d+)(?:\.0+)?\])"));
        QStringList sizes;
        for (auto it = box.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            sizes << match.captured(1) + QLatin1Char('x') + match.captured(2);
        }
        QCOMPARE(sizes, (QStringList{"200x100", "120x80", "200x100"}));
    }

    void aPageWithNothingToExportIsSkippedAndAllPagesOffIsAnError()
    {
        EditorSession session;
        session.createDocument({200, 100});
        session.addPage(QStringLiteral("Second"));
        session.setArtboardExported(0, false);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("one.pdf"));
        DocumentExporter::writePdf(*session.document(), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromLatin1(file.readAll()).count(QStringLiteral("/MediaBox")), 1);
        session.setCurrentPage(session.document()->allPages()[0].id);
        session.setArtboardExported(0, false);
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePdf(*session.document(), dir.filePath(QStringLiteral("none.pdf"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("none.pdf"))));
    }

    void pngSkipsAFlaggedFirstArtboardOnTheCurrentPageOnly()
    {
        EditorSession session;
        session.createDocument({200, 100});
        session.setArtboardExported(0, false);
        session.addArtboard(QRectF(300, 0, 80, 40));
        session.addPage(QStringLiteral("Second"));
        session.setArtboardSize(QSizeF(60, 30));
        session.setCurrentPage(session.document()->allPages()[0].id);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        DocumentExporter::writePng(*session.document(), path);
        QCOMPARE(QImageReader(path).size(), QSize(80, 40));
    }

    void pngExportsTheCurrentPagesFirstArtboard()
    {
        EditorSession session;
        session.createDocument({200, 100});
        session.addPage(QStringLiteral("Second"));
        session.renameArtboard(0, QStringLiteral("Wide"));
        session.setArtboardSize(QSizeF(80, 40));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        DocumentExporter::writePng(*session.document(), path);
        QCOMPARE(QImageReader(path).size(), QSize(80, 40));
    }

    void typeExportsAsItDraws()
    {
        // Area type, justified and underlined, with a baseline shift: PDF and PNG draw what the canvas does.
        VectorDocument document = VectorDocument::blank({300, 200});
        VectorObject text;
        text.kind = ObjectKind::text;
        text.text.text = QStringLiteral("Wrapped justified words across a narrow box");
        text.text.size = 18;
        text.text.area = QSizeF(120, 0);
        text.text.alignment = TextAlignment::justifyAll;
        text.text.underline = true;
        text.fill = Paint::solid(Qt::black);
        text.stroke.paint = Paint::none();
        text.transform = QTransform::fromTranslate(20, 20);
        document.insert(text, document.layers().front());
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("type.pdf"));
        DocumentExporter::writePdf(document, pdf);
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("%PDF"));
        const QString png = dir.filePath(QStringLiteral("type.png"));
        DocumentExporter::writePng(document, png, 1);
        const QImage image(png);
        const QRectF box = document.bounds(text.id);
        QCOMPARE(box.width(), 120.0);
        // Ink inside the box, none to its right.
        int inside = 0, outside = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qGray(image.pixel(x, y)) > 128)
                    continue;
                (box.adjusted(-2, -2, 2, 2).contains(QPointF(x, y)) ? inside : outside) += 1;
            }
        }
        QVERIFY(inside > 200);
        QCOMPARE(outside, 0);
        // The justified lines reach the box's right edge.
        bool reaches = false;
        for (int y = int(box.top()); y < int(box.bottom()); ++y)
            reaches = reaches || qGray(image.pixel(int(box.right()) - 2, y)) < 128;
        QVERIFY(reaches);
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

    void opacityMaskFadesInPngAndSurvivesPdf()
    {
        VectorDocument document = VectorDocument::blank({100, 20});
        VectorObject art;
        art.path = Shapes::rectangle({0, 0, 100, 20});
        art.fill = Paint::solid(Qt::red);
        art.stroke.paint = Paint::none();
        document.insert(art, document.layers().front());
        VectorObject fade;
        fade.path = Shapes::rectangle({0, 0, 100, 20});
        fade.fill = Paint::linear(Qt::white, Qt::black);
        fade.stroke.paint = Paint::none();
        document.insert(fade, document.layers().front());
        VectorObject group;
        group.kind = ObjectKind::group;
        group.mask = OpacityMask();
        document.insert(group, document.layers().front());
        document.move(art.id, group.id, -1);
        document.move(fade.id, group.id, -1);

        QTemporaryDir dir;
        const QString png = dir.filePath(QStringLiteral("mask.png"));
        DocumentExporter::writePng(document, png, 1);
        const QImage image(png);
        QVERIFY(close(image.pixelColor(2, 10), Qt::red, 20));
        QVERIFY(qGray(image.pixelColor(98, 10).rgb()) > qGray(image.pixelColor(2, 10).rgb()));

        const QString pdf = dir.filePath(QStringLiteral("mask.pdf"));
        DocumentExporter::writePdf(document, pdf);
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF"));
        // No live soft mask in Qt's PDF writer: the masked patch rasterizes instead.
        QVERIFY(bytes.contains("/Subtype /Image"));
    }

    void hugeScaleIsRefused()
    {
        QTemporaryDir dir;
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePng(sample(), dir.filePath(QStringLiteral("big.png")), 1000));
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePng(sample(), dir.filePath(QStringLiteral("zero.png")), 0));
    }

    // Two artboards side by side, red 200 × 100 and blue 150 × 300, each named by its colour.
    static VectorDocument twoBoards(bool firstExports, bool secondExports)
    {
        VectorDocument document = VectorDocument::blank({200, 100});
        for (const auto &[x, color] : {std::pair{0.0, QColor(Qt::red)}, std::pair{300.0, QColor(Qt::blue)}}) {
            VectorObject rect;
            rect.path = Shapes::rectangle({x, 0, 200, 100});
            rect.fill = Paint::solid(color);
            rect.stroke.paint = Paint::none();
            document.insert(rect, document.layers().front());
        }
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("Red"), QRectF(0, 0, 200, 100), Qt::white, QUuid(), firstExports},
                               {QUuid::createUuid(), QStringLiteral("Blue"), QRectF(300, 0, 150, 300), Qt::white, QUuid(), secondExports}});
        return document;
    }

    void anUnexportedArtboardIsLeftOutSoTheNextOneExports()
    {
        QTemporaryDir dir;
        const VectorDocument document = twoBoards(false, true);
        const QString png = dir.filePath(QStringLiteral("out.png"));
        DocumentExporter::writePng(document, png);
        QVERIFY(close(QImage(png).pixelColor(50, 50), Qt::blue));
        const QString svg = dir.filePath(QStringLiteral("out.svg"));
        SvgExporter::write(document, svg);
        QFile file(svg);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.contains("#0000ff"));
        QVERIFY(!text.contains("#ff0000"));
        const QString pdf = dir.filePath(QStringLiteral("out.pdf"));
        DocumentExporter::writePdf(document, pdf);
        // The page is the blue board's size, not the first board's.
        const VectorDocument page = PdfImporter::read(pdf);
        QVERIFY(qAbs(page.size.width() - 150) < 1 && qAbs(page.size.height() - 300) < 1);
    }

    void withEveryArtboardUnexportedNothingIsWrittenAndTheErrorSaysWhy()
    {
        QTemporaryDir dir;
        const VectorDocument document = twoBoards(false, false);
        const auto missing = [&](const char *name) { return dir.filePath(QString::fromLatin1(name)); };
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePng(document, missing("a.png")));
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writeJpeg(document, missing("a.jpg")));
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePdf(document, missing("a.pdf")));
        QVERIFY_THROWS_EXCEPTION(FileError, SvgExporter::write(document, missing("a.svg")));
        for (const char *name : {"a.png", "a.jpg", "a.pdf", "a.svg"})
            QVERIFY(!QFileInfo::exists(missing(name)));
        try {
            DocumentExporter::exportedPage(document);
            QFAIL("expected a FileError");
        } catch (const FileError &error) {
            QVERIFY(error.message().contains(QLatin1String("set not to export")));
        }
    }

    void unwritablePathIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, DocumentExporter::writePdf(sample(), QStringLiteral("/nonexistent-folder/out.pdf")));
    }
};

QTEST_MAIN(DocumentExporterTests)
#include "DocumentExporterTests.moc"
