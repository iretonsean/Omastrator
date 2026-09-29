#include "../FontSupport.h"
#include "Document/EditorSession.h"
#include "IO/DocumentExporter.h"
#include "IO/PdfImporter.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include "Rendering/VectorRenderer.h"
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

// Inside and outside strokes on live type: canvas, PNG, SVG and PDF.
namespace {
constexpr double strokeWidth = 8;

VectorObject typeObject(const QString &words, StrokeAlignment alignment, bool area)
{
    VectorObject text;
    text.kind = ObjectKind::text;
    text.name = QStringLiteral("Type");
    text.text.text = words;
    text.text.size = 120;
    if (area)
        text.text.area = QSizeF(260, 150);
    // Point type sits on its baseline; area type hangs from its top.
    text.transform = QTransform::fromTranslate(20, area ? 20 : 150);
    text.fill = Paint::none();
    text.stroke.paint = Paint::solid(Qt::black);
    text.stroke.width = strokeWidth;
    text.stroke.alignment = alignment;
    return text;
}

VectorDocument typeDocument(const QString &words, StrokeAlignment alignment, bool area)
{
    VectorDocument document = VectorDocument::blank({300, 200});
    document.insert(typeObject(words, alignment, area), document.layers().front());
    return document;
}

QPainterPath glyphsOf(const QString &words, bool area)
{
    const VectorObject text = typeObject(words, StrokeAlignment::center, area);
    return text.transform.map(text.text.outline());
}

struct Tally {
    int painted = 0;
    int wrongSide = 0;
};

// Fully covered black pixels of `image` (a PDF page has a white background), counted against which side of the glyphs they sit on.
Tally tally(const QImage &image, const QPainterPath &glyphs, double scale, bool expectInside)
{
    Tally out;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) < 255 || qGray(pixel) > 4)
                continue;
            ++out.painted;
            const bool inside = glyphs.contains(QPointF((x + 0.5) / scale, (y + 0.5) / scale));
            if (inside != expectInside)
                ++out.wrongSide;
        }
    return out;
}
}

class TextStrokeAlignTests : public QObject {
    Q_OBJECT

private:
    // Fully opaque black pixels only ever land on the stroke's own side of the glyphs.
    static void checkSides(const QImage &image, const QPainterPath &glyphs, double scale, StrokeAlignment alignment, const char *label)
    {
        const Tally result = tally(image, glyphs, scale, alignment == StrokeAlignment::inside);
        QVERIFY2(result.painted > 500, label);
        QVERIFY2(result.wrongSide == 0, qPrintable(QStringLiteral("%1: %2 pixels on the wrong side").arg(QLatin1String(label)).arg(result.wrongSide)));
    }

    static QImage exportedPng(const VectorDocument &document, double scale)
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.png"));
        DocumentExporter::writePng(document, path, scale, true);
        return QImage(path).convertToFormat(QImage::Format_ARGB32);
    }

    static QImage viaSvg(const VectorDocument &document)
    {
        const QByteArray svg = SvgExporter::serialize(document);
        const VectorDocument back = SvgImporter::parse(svg);
        return VectorRenderer::render(back, 1, true).convertToFormat(QImage::Format_ARGB32);
    }

