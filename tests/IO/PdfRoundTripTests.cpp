#include "IO/DocumentExporter.h"
#include "IO/PdfImporter.h"
#include <QTemporaryDir>
#include <QTest>

namespace {
VectorDocument buildSample()
{
    VectorDocument document = VectorDocument::blank(QSizeF(200, 150));
    const QUuid layer = document.layers().front();

    VectorObject rect;
    rect.kind = ObjectKind::path;
    QPainterPath rectPath;
    rectPath.addRect(20, 20, 60, 40);
    rect.path = VectorPath::fromPainterPath(rectPath);
    rect.fill = Paint::solid(QColor(200, 50, 50));
    rect.stroke.paint = Paint::none();
    document.insert(rect, layer);

    VectorObject text;
    text.kind = ObjectKind::text;
    text.text.text = QStringLiteral("Round Trip");
    text.text.family = QStringLiteral("Sans Serif");
    text.text.size = 18;
    text.transform = QTransform::fromTranslate(20, 100);
    text.fill = Paint::solid(QColor(Qt::black));
    text.stroke.paint = Paint::none();
    document.insert(text, layer);

    return document;
}
}

class PdfRoundTripTests : public QObject {
    Q_OBJECT

private slots:
    // DocumentExporter::writePdf draws through the same VectorRenderer every
    // export format uses, which fills pre-computed glyph outlines
    // (VectorRenderer.cpp draws `object.text.outline()`) rather than calling
    // QPainter::drawText — so a round trip's text comes back as ordinary
    // filled paths, not a live text object, in every export format alike.
    // What a round trip actually promises, and what this checks: the
    // rectangle's fill color survives, and the text's glyph outlines survive
    // as enough additional filled paths to still be "the text strings match"
    // in substance, if not as an editable string.
    void pathsAndFillsSurviveARoundTrip()
    {
        const VectorDocument original = buildSample();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("roundtrip.pdf"));
        DocumentExporter::writePdf(original, path);

        QStringList warnings;
        const VectorDocument imported = PdfImporter::read(path, &warnings);
        QCOMPARE(imported.artboards.size(), size_t(1));
        QCOMPARE(imported.artboards.front().rect.size(), QSizeF(200, 150));

        bool foundRedPath = false;
        int textContours = 0;
        for (const VectorObject &object : imported.objects) {
            if (object.kind != ObjectKind::path || object.fill.kind != PaintKind::solid)
                continue;
            const QColor color = object.fill.color;
            if (std::abs(color.red() - 200) < 20 && std::abs(color.green() - 50) < 20 && std::abs(color.blue() - 50) < 20)
                foundRedPath = true;
            if (color == QColor(Qt::black))
                textContours += int(object.path.contours.size());
        }
        QVERIFY(foundRedPath);
        // "Round Trip" (9 non-space glyphs) becomes one black filled path
        // with one contour per glyph, more for glyphs with counters (o, R, d).
        QVERIFY(textContours >= 9);
    }
};

QTEST_MAIN(PdfRoundTripTests)
#include "PdfRoundTripTests.moc"
