#include "Document/PathOperations.h"
#include "IO/SvgExporter.h"
#include "IO/SvgImporter.h"
#include <QTemporaryDir>
#include <QTest>

class SvgRoundTripTests : public QObject {
    Q_OBJECT

private:
    static QUuid add(VectorDocument &document, VectorObject object, std::optional<QUuid> parent = std::nullopt)
    {
        const QUuid id = object.id;
        document.insert(std::move(object), parent.value_or(document.layers().front()));
        return id;
    }

    static VectorObject shape(const QString &name, const VectorPath &path, const Paint &fill)
    {
        VectorObject object;
        object.name = name;
        object.path = path;
        object.fill = fill;
        object.stroke.paint = Paint::none();
        return object;
    }

    static const VectorObject *named(const VectorDocument &document, const QString &name)
    {
        for (const VectorObject &object : document.objects) {
            if (object.name == name && object.kind == ObjectKind::path)
                return &object;
        }
        return nullptr;
    }

    static bool near(const QRectF &a, const QRectF &b, double tolerance = 0.01)
    {
        return std::abs(a.left() - b.left()) < tolerance && std::abs(a.top() - b.top()) < tolerance
               && std::abs(a.right() - b.right()) < tolerance && std::abs(a.bottom() - b.bottom()) < tolerance;
    }

    static bool near(QPointF a, QPointF b)
    {
        return QLineF(a, b).length() < 1e-3;
    }

    static bool near(const QTransform &a, const QTransform &b)
    {
        return near(a.map(QPointF(0, 0)), b.map(QPointF(0, 0))) && near(a.map(QPointF(1, 0)), b.map(QPointF(1, 0)))
               && near(a.map(QPointF(0, 1)), b.map(QPointF(0, 1)));
    }

    static const VectorObject *find(const VectorDocument &document, const QString &name, ObjectKind kind)
    {
        for (const VectorObject &object : document.objects) {
            if (object.name == name && object.kind == kind)
                return &object;
        }
        return nullptr;
    }

    static VectorObject container(ObjectKind kind, const QString &name)
    {
        VectorObject object;
        object.kind = kind;
        object.name = name;
        return object;
    }

    static VectorDocument roundTrip(const VectorDocument &document, const SvgExporter::Options &options = {})
    {
        return SvgImporter::parse(SvgExporter::serialize(document, options));
    }

private slots:
    void artboardSizeSurvives()
    {
        const VectorDocument document = roundTrip(VectorDocument::blank({612.5, 792}));
        QCOMPARE(document.size, QSizeF(612.5, 792));
    }

    void rectanglesAndEllipsesKeepBoundsAndFills()
    {
        VectorDocument document = VectorDocument::blank({300, 200});
        add(document, shape(QStringLiteral("Rect"), Shapes::rectangle({10, 20, 100, 50}), Paint::solid(QColor(200, 10, 20))));
        add(document, shape(QStringLiteral("Rounded"), Shapes::rectangle({120, 20, 60, 60}, 10), Paint::solid(QColor(0, 0, 255, 128))));
        add(document, shape(QStringLiteral("Ellipse"), Shapes::ellipse({30, 100, 80, 40}), Paint::solid(Qt::green)));

        const VectorDocument read = roundTrip(document);
        for (const QString &name : {QStringLiteral("Rect"), QStringLiteral("Rounded"), QStringLiteral("Ellipse")}) {
            const VectorObject *before = named(document, name);
            const VectorObject *after = named(read, name);
            QVERIFY2(after, qPrintable(name));
            QVERIFY2(near(after->path.bounds(), before->path.bounds()), qPrintable(name));
            QCOMPARE(after->fill.kind, PaintKind::solid);
            QCOMPARE(after->fill.color.rgb(), before->fill.color.rgb());
            QVERIFY(std::abs(after->fill.color.alphaF() - before->fill.color.alphaF()) < 0.01);
            QCOMPARE(after->path.contours.front().nodes.size(), before->path.contours.front().nodes.size());
            QVERIFY(after->path.contours.front().closed);
        }
    }