private slots:

    void initTestCase()
    {
        if (!haveInstalledFonts())
            QSKIP("No fonts installed (a bare container): there is no type to stroke");
    }

    void insideAndOutsideStayOnTheirSideOfPointAndAreaType_data()
    {
        QTest::addColumn<QString>("words");
        QTest::addColumn<bool>("area");
        QTest::addColumn<bool>("inside");
        for (bool area : {false, true})
            for (bool inside : {true, false}) {
                const QByteArray tag = QByteArray(area ? "area" : "point") + (inside ? " inside" : " outside");
                QTest::newRow((tag + " HOe").constData()) << QStringLiteral("HOe") << area << inside;
            }
    }

    void insideAndOutsideStayOnTheirSideOfPointAndAreaType()
    {
        QFETCH(QString, words);
        QFETCH(bool, area);
        QFETCH(bool, inside);
        const StrokeAlignment alignment = inside ? StrokeAlignment::inside : StrokeAlignment::outside;
        const VectorDocument document = typeDocument(words, alignment, area);
        const QPainterPath glyphs = glyphsOf(words, area);
        QVERIFY(!glyphs.isEmpty());

        // The canvas, and PNG at two scales.
        checkSides(VectorRenderer::render(document, 1, true), glyphs, 1, alignment, "canvas");
        checkSides(exportedPng(document, 1), glyphs, 1, alignment, "png1");
        checkSides(exportedPng(document, 2), glyphs, 2, alignment, "png2");
        // SVG goes out as a filled outline; reading it back draws the same.
        checkSides(viaSvg(document), glyphs, 1, alignment, "svg");
    }

    void aStemTakesTheFullWidthOnItsOwnSideOnly()
    {
        // "I" is a plain vertical bar in any sans face: probe across its left edge at mid height.
        for (bool area : {false, true}) {
            const QPainterPath glyphs = glyphsOf(QStringLiteral("I"), area);
            const QRectF box = glyphs.boundingRect();
            const int y = int(box.center().y());
            const double edge = box.left();
            const auto painted = [&](const QImage &image, double x) { return qAlpha(image.pixel(int(x), y)) > 200; };
            const QImage inside = VectorRenderer::render(typeDocument(QStringLiteral("I"), StrokeAlignment::inside, area), 1, true);
            const QImage outside = VectorRenderer::render(typeDocument(QStringLiteral("I"), StrokeAlignment::outside, area), 1, true);
            const QImage centre = VectorRenderer::render(typeDocument(QStringLiteral("I"), StrokeAlignment::center, area), 1, true);
            const double near = strokeWidth * 0.5;
            QVERIFY(painted(inside, edge + near) && !painted(inside, edge - near));
            QVERIFY(painted(outside, edge - near) && !painted(outside, edge + near));
            QVERIFY(painted(centre, edge + 1) && painted(centre, edge - 1));
        }
    }

    void counterOfAnOIsOutsideTheGlyph()
    {
        // The hole of an O is outside the type, so an outside stroke rims it and an inside one leaves it clear.
        const VectorDocument outside = typeDocument(QStringLiteral("O"), StrokeAlignment::outside, false);
        const QPainterPath glyphs = glyphsOf(QStringLiteral("O"), false);
        const QRectF box = glyphs.boundingRect();
        const QPointF middle = box.center();
        // Walk from the centre to the counter's edge.
        double x = middle.x();
        while (!glyphs.contains(QPointF(x, middle.y())) && x < box.right())
            x += 0.25;
        const QImage rim = VectorRenderer::render(outside, 1, true);
        QVERIFY(qAlpha(rim.pixel(int(x - strokeWidth * 0.5), int(middle.y()))) > 200);
        const QImage clear = VectorRenderer::render(typeDocument(QStringLiteral("O"), StrokeAlignment::inside, false), 1, true);
        QCOMPARE(qAlpha(clear.pixel(int(x - strokeWidth * 0.5), int(middle.y()))), 0);
    }

    void pdfReadBackStaysOnItsSideOfPointAndAreaType_data()
    {
        QTest::addColumn<bool>("area");
        QTest::addColumn<bool>("inside");
        QTest::newRow("point inside") << false << true;
        QTest::newRow("point outside") << false << false;
        QTest::newRow("area inside") << true << true;
        QTest::newRow("area outside") << true << false;
    }

    void pdfReadBackStaysOnItsSideOfPointAndAreaType()
    {
        QFETCH(bool, area);
        QFETCH(bool, inside);
        const StrokeAlignment alignment = inside ? StrokeAlignment::inside : StrokeAlignment::outside;
        const QString words = QStringLiteral("HOe");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("out.pdf"));
        DocumentExporter::writePdf(typeDocument(words, alignment, area), path);
        // The artboard keeps its size in points, so scale 1 lines up with the glyphs.
        const VectorDocument back = PdfImporter::read(path);
        const QImage image = VectorRenderer::render(back, 1, true).convertToFormat(QImage::Format_ARGB32);
        checkSides(image, glyphsOf(words, area), 1, alignment, "pdf");
    }
};

QTEST_MAIN(TextStrokeAlignTests)
#include "TextStrokeAlignTests.moc"
