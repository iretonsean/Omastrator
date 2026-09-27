#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Document/StrokeGeometry.h"
#include "Document/Swatches.h"
#include "Rendering/VectorRenderer.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

// The appearance stack, stroke alignment and arrowheads, colour tools and Copy/Paste Properties.
namespace {
StrokeStyle stroke(const QColor &color, double width)
{
    StrokeStyle style;
    style.paint = Paint::solid(color);
    style.width = width;
    return style;
}

QColor pixel(const VectorDocument &document, QPointF at)
{
    return VectorRenderer::render(document, 1, false).pixelColor(at.toPoint());
}

bool close(const QColor &a, const QColor &b, int tolerance = 12)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance && std::abs(a.blue() - b.blue()) <= tolerance;
}

struct Drawn {
    EditorSession session;
    QUuid add(const VectorPath &path)
    {
        return session.addPath(path, QStringLiteral("Shape"));
    }
    VectorObject &object(const QUuid &id) { return *const_cast<VectorObject *>(session.document()->find(id)); }
    const VectorObject &read(const QUuid &id) const { return *session.document()->find(id); }
};
}

class PaintAppearanceTests : public QObject {
    Q_OBJECT

private slots:
    void aThickStrokeUnderAThinOneRendersAsOneObject()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid id = d.add(Shapes::rectangle({40, 40, 120, 120}));
        d.session.setFillsOfSelection({Paint::none()}, QStringLiteral("Fill"));
        d.session.setStrokesOfSelection({stroke(QColor(20, 20, 60), 20), stroke(QColor(250, 240, 200), 4)}, QStringLiteral("Stroke"));
        QCOMPARE(d.session.undoName(), QStringLiteral("Stroke"));
        QCOMPARE(d.read(id).strokes().size(), size_t(2));
        const VectorDocument &document = *d.session.document();
        // On the edge: the thin light stroke on top; 7 pt out: only the thick dark one.
        QVERIFY(close(pixel(document, {40, 100}), QColor(250, 240, 200)));
        QVERIFY(close(pixel(document, {33, 100}), QColor(20, 20, 60)));
        QVERIFY(close(pixel(document, {26, 100}), Qt::white));
        // Bounds take the widest stroke.
        const QRectF bounds = document.bounds(id, true);
        QVERIFY(std::abs(bounds.left() - 30) < 0.01 && std::abs(bounds.right() - 170) < 0.01);
    }

    void fillsStackWithTheirOwnEyeOpacityAndBlend()
    {
        Drawn d;
        d.session.createDocument({100, 100});
        const QUuid id = d.add(Shapes::rectangle({0, 0, 100, 100}));
        d.session.setStrokesOfSelection({}, QStringLiteral("Stroke"));
        Paint red = Paint::solid(Qt::red);
        d.session.setFillsOfSelection({Paint::solid(Qt::blue), red}, QStringLiteral("Fill"));
        QVERIFY(close(pixel(*d.session.document(), {50, 50}), Qt::red));
        red.isHidden = true;
        d.session.setFillsOfSelection({Paint::solid(Qt::blue), red}, QStringLiteral("Hide Fill"));
        QVERIFY(close(pixel(*d.session.document(), {50, 50}), Qt::blue));
        QVERIFY(d.read(id).hasVisibleFill());
        red.isHidden = false;
        red.opacity = 0.5;
        d.session.setFillsOfSelection({Paint::solid(Qt::blue), red}, QStringLiteral("Fill Opacity"));
        const QColor mixed = pixel(*d.session.document(), {50, 50});
        QVERIFY(close(mixed, QColor(128, 0, 127), 3));
        red.opacity = 1;
        red.blendMode = LayerBlendMode::multiply;
        d.session.setFillsOfSelection({Paint::solid(QColor(255, 255, 0)), red}, QStringLiteral("Fill Blending Mode"));
        QVERIFY(close(pixel(*d.session.document(), {50, 50}), Qt::red));
        red.blendMode = LayerBlendMode::screen;
        d.session.setFillsOfSelection({Paint::solid(QColor(0, 0, 255)), red}, QStringLiteral("Fill Blending Mode"));
        QVERIFY(close(pixel(*d.session.document(), {50, 50}), QColor(255, 0, 255)));
    }

    void theStackRoundTripsAndOldFilesReadUnchanged()
    {
        VectorObject object;
        object.path = Shapes::rectangle({0, 0, 10, 10});
        Paint top = Paint::linear(Qt::red, Qt::blue);
        top.opacity = 0.25;
        top.blendMode = LayerBlendMode::overlay;
        top.isHidden = true;
        Paint linked = Paint::solid(Qt::green);
        linked.swatchId = QStringLiteral("swatch-1");
        StrokeStyle arrowed = stroke(Qt::black, 3);
        arrowed.alignment = StrokeAlignment::outside;
        arrowed.startArrow = Arrowhead::circle;
        arrowed.endArrow = Arrowhead::triangle;
        arrowed.arrowScale = 150;
        arrowed.dashes = {6, 3};
        arrowed.alignDashes = true;
        object.setFills({linked, top});
        object.setStrokes({stroke(Qt::white, 1), arrowed});
        const VectorObject back = DocumentCodec::decodeObject(DocumentCodec::encode(object));
        QCOMPARE(back, object);

        // A version 2 object from before the stack: one fill, one stroke, no new keys.
        const QByteArray old = R"({"id":"6f1c5d2e-2d7b-4f6e-9e3a-0a4c7f0b1d11","kind":"path","name":"Old",
            "path":{"fillRule":"nonzero","contours":[{"closed":true,"nodes":[{"anchor":[0,0]},{"anchor":[10,0]},{"anchor":[10,10]}]}]},
            "fill":{"kind":"solid","color":"#ffff0000"},
            "stroke":{"paint":{"kind":"solid","color":"#ff000000"},"width":2,"cap":"butt","join":"miter","miterLimit":10,"dashes":[]}})";
        const QJsonObject json = QJsonDocument::fromJson(old).object();
        const VectorObject read = DocumentCodec::decodeObject(json);
        QVERIFY(read.extraFills.empty());
        QVERIFY(read.extraStrokes.empty());
        QVERIFY(read.hasSimpleAppearance());
        QCOMPARE(read.fill, Paint::solid(Qt::red));
        QCOMPARE(read.stroke.alignment, StrokeAlignment::center);
        // Saving it again writes nothing an older build wouldn't know.
        const QJsonObject again = DocumentCodec::encode(read);
        for (const char *key : {"moreFills", "moreStrokes"})
            QVERIFY(!again.contains(QLatin1String(key)));
        for (const char *key : {"align", "startArrow", "endArrow", "arrowScale", "alignDashes", "widthProfile", "widthPoints"})
            QVERIFY(!again["stroke"].toObject().contains(QLatin1String(key)));
        QCOMPARE(again["fill"].toObject().keys(), json["fill"].toObject().keys());
    }

    void anInsideStrokeStaysWithinItsBounds()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid id = d.add(Shapes::rectangle({50, 50, 100, 100}));
        d.session.setFillsOfSelection({Paint::none()}, QStringLiteral("Fill"));
        StrokeStyle inside = stroke(Qt::black, 10);
        inside.alignment = StrokeAlignment::inside;
        d.session.setStrokeOfSelection(inside);
        const QRectF square(50, 50, 100, 100);
        QCOMPARE(d.session.document()->bounds(id, true), square);
        const QPainterPath area = StrokeGeometry::area(d.read(id).outline(), inside);
        QVERIFY(square.adjusted(-0.01, -0.01, 0.01, 0.01).contains(area.boundingRect()));
        QVERIFY(area.contains(QPointF(55, 100)));
        QVERIFY(!area.contains(QPointF(45, 100)));
        QVERIFY(close(pixel(*d.session.document(), {48, 100}), Qt::white));
        QVERIFY(close(pixel(*d.session.document(), {57, 100}), Qt::black));
        // Outline Stroke keeps it inside too.
        d.session.outlineSelectedStrokes();
        QVERIFY(square.adjusted(-0.5, -0.5, 0.5, 0.5).contains(d.session.selectionBounds()));

        StrokeStyle outside = inside;
        outside.alignment = StrokeAlignment::outside;
        const QUuid other = d.add(Shapes::rectangle({50, 50, 100, 100}));
        d.session.setStrokeOfSelection(outside);
        const QRectF grown = d.session.document()->bounds(other, true);
        QVERIFY(std::abs(grown.left() - 40) < 0.01 && std::abs(grown.bottom() - 160) < 0.01);
        // Open paths ignore alignment.
        QCOMPARE(StrokeGeometry::extent(Shapes::line({0, 0}, {100, 0}).painterPath(), outside).height(), 10.0);
    }

    void outlineStrokeOnAnArrowedLineIncludesTheHead()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid id = d.add(Shapes::line({20, 100}, {180, 100}));
        StrokeStyle arrowed = stroke(Qt::black, 2);
        arrowed.endArrow = Arrowhead::triangle;
        arrowed.startArrow = Arrowhead::bar;
        d.session.setStrokeOfSelection(arrowed);
        const QRectF withHeads = d.session.document()->bounds(id, true);
        // A triangle 4.5 weights wide stands well past the 2 pt line.
        QVERIFY(withHeads.height() >= 8.9);
        QVERIFY(close(pixel(*d.session.document(), {173, 101}), Qt::black, 40));
        d.session.outlineSelectedStrokes();
        const QRectF outlined = d.session.selectionBounds();
        QVERIFY(outlined.height() >= 8.9);
        QVERIFY(outlined.right() >= 179.5);
        QVERIFY(outlined.left() <= 19.5);
        // Heads land on open ends only.
        StrokeStyle closedHeads = arrowed;
        QVERIFY(StrokeGeometry::heads(Shapes::rectangle({0, 0, 10, 10}).painterPath(), closedHeads).isEmpty());
        for (const Arrowhead head : {Arrowhead::arrow, Arrowhead::triangle, Arrowhead::circle, Arrowhead::square, Arrowhead::bar}) {
            StrokeStyle style = arrowed;
            style.endArrow = head;
            style.startArrow = Arrowhead::none;
            QVERIFY2(!StrokeGeometry::heads(Shapes::line({0, 0}, {100, 0}).painterPath(), style).isEmpty(), qPrintable(rawValue(head)));
        }
        // Scale grows the head.
        StrokeStyle big = arrowed;
        big.arrowScale = 200;
        QVERIFY(StrokeGeometry::heads(Shapes::line({0, 0}, {100, 0}).painterPath(), big).boundingRect().height()
                > StrokeGeometry::heads(Shapes::line({0, 0}, {100, 0}).painterPath(), arrowed).boundingRect().height() * 1.9);
    }

    void widthToolProfilesRenderAsFilledOutlines()
    {
        StrokeStyle style = stroke(Qt::black, 20);
        const QPainterPath line = Shapes::line({0, 0}, {200, 0}).painterPath();
        QVERIFY(style.isPlain());

        StrokeStyle bulge = style;
        bulge.widthProfile = StrokeWidthProfile::bulge;
        bulge.widthPoints = StrokeGeometry::presetWidthPoints(StrokeWidthProfile::bulge, style.width);
        QVERIFY(!bulge.isPlain());
        const QPainterPath bulged = StrokeGeometry::area(line, bulge);
        // The middle bulges past the plain pen width; it still covers the centerline throughout.
        QVERIFY(bulged.boundingRect().height() > style.width * 1.5);
        QVERIFY(bulged.contains(QPointF(100, 0)));
        QVERIFY(bulged.contains(QPointF(10, 0)));

        StrokeStyle taperStart = style;
        taperStart.widthPoints = StrokeGeometry::presetWidthPoints(StrokeWidthProfile::taperStart, style.width);
        const QPainterPath tapered = StrokeGeometry::area(line, taperStart);
        // It tapers to a point at the very start and is at full width well before the end.
        QVERIFY(!tapered.contains(QPointF(0, style.width / 2 - 1)));
        QVERIFY(tapered.contains(QPointF(150, 0)));

        StrokeStyle taperEnd = style;
        taperEnd.widthPoints = StrokeGeometry::presetWidthPoints(StrokeWidthProfile::taperEnd, style.width);
        const QPainterPath taperedEnd = StrokeGeometry::area(line, taperEnd);
        QVERIFY(taperedEnd.contains(QPointF(50, 0)));
        QVERIFY(!taperedEnd.contains(QPointF(199, style.width / 2 - 1)));

        // Outline Stroke and the codec both go through the same geometry.
        const StrokeStyle back = DocumentCodec::decodeStroke(DocumentCodec::encode(bulge));
        QCOMPARE(back, bulge);
        QCOMPARE(DocumentCodec::encode(bulge)["widthProfile"].toString(), QStringLiteral("bulge"));
    }

    void widthToolOutlineStrokeKeepsTheProfile()
    {
        Drawn d;
        d.session.createDocument({220, 40});
        d.add(Shapes::line({10, 20}, {210, 20}));
        d.session.setFillsOfSelection({Paint::none()}, QStringLiteral("Fill"));
        StrokeStyle bulge = stroke(Qt::black, 10);
        bulge.widthProfile = StrokeWidthProfile::bulge;
        bulge.widthPoints = StrokeGeometry::presetWidthPoints(StrokeWidthProfile::bulge, bulge.width);
        d.session.setStrokeOfSelection(bulge);
        // The bulge doubles the half-width at the middle: 7 pt off-centre is covered there, but not near the start.
        QVERIFY(close(pixel(*d.session.document(), {110, 13}), Qt::black));
        QVERIFY(close(pixel(*d.session.document(), {15, 13}), Qt::white));
        d.session.outlineSelectedStrokes();
        QCOMPARE(d.session.undoName(), QStringLiteral("Outline Stroke"));
        // No fill to keep, so Outline Stroke replaces the line outright with its filled outline.
        QCOMPARE(d.session.selection().size(), size_t(1));
        const VectorObject &outlined = d.read(d.session.selection().back());
        QVERIFY(outlined.path.bounds().height() > bulge.width * 1.5);
        QCOMPARE(outlined.fill, Paint::solid(Qt::black));
    }

    void dashesAlignToCorners()
    {
        StrokeStyle dashed = stroke(Qt::black, 2);
        dashed.dashes = {10, 10};
        const QPainterPath square = Shapes::rectangle({0, 0, 100, 100}).painterPath();
        // As they fall, the corner at (100, 0) ends a gap.
        QVERIFY(!StrokeGeometry::area(square, dashed).contains(QPointF(97, 0)));
        dashed.alignDashes = true;
        QVERIFY(!dashed.isPlain());
        const QPainterPath aligned = StrokeGeometry::area(square, dashed);
        for (const QPointF corner : {QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)}) {
            for (const QPointF step : {QPointF(3, 0), QPointF(-3, 0), QPointF(0, 3), QPointF(0, -3)}) {
                const QPointF at = corner + step;
                if (QRectF(0, 0, 100, 100).adjusted(-0.1, -0.1, 0.1, 0.1).contains(at))
                    QVERIFY2(aligned.contains(at), qPrintable(QStringLiteral("%1,%2").arg(at.x()).arg(at.y())));
            }
        }
        // An open line starts and ends on half dashes.
        const QPainterPath line = StrokeGeometry::alignedDashes(Shapes::line({0, 0}, {95, 0}).painterPath(), {10, 10});
        QVERIFY(line.boundingRect().left() <= 0.01);
        QVERIFY(line.boundingRect().right() >= 94.99);
    }

    void outlineStrokeAndPathfinderKeepTheStack()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid id = d.add(Shapes::rectangle({20, 20, 60, 60}));
        d.session.setFillsOfSelection({Paint::solid(Qt::yellow)}, QStringLiteral("Fill"));
        d.session.setStrokesOfSelection({stroke(Qt::black, 8), stroke(Qt::white, 2)}, QStringLiteral("Stroke"));
        d.session.outlineSelectedStrokes();
        QCOMPARE(d.session.undoName(), QStringLiteral("Outline Stroke"));
        // The filled original plus one path per stroke, in their order.
        QCOMPARE(d.session.selection().size(), size_t(3));
        QCOMPARE(d.session.selection().front(), id);
        QVERIFY(!d.read(id).hasVisibleStroke());
        QCOMPARE(d.read(d.session.selection()[1]).fill, Paint::solid(Qt::black));
        QCOMPARE(d.read(d.session.selection()[2]).fill, Paint::solid(Qt::white));
        d.session.undo();

        const QUuid top = d.add(Shapes::rectangle({50, 50, 60, 60}));
        d.session.setFillsOfSelection({Paint::solid(Qt::red), Paint::solid(QColor(0, 0, 255, 128))}, QStringLiteral("Fill"));
        d.session.select({id, top});
        d.session.combineSelection(BooleanOperation::unite);
        const VectorObject &united = d.read(d.session.selection().front());
        QCOMPARE(united.fills().size(), size_t(2));
        QCOMPARE(united.extraFills.front().color, QColor(0, 0, 255, 128));
    }

    void selectionColorsRecolourEveryUseInOneStep()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid a = d.add(Shapes::rectangle({0, 0, 20, 20}));
        d.session.setFillOfSelection(Paint::solid(Qt::red));
        d.session.setStrokeOfSelection(stroke(Qt::blue, 1));
        const QUuid b = d.add(Shapes::rectangle({40, 0, 20, 20}));
        d.session.setFillsOfSelection({Paint::linear(Qt::red, Qt::green)}, QStringLiteral("Fill"));
        d.session.setStrokeOfSelection(stroke(Qt::red, 1));
        d.session.select({a, b});
        const std::vector<QColor> colors = d.session.selectionColors();
        QCOMPARE(colors.size(), size_t(3));
        QCOMPARE(colors.front(), QColor(Qt::red));
        d.session.replaceColor(Qt::red, QColor(255, 102, 0));
        QCOMPARE(d.session.undoName(), QStringLiteral("Recolor"));
        QCOMPARE(d.read(a).fill.color, QColor(255, 102, 0));
        QCOMPARE(d.read(a).stroke.paint.color, QColor(Qt::blue));
        QCOMPARE(d.read(b).fill.stops.front().color, QColor(255, 102, 0));
        QCOMPARE(d.read(b).fill.stops.back().color, QColor(Qt::green));
        QCOMPARE(d.read(b).stroke.paint.color, QColor(255, 102, 0));
        d.session.undo();
        QCOMPARE(d.read(a).fill.color, QColor(Qt::red));
        QCOMPARE(d.read(b).stroke.paint.color, QColor(Qt::red));
    }

    void aGlobalSwatchUpdatesEveryUse()
    {
        Swatches library(QString{});
        library.add(Swatches::defaultGroup, {Swatch(QStringLiteral("Brand"), Qt::red), Swatch(QStringLiteral("Other"), Qt::blue)});
        const Swatch brand = library.groups().front().swatches.front();
        QVERIFY(!brand.id.isEmpty());
        QSignalSpy recolored(&library, &Swatches::globalSwatchRecolored);
        library.setColor(Swatches::defaultGroup, 0, Qt::darkRed);
        QCOMPARE(recolored.count(), 0);
        library.setGlobal(Swatches::defaultGroup, 0, true);
        library.setColor(Swatches::defaultGroup, 0, QColor(200, 0, 50));
        QCOMPARE(recolored.count(), 1);
        QCOMPARE(recolored.first().at(0).toString(), brand.id);
        // Ids and the global mark survive a save.
        const std::vector<SwatchGroup> again = Swatches::fromJson(library.toJson());
        QCOMPARE(again.front().swatches.front().id, brand.id);
        QVERIFY(again.front().swatches.front().global);
        QVERIFY(!again.front().swatches.back().global);

        Drawn d;
        d.session.createDocument({200, 200});
        Paint linked = Paint::solid(Qt::red);
        linked.swatchId = brand.id;
        const QUuid a = d.add(Shapes::rectangle({0, 0, 20, 20}));
        d.session.setFillOfSelection(linked);
        const QUuid b = d.add(Shapes::rectangle({40, 0, 20, 20}));
        d.session.setFillOfSelection(Paint::solid(Qt::red));
        StrokeStyle linkedStroke = stroke(Qt::red, 2);
        linkedStroke.paint.swatchId = brand.id;
        d.session.setStrokesOfSelection({stroke(Qt::black, 1), linkedStroke}, QStringLiteral("Stroke"));
        d.session.recolorSwatch(brand.id, QColor(200, 0, 50));
        QCOMPARE(d.session.undoName(), QStringLiteral("Edit Swatch"));
        QCOMPARE(d.read(a).fill.color, QColor(200, 0, 50));
        QCOMPARE(d.read(b).fill.color, QColor(Qt::red));
        QCOMPARE(d.read(b).extraStrokes.front().paint.color, QColor(200, 0, 50));
        // Picking a colour by hand unlinks it.
        d.session.select({a});
        d.session.replaceColor(QColor(200, 0, 50), Qt::yellow);
        QVERIFY(d.read(a).fill.swatchId.isEmpty());
    }

    void pastePropertiesFromTextOntoAPathTakesOnlyPaint()
    {
        Drawn d;
        d.session.createDocument({300, 300});
        const QUuid text = d.session.addText({20, 50}, QStringLiteral("Hello"));
        d.session.updateText([](TextContent &t) {
            t.size = 40;
            t.tracking = 50;
        }, QStringLiteral("Size"));
        d.session.setFillsOfSelection({Paint::solid(Qt::red), Paint::solid(QColor(0, 0, 255, 100))}, QStringLiteral("Fill"));
        d.session.setOpacityOfSelection(0.5);
        QVERIFY(d.session.canCopyProperties());
        QVERIFY(d.session.copyProperties());

        const QUuid path = d.add(Shapes::rectangle({100, 100, 50, 50}));
        const TextContent pathText = d.read(path).text;
        d.session.pasteProperties();
        QCOMPARE(d.session.undoName(), QStringLiteral("Paste Properties"));
        QCOMPARE(d.read(path).fills(), d.read(text).fills());
        QCOMPARE(d.read(path).opacity, 0.5);
        QCOMPARE(d.read(path).text, pathText);

        const QUuid other = d.session.addText({20, 200}, QStringLiteral("World"));
        d.session.pasteProperties();
        QCOMPARE(d.read(other).text.size, 40.0);
        QCOMPARE(d.read(other).text.tracking, 50.0);
        QCOMPARE(d.read(other).text.text, QStringLiteral("World"));
        QCOMPARE(d.read(other).fills(), d.read(text).fills());

        // One clipboard across documents.
        EditorSession second;
        second.createDocument({100, 100});
        second.addPath(Shapes::rectangle({0, 0, 10, 10}), QStringLiteral("Rectangle"));
        QVERIFY(second.canPasteProperties());
        second.pasteProperties();
        QCOMPARE(second.document()->find(second.selection().front())->fill, Paint::solid(Qt::red));
    }

    void pastePropertiesOntoStyledTextSpreadsTheSourcesFirstLook()
    {
        Drawn d;
        d.session.createDocument({300, 300});
        const QUuid source = d.session.addText({20, 50}, QStringLiteral("Hello"));
        d.object(source).text.formatCharacters(0, 2, [](CharacterFormat &format) {
            format.size = 60;
            format.fill = QColor(Qt::green);
        });
        QVERIFY(d.session.copyProperties());

        const QUuid target = d.session.addText({20, 200}, QStringLiteral("World wide"));
        d.object(target).text.formatCharacters(1, 3, [](CharacterFormat &format) { format.size = 10; });
        d.object(target).text.kerns[2] = 40;
        QVERIFY(!d.read(target).text.runs.empty());
        d.session.pasteProperties();
        const TextContent &pasted = d.read(target).text;
        QCOMPARE(pasted.text, QStringLiteral("World wide"));
        QVERIFY(pasted.runs.empty());
        QVERIFY(pasted.paragraphFormats.empty());
        QCOMPARE(pasted.size, 60.0);
        QCOMPARE(pasted.formatAt(4).size, 60.0);
        // A run's own colour stays with the source's characters; the paint comes from the fill stack.
        QVERIFY(!pasted.fill);
        QCOMPARE(pasted.kerns.at(2), 40.0);
    }

    void eyedropperAltClickGivesTheSelectionsStyle()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid target = d.add(Shapes::rectangle({100, 0, 20, 20}));
        const QUuid source = d.add(Shapes::rectangle({0, 0, 20, 20}));
        d.session.setFillsOfSelection({Paint::solid(Qt::green), Paint::solid(QColor(0, 0, 0, 50))}, QStringLiteral("Fill"));
        d.session.applyStyleTo(target);
        QCOMPARE(d.session.undoName(), QStringLiteral("Eyedropper"));
        QCOMPARE(d.read(target).fills(), d.read(source).fills());
        QCOMPARE(d.session.selection(), std::vector<QUuid>{source});
    }

    void eachStrokeScalesWithScaleStrokes()
    {
        Drawn d;
        d.session.createDocument({200, 200});
        const QUuid id = d.add(Shapes::rectangle({0, 0, 20, 20}));
        d.session.setStrokesOfSelection({stroke(Qt::black, 2), stroke(Qt::white, 4)}, QStringLiteral("Stroke"));
        d.session.scaleStrokes = true;
        d.session.scaleSelection(2, 2);
        QCOMPARE(d.read(id).stroke.width, 4.0);
        QCOMPARE(d.read(id).extraStrokes.front().width, 8.0);
    }
};

QTEST_MAIN(PaintAppearanceTests)
#include "PaintAppearanceTests.moc"