    void compoundPathKeepsItsHole()
    {
        VectorDocument document = VectorDocument::blank({200, 200});
        VectorPath ring = Shapes::ellipse({0, 0, 200, 200});
        ring.contours.push_back(Shapes::ellipse({50, 50, 100, 100}).contours.front());
        ring.fillRule = Qt::OddEvenFill;
        add(document, shape(QStringLiteral("Ring"), ring, Paint::solid(Qt::black)));

        const VectorDocument result = roundTrip(document);
        const VectorObject *read = named(result, QStringLiteral("Ring"));
        QVERIFY(read);
        QCOMPARE(read->path.fillRule, Qt::OddEvenFill);
        QCOMPARE(read->path.contours.size(), size_t(2));
        QVERIFY(near(read->path.bounds(), {0, 0, 200, 200}));
        QVERIFY(read->path.painterPath().contains(QPointF(25, 100)));
        QVERIFY(!read->path.painterPath().contains(QPointF(100, 100)));
    }

    void gradientsKeepTheirEndsAndStops()
    {
        VectorDocument document = VectorDocument::blank({400, 400});
        Paint linear = Paint::linear(QColor(255, 0, 0), QColor(0, 0, 255, 100));
        linear.start = {0.1, 0.2};
        linear.end = {0.9, 0.6};
        linear.stops.insert(linear.stops.begin() + 1, GradientStop{0.3, QColor(0, 255, 0)});
        add(document, shape(QStringLiteral("Linear"), Shapes::rectangle({20, 30, 200, 100}), linear));
        Paint radial = Paint::radial(Qt::white, Qt::black);
        radial.start = {0.4, 0.5};
        radial.end = {0.9, 0.5};
        add(document, shape(QStringLiteral("Radial"), Shapes::ellipse({100, 200, 150, 100}), radial));

        const VectorDocument read = roundTrip(document);
        const VectorObject *l = named(read, QStringLiteral("Linear"));
        QVERIFY(l);
        QCOMPARE(l->fill.kind, PaintKind::linearGradient);
        QVERIFY(near(l->fill.start, linear.start));
        QVERIFY(near(l->fill.end, linear.end));
        QCOMPARE(l->fill.stops.size(), size_t(3));
        QVERIFY(std::abs(l->fill.stops[1].offset - 0.3) < 1e-4);
        QCOMPARE(l->fill.stops[1].color.rgb(), QColor(0, 255, 0).rgb());
        QVERIFY(std::abs(l->fill.stops[2].color.alpha() - 100) <= 1);

        const VectorObject *r = named(read, QStringLiteral("Radial"));
        QVERIFY(r);
        QCOMPARE(r->fill.kind, PaintKind::radialGradient);
        QVERIFY(near(r->fill.start, radial.start));
        // The radius is 0.5 of the width: 75 points.
        QVERIFY2(near(r->fill.end, radial.end), qPrintable(QStringLiteral("%1 %2").arg(r->fill.end.x()).arg(r->fill.end.y())));
    }

    void strokesSurvive()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        VectorObject line = shape(QStringLiteral("Line"), Shapes::line({10, 10}, {90, 50}), Paint::none());
        line.stroke.paint = Paint::solid(QColor(10, 20, 30));
        line.stroke.width = 5;
        line.stroke.cap = Qt::RoundCap;
        line.stroke.join = Qt::RoundJoin;
        line.stroke.dashes = {6, 2, 1, 2};
        line.opacity = 0.4;
        add(document, line);

