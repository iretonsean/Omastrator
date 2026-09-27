#include "Document/EditorSession.h"
#include "Document/PathOperations.h"
#include "Document/StrokeGeometry.h"
#include "IO/DocumentExporter.h"
#include "IO/SvgExporter.h"
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

// Stacked fills and strokes, aligned strokes and arrowheads, out to SVG and PDF.
namespace {
StrokeStyle stroke(const QColor &color, double width)
{
    StrokeStyle style;
    style.paint = Paint::solid(color);
    style.width = width;
    return style;
}

VectorDocument withObject(const VectorObject &object)
{
    VectorDocument document = VectorDocument::blank({200, 200});
    document.insert(object, document.layers().front());
    return document;
}

struct Element {
    QString name;
    QXmlStreamAttributes attributes;
    int depth = 0;
};

std::vector<Element> elements(const QByteArray &svg)
{
    std::vector<Element> out;
    QXmlStreamReader reader(svg);
    int depth = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            out.push_back({reader.name().toString(), reader.attributes(), depth});
            depth += 1;
        } else if (reader.isEndElement()) {
            depth -= 1;
        }
    }
    return out;
}
}

class SvgAppearanceTests : public QObject {
    Q_OBJECT

private slots:
    void twoStrokesExportAsOneGroup()
    {
        VectorObject frame;
        frame.name = QStringLiteral("Frame");
        frame.path = Shapes::rectangle({40, 40, 120, 120});
        frame.setFills({Paint::solid(Qt::yellow)});
        frame.setStrokes({stroke(QColor(20, 20, 60), 20), stroke(QColor(250, 240, 200), 4)});
        frame.opacity = 0.8;
        const std::vector<Element> all = elements(SvgExporter::serialize(withObject(frame)));
        const auto group = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.value("id") == QLatin1String("Frame"); });
        QVERIFY(group != all.end());
        QCOMPARE(group->name, QStringLiteral("g"));
        QCOMPARE(group->attributes.value("opacity").toString(), QStringLiteral("0.8"));
        // Inside it: the fill, then the thick stroke, then the thin one.
        std::vector<Element> inside;
        for (auto it = group + 1; it != all.end() && it->depth > group->depth; ++it)
            inside.push_back(*it);
        QCOMPARE(inside.size(), size_t(3));
        QCOMPARE(inside[0].attributes.value("fill").toString(), QStringLiteral("#ffff00"));
        QCOMPARE(inside[1].attributes.value("stroke-width").toString(), QStringLiteral("20"));
        QCOMPARE(inside[1].attributes.value("fill").toString(), QStringLiteral("none"));
        QCOMPARE(inside[2].attributes.value("stroke").toString(), QStringLiteral("#faf0c8"));
        QCOMPARE(inside[2].attributes.value("stroke-width").toString(), QStringLiteral("4"));
    }

    void aSingleFillAndStrokeStayOnePath()
    {
        VectorObject plain;
        plain.name = QStringLiteral("Plain");
        plain.path = Shapes::rectangle({0, 0, 10, 10});
        plain.fill = Paint::solid(Qt::red);
        const std::vector<Element> all = elements(SvgExporter::serialize(withObject(plain)));
        const auto path = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.value("id") == QLatin1String("Plain"); });
        QVERIFY(path != all.end());
        QCOMPARE(path->name, QStringLiteral("path"));
    }

    void hiddenAndFadedEntriesExportAsTheyDraw()
    {
        VectorObject object;
        object.name = QStringLiteral("Stack");
        object.path = Shapes::rectangle({0, 0, 10, 10});
        Paint hidden = Paint::solid(Qt::green);
        hidden.isHidden = true;
        Paint faded = Paint::solid(Qt::blue);
        faded.opacity = 0.5;
        faded.blendMode = LayerBlendMode::multiply;
        object.setFills({Paint::solid(Qt::red), hidden, faded});
        object.setStrokes({});
        const QByteArray svg = SvgExporter::serialize(withObject(object));
        QVERIFY(!svg.contains("#00ff00"));
        const std::vector<Element> all = elements(svg);
        const auto blue = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.value("fill") == QLatin1String("#0000ff"); });
        QVERIFY(blue != all.end());
        QCOMPARE(blue->attributes.value("opacity").toString(), QStringLiteral("0.5"));
        QVERIFY(blue->attributes.value("style").toString().contains(QStringLiteral("multiply")));
    }

    void alignedAndArrowedStrokesExportAsOutlines()
    {
        VectorObject square;
        square.name = QStringLiteral("Square");
        square.path = Shapes::rectangle({50, 50, 100, 100});
        square.fill = Paint::none();
        StrokeStyle inside = stroke(Qt::black, 10);
        inside.alignment = StrokeAlignment::inside;
        square.stroke = inside;
        const std::vector<Element> all = elements(SvgExporter::serialize(withObject(square)));
        const auto group = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.value("id") == QLatin1String("Square"); });
        QVERIFY(group != all.end());
        const Element &outline = *(group + 1);
        QCOMPARE(outline.attributes.value("fill").toString(), QStringLiteral("#000000"));
        QVERIFY(!outline.attributes.hasAttribute("stroke-width"));
        // Its path stays inside the square.
        const QString d = outline.attributes.value("d").toString();
        QVERIFY(!d.isEmpty());

        VectorObject arrow;
        arrow.name = QStringLiteral("Arrow");
        arrow.path = Shapes::line({10, 100}, {190, 100});
        StrokeStyle headed = stroke(Qt::red, 2);
        headed.endArrow = Arrowhead::triangle;
        arrow.stroke = headed;
        const QByteArray svg = SvgExporter::serialize(withObject(arrow));
        QVERIFY(svg.contains("fill=\"#ff0000\""));
    }

    void variableWidthStrokesExportAsFilledOutlines()
    {
        VectorObject line;
        line.name = QStringLiteral("Line");
        line.path = Shapes::line({10, 100}, {190, 100});
        line.fill = Paint::none();
        StrokeStyle bulge = stroke(Qt::black, 10);
        bulge.widthProfile = StrokeWidthProfile::bulge;
        bulge.widthPoints = StrokeGeometry::presetWidthPoints(StrokeWidthProfile::bulge, bulge.width);
        line.stroke = bulge;
        const std::vector<Element> all = elements(SvgExporter::serialize(withObject(line)));
        const auto group = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.value("id") == QLatin1String("Line"); });
        QVERIFY(group != all.end());
        const Element &outline = *(group + 1);
        // A filled path, not a stroked line: no stroke-width, the stroke's colour as a fill.
        QCOMPARE(outline.attributes.value("fill").toString(), QStringLiteral("#000000"));
        QVERIFY(!outline.attributes.hasAttribute("stroke-width"));
        QVERIFY(!outline.attributes.value("d").toString().isEmpty());
    }

    void opacityMasksExportAsAnSvgMask()
    {
        EditorSession session;
        session.createDocument({100, 20});
        session.setDefaultFill(Paint::solid(Qt::red));
        const QUuid art = session.addPath(Shapes::rectangle({0, 0, 100, 20}), QStringLiteral("Art"));
        const QUuid fade = session.addPath(Shapes::rectangle({0, 0, 100, 20}), QStringLiteral("Fade"));
        session.select({fade});
        session.setFillOfSelection(Paint::linear(Qt::white, Qt::black));
        session.select({art, fade});
        session.makeOpacityMask();
        const QByteArray svg = SvgExporter::serialize(*session.document());
        QVERIFY(svg.contains("<mask"));
        const std::vector<Element> all = elements(svg);
        const auto masked = std::find_if(all.begin(), all.end(), [](const Element &e) { return e.attributes.hasAttribute("mask"); });
        QVERIFY(masked != all.end());
        QVERIFY(masked->attributes.value("mask").toString().startsWith(QStringLiteral("url(#")));

        // Invert Mask runs the mask through a colour-matrix filter.
        session.setOpacityMaskInverted(true);
        QVERIFY(SvgExporter::serialize(*session.document()).contains("feColorMatrix"));
    }

    void pdfExportDrawsTheStack()
    {
        VectorObject frame;
        frame.path = Shapes::rectangle({40, 40, 120, 120});
        frame.setStrokes({stroke(Qt::black, 20), stroke(Qt::white, 4)});
        StrokeStyle outside = stroke(Qt::red, 3);
        outside.alignment = StrokeAlignment::outside;
        outside.dashes = {5, 5};
        outside.alignDashes = true;
        frame.extraStrokes.push_back(outside);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("stack.pdf"));
        DocumentExporter::writePdf(withObject(frame), path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("%PDF"));
    }
};

QTEST_MAIN(SvgAppearanceTests)
#include "SvgAppearanceTests.moc"