        const VectorDocument result = roundTrip(document);
        const VectorObject *read = named(result, QStringLiteral("Line"));
        QVERIFY(read);
        QVERIFY(!read->fill.isVisible());
        QCOMPARE(read->stroke.paint.color.rgb(), QColor(10, 20, 30).rgb());
        QCOMPARE(read->stroke.width, 5.0);
        QCOMPARE(read->stroke.cap, Qt::RoundCap);
        QCOMPARE(read->stroke.join, Qt::RoundJoin);
        QCOMPARE(read->stroke.dashes, (std::vector<double>{6, 2, 1, 2}));
        QVERIFY(std::abs(read->opacity - 0.4) < 1e-4);
        QVERIFY(!read->path.contours.front().closed);
    }

    void hiddenObjectsAreLeftOut()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        VectorObject hidden = shape(QStringLiteral("Hidden"), Shapes::rectangle({0, 0, 10, 10}), Paint::solid(Qt::red));
        hidden.isVisible = false;
        add(document, hidden);
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = QStringLiteral("Off");
        layer.isVisible = false;
        document.objects.push_back(layer);
        add(document, shape(QStringLiteral("InHiddenLayer"), Shapes::rectangle({0, 0, 10, 10}), Paint::solid(Qt::red)), layer.id);

        const QByteArray svg = SvgExporter::serialize(document);
        QVERIFY(!svg.contains("Hidden"));
        QVERIFY(!svg.contains("InHiddenLayer"));
    }

    void structureAndTextAreWritten()
    {
        VectorDocument document = VectorDocument::blank({300, 200});
        const QUuid layer = document.layers().front();
        document.find(layer)->blendMode = LayerBlendMode::softLight;
        VectorObject group;
        group.kind = ObjectKind::group;
        group.name = QStringLiteral("Clipped");
        group.isClipGroup = true;
        group.opacity = 0.5;
        const QUuid groupID = add(document, group);
        add(document, shape(QStringLiteral("Mask"), Shapes::ellipse({0, 0, 50, 50}), Paint::none()), groupID);
        add(document, shape(QStringLiteral("Inside"), Shapes::rectangle({0, 0, 100, 100}), Paint::solid(Qt::red)), groupID);
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Caption");
        text.text.text = QStringLiteral("One & two\nThree");
        text.text.family = QStringLiteral("DejaVu Sans");
        text.text.size = 18;
        text.text.style = TextContent::styleFor(text.text.family, 400, true);
        text.text.alignment = TextAlignment::center;
        text.fill = Paint::solid(Qt::blue);
        text.transform = QTransform::fromTranslate(150, 100);
        add(document, text);
        VectorObject image;
        image.kind = ObjectKind::image;
        image.image = QImage(2, 2, QImage::Format_ARGB32_Premultiplied);
        image.image.fill(Qt::yellow);
        image.transform = QTransform::fromTranslate(5, 6);
        add(document, image);

        const QString svg = QString::fromUtf8(SvgExporter::serialize(document));
        QVERIFY(svg.contains(QStringLiteral("viewBox=\"0 0 300 200\"")));
        QVERIFY(svg.contains(QStringLiteral("inkscape:groupmode=\"layer\"")));
        QVERIFY(svg.contains(QStringLiteral("mix-blend-mode:soft-light")));
        QVERIFY(svg.contains(QStringLiteral("<clipPath")));
        QVERIFY(svg.contains(QStringLiteral("clip-path=\"url(#clip")));
        QVERIFY(svg.contains(QStringLiteral("opacity=\"0.5\"")));
        QVERIFY(svg.contains(QStringLiteral("font-family=\"DejaVu Sans\"")));
        QVERIFY(svg.contains(QStringLiteral("font-style=\"italic\"")));
        QVERIFY(svg.contains(QStringLiteral("text-anchor=\"middle\"")));
        QVERIFY(svg.contains(QStringLiteral("One &amp; two</tspan>")));
        QCOMPARE(svg.count(QStringLiteral("<tspan")), 2);
        QVERIFY(svg.contains(QStringLiteral("matrix(1 0 0 1 150 100)")));
        QVERIFY(svg.contains(QStringLiteral("data:image/png;base64,")));

        // The clip shape is a definition, not a drawn path; what it clips still imports.
        const VectorDocument read = SvgImporter::parse(svg.toUtf8());
        QVERIFY(!named(read, QStringLiteral("Mask")));
        QVERIFY(named(read, QStringLiteral("Inside")));
    }

    void layersGroupsAndNamesSurvive()
    {
        VectorDocument document = VectorDocument::blank({200, 200});
        document.find(document.layers().front())->name = QStringLiteral("Background & Sky");
        add(document, shape(QStringLiteral("Sky"), Shapes::rectangle({0, 0, 200, 100}), Paint::solid(Qt::cyan)));
        VectorObject art = container(ObjectKind::layer, QStringLiteral("Art"));
        art.opacity = 0.75;
        art.blendMode = LayerBlendMode::multiply;
        const QUuid artID = art.id;
        document.objects.push_back(art);
        VectorObject outer = container(ObjectKind::group, QStringLiteral("Face"));
        outer.opacity = 0.5;
        outer.blendMode = LayerBlendMode::colorDodge;
        const QUuid outerID = add(document, outer, artID);
        const QUuid innerID = add(document, container(ObjectKind::group, QStringLiteral("Eyes")), outerID);
        VectorObject eye = shape(QStringLiteral("Left eye"), Shapes::ellipse({50, 50, 10, 10}), Paint::solid(Qt::black));
        eye.opacity = 0.25;
        eye.blendMode = LayerBlendMode::screen;
        add(document, eye, innerID);
        add(document, shape(QStringLiteral("Right eye"), Shapes::ellipse({80, 50, 10, 10}), Paint::solid(Qt::black)), innerID);
        add(document, shape(QStringLiteral("Mouth"), Shapes::rectangle({50, 80, 40, 5}), Paint::solid(Qt::red)), outerID);

        QStringList warnings;
        const VectorDocument read = SvgImporter::parse(SvgExporter::serialize(document), &warnings);
        QVERIFY(warnings.isEmpty());
        const std::vector<QUuid> layers = read.layers();
        QCOMPARE(layers.size(), size_t(2));
        QCOMPARE(read.find(layers[0])->name, QStringLiteral("Background & Sky"));
        const VectorObject *artRead = read.find(layers[1]);
        QCOMPARE(artRead->name, QStringLiteral("Art"));
        QVERIFY(std::abs(artRead->opacity - 0.75) < 1e-6);
        QCOMPARE(artRead->blendMode, LayerBlendMode::multiply);
        const VectorObject *face = find(read, QStringLiteral("Face"), ObjectKind::group);
        QVERIFY(face);
        QCOMPARE(face->parentID, artRead->id);
        QCOMPARE(face->opacity, 0.5);
        QCOMPARE(face->blendMode, LayerBlendMode::colorDodge);
        const VectorObject *eyes = find(read, QStringLiteral("Eyes"), ObjectKind::group);
        QCOMPARE(eyes->parentID, face->id);
        const std::vector<QUuid> order = read.children(face->id);
        QCOMPARE(order.size(), size_t(2));
        QCOMPARE(order.front(), eyes->id);
        QCOMPARE(read.find(order.back())->name, QStringLiteral("Mouth"));
        const VectorObject *left = named(read, QStringLiteral("Left eye"));
        QCOMPARE(left->parentID, eyes->id);
        // Group opacity isn't multiplied into the children.
        QCOMPARE(left->opacity, 0.25);
        QCOMPARE(left->blendMode, LayerBlendMode::screen);
        QCOMPARE(named(read, QStringLiteral("Right eye"))->opacity, 1.0);
        QCOMPARE(named(read, QStringLiteral("Sky"))->parentID, layers[0]);
    }

    void textSurvives()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Headline");
        text.text.text = QStringLiteral("Big  <news>\n\n  indented & more");
        text.text.family = QStringLiteral("DejaVu Serif");
        text.text.size = 30.5;
        text.text.style = TextContent::styleFor(text.text.family, 700, true);
        text.text.alignment = TextAlignment::center;
        text.text.leading = 45.75;
        text.text.tracking = 50;
        text.fill = Paint::solid(QColor(10, 20, 30, 128));
        text.stroke.paint = Paint::solid(QColor(200, 0, 0));
        text.stroke.width = 1.5;
        text.opacity = 0.6;
        text.blendMode = LayerBlendMode::overlay;
        text.transform = QTransform().rotate(30) * QTransform::fromScale(2, 1.5) * QTransform::fromTranslate(120, 80);
        add(document, text);
        VectorObject right = text;
        right.id = QUuid::createUuid();
        right.name = QStringLiteral("Right");
        right.text = TextContent();
        right.text.text = QStringLiteral("Aligned");
        right.text.alignment = TextAlignment::right;
        right.opacity = 1;
        right.blendMode = LayerBlendMode::normal;
        right.stroke.paint = Paint::none();
        right.transform = QTransform::fromTranslate(300, 250);
        add(document, right);

        const VectorDocument read = roundTrip(document);
        const VectorObject *after = find(read, QStringLiteral("Headline"), ObjectKind::text);
        QVERIFY(after);
        QCOMPARE(after->text.text, text.text.text);
        QCOMPARE(after->text.family, text.text.family);
        QCOMPARE(after->text.size, text.text.size);
        QVERIFY(after->text.isBold());
        QVERIFY(after->text.isItalic());
        QCOMPARE(after->text.alignment, TextAlignment::center);
        QCOMPARE(after->text.leading, std::optional<double>(45.75));
        QVERIFY(std::abs(after->text.tracking - 50) < 1e-6);
        QCOMPARE(after->fill.color.rgb(), text.fill.color.rgb());
        QVERIFY(std::abs(after->fill.color.alphaF() - 0.5) < 0.01);
        QCOMPARE(after->stroke.paint.color, QColor(200, 0, 0));
        QVERIFY(std::abs(after->stroke.width - 1.5) < 1e-4);
        QVERIFY(std::abs(after->opacity - 0.6) < 1e-6);
        QCOMPARE(after->blendMode, LayerBlendMode::overlay);
        QVERIFY(near(after->transform, text.transform));

        const VectorObject *aligned = find(read, QStringLiteral("Right"), ObjectKind::text);
        QVERIFY(aligned);
        QCOMPARE(aligned->text, right.text);
        QVERIFY(!aligned->stroke.isVisible());
        QVERIFY(near(aligned->transform, right.transform));
    }

    void textGradientSurvives()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Shiny");
        text.text.text = QStringLiteral("Gradient");
        text.text.size = 40;
        text.fill = Paint::linear(Qt::red, Qt::blue);
        text.fill.start = {0.2, 0.1};
        text.fill.end = {0.8, 0.9};
        text.stroke.paint = Paint::none();
        text.transform = QTransform().rotate(-20) * QTransform::fromTranslate(50, 150);
        add(document, text);

        const VectorDocument read = roundTrip(document);
        const VectorObject *after = find(read, QStringLiteral("Shiny"), ObjectKind::text);
        QVERIFY(after);
        QCOMPARE(after->fill.kind, PaintKind::linearGradient);
        QVERIFY2(QLineF(after->fill.start, text.fill.start).length() < 1e-2,
                 qPrintable(QStringLiteral("%1,%2").arg(after->fill.start.x()).arg(after->fill.start.y())));
        QVERIFY(QLineF(after->fill.end, text.fill.end).length() < 1e-2);
        QCOMPARE(after->fill.stops.size(), size_t(2));
    }

    void imagesSurvive()
    {
        VectorDocument document = VectorDocument::blank({300, 300});
        VectorObject image;
        image.kind = ObjectKind::image;
        image.name = QStringLiteral("Photo");
        image.image = QImage(3, 2, QImage::Format_ARGB32);
        image.image.fill(Qt::transparent);
        image.image.setPixelColor(0, 0, QColor(255, 0, 0));
        image.image.setPixelColor(2, 1, QColor(0, 0, 255, 128));
        image.transform = QTransform::fromScale(10, 20) * QTransform().rotate(15) * QTransform::fromTranslate(40, 60);
        image.opacity = 0.8;
        add(document, image);

        const VectorDocument read = roundTrip(document);
        const VectorObject *after = find(read, QStringLiteral("Photo"), ObjectKind::image);
        QVERIFY(after);
        QCOMPARE(after->image.size(), QSize(3, 2));
        QCOMPARE(after->image.pixelColor(0, 0), QColor(255, 0, 0));
        QCOMPARE(after->image.pixelColor(2, 1).alpha(), 128);
        QCOMPARE(after->image.pixelColor(1, 0).alpha(), 0);
        QVERIFY(near(after->transform, image.transform));
        QVERIFY(std::abs(after->opacity - 0.8) < 1e-6);
        QVERIFY(!read.find(*after->parentID)->isClipGroup);
    }

    void clipGroupsSurvive()
    {
        VectorDocument document = VectorDocument::blank({300, 300});
        VectorObject group = container(ObjectKind::group, QStringLiteral("Porthole"));
        group.isClipGroup = true;
        group.opacity = 0.9;
        const QUuid groupID = add(document, group);
        VectorPath ring = Shapes::ellipse({0, 0, 100, 100});
        ring.contours.push_back(Shapes::ellipse({25, 25, 50, 50}).contours.front());
        ring.fillRule = Qt::OddEvenFill;
        add(document, shape(QStringLiteral("Hole"), ring, Paint::none()), groupID);
        add(document, shape(QStringLiteral("Sea"), Shapes::rectangle({-50, -50, 200, 200}), Paint::solid(Qt::blue)), groupID);
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Label");
        text.text.text = QStringLiteral("Ahoy");
        text.fill = Paint::solid(Qt::white);
        text.stroke.paint = Paint::none();
        text.transform = QTransform::fromTranslate(30, 50);
        add(document, text, groupID);

        const VectorDocument read = roundTrip(document);
        const VectorObject *after = find(read, QStringLiteral("Porthole"), ObjectKind::group);
        QVERIFY(after);
        QVERIFY(after->isClipGroup);
        QVERIFY(std::abs(after->opacity - 0.9) < 1e-6);
        const std::vector<QUuid> inside = read.children(after->id);
        QCOMPARE(inside.size(), size_t(3));
        const VectorObject *clip = read.find(inside[0]);
        QCOMPARE(clip->kind, ObjectKind::path);
        QVERIFY(!clip->fill.isVisible());
        QCOMPARE(clip->path.fillRule, Qt::OddEvenFill);
        QCOMPARE(clip->path.contours.size(), size_t(2));
        QVERIFY(near(clip->path.bounds(), {0, 0, 100, 100}));
        QCOMPARE(read.find(inside[1])->name, QStringLiteral("Sea"));
        QCOMPARE(read.find(inside[2])->kind, ObjectKind::text);
        QVERIFY(near(read.find(inside[2])->transform, text.transform));
    }

    void hiddenShapesInTheSvgArriveHidden()
    {
        // Export leaves hidden objects out; hidden ones written by other apps stay, hidden.
        const VectorDocument read = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<g id='Off' style='display:none'><text id='Note' y='5'>Hi</text><image id='Pic' width='1' height='1' href='data:,'/></g></svg>");
        const VectorObject *layer = read.find(read.layers().front());
        QCOMPARE(layer->name, QStringLiteral("Off"));
        QVERIFY(!layer->isVisible);
        QVERIFY(find(read, QStringLiteral("Note"), ObjectKind::text));
    }

    void textAsOutlinesImportsAsPaths()
    {
        VectorDocument document = VectorDocument::blank({300, 100});
        VectorObject text;
        text.kind = ObjectKind::text;
        text.name = QStringLiteral("Word");
        text.text.text = QStringLiteral("Hello");
        text.fill = Paint::solid(Qt::black);
        text.stroke.paint = Paint::none();
        text.transform = QTransform::fromTranslate(20, 60);
        add(document, text);

        SvgExporter::Options options;
        options.textAsOutlines = true;
        const QByteArray svg = SvgExporter::serialize(document, options);
        QVERIFY(!svg.contains("<text"));
        const VectorDocument result = SvgImporter::parse(svg);
        const VectorObject *read = named(result, QStringLiteral("Word"));
        QVERIFY(read);
        QVERIFY(near(read->path.bounds(), document.bounds(text.id), 0.05));
    }

    void backgroundIsOptional()
    {
        VectorDocument document = VectorDocument::blank({50, 50});
        document.background = QColor(1, 2, 3);
        QVERIFY(!SvgExporter::serialize(document).contains("#010203"));
        SvgExporter::Options options;
        options.includeBackground = true;
        QVERIFY(SvgExporter::serialize(document, options).contains("#010203"));
    }

    void writeSavesAFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.svg"));
        VectorDocument document = VectorDocument::blank({80, 60});
        add(document, shape(QStringLiteral("R"), Shapes::rectangle({1, 2, 3, 4}), Paint::solid(Qt::red)));
        SvgExporter::write(document, path);
        const VectorDocument read = SvgImporter::read(path);
        QCOMPARE(read.size, QSizeF(80, 60));
        QVERIFY(named(read, QStringLiteral("R")));
    }
};

QTEST_MAIN(SvgRoundTripTests)
#include "SvgRoundTripTests.moc"
