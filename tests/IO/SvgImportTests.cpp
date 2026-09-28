#include "IO/SvgImporter.h"
#include <QBuffer>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#ifdef OMASTRATOR_HAVE_ZLIB
#include <zlib.h>
#endif

class SvgImportTests : public QObject {
    Q_OBJECT

private:
    static std::vector<const VectorObject *> paths(const VectorDocument &document)
    {
        std::vector<const VectorObject *> result;
        for (const VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::path)
                result.push_back(&object);
        }
        return result;
    }

    static bool near(const QRectF &a, const QRectF &b, double tolerance = 0.01)
    {
        return std::abs(a.left() - b.left()) < tolerance && std::abs(a.top() - b.top()) < tolerance
               && std::abs(a.right() - b.right()) < tolerance && std::abs(a.bottom() - b.bottom()) < tolerance;
    }

    static const VectorObject *named(const VectorDocument &document, const QString &name)
    {
        for (const VectorObject &object : document.objects) {
            if (object.name == name)
                return &object;
        }
        return nullptr;
    }

    static std::vector<const VectorObject *> children(const VectorDocument &document, const QUuid &parent)
    {
        std::vector<const VectorObject *> result;
        for (const QUuid &id : document.children(parent))
            result.push_back(document.find(id));
        return result;
    }

    static bool near(QPointF a, QPointF b, double tolerance = 1e-3)
    {
        return QLineF(a, b).length() < tolerance;
    }

    static QString pngURI(const QImage &image)
    {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    }

private slots:
    void rectangleBecomesACornerPath()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='100'>"
            "<rect id='box' x='10' y='20' width='30' height='40' fill='#ff8000'/></svg>");
        QCOMPARE(document.size, QSizeF(200, 100));
        QCOMPARE(document.layers().size(), size_t(1));
        const auto shapes = paths(document);
        QCOMPARE(shapes.size(), size_t(1));
        const VectorObject &rect = *shapes.front();
        QCOMPARE(rect.name, QStringLiteral("box"));
        QCOMPARE(rect.parentID, document.layers().front());
        QCOMPARE(rect.path.contours.size(), size_t(1));
        const Contour &contour = rect.path.contours.front();
        QVERIFY(contour.closed);
        QCOMPARE(contour.nodes.size(), size_t(4));
        for (const PathNode &node : contour.nodes)
            QVERIFY(!node.hasIn() && !node.hasOut());
        QVERIFY(near(rect.path.bounds(), {10, 20, 30, 40}));
        QCOMPARE(rect.fill.kind, PaintKind::solid);
        QCOMPARE(rect.fill.color, QColor(255, 128, 0));
        QVERIFY(!rect.stroke.isVisible());
    }

    void circleKeepsItsCurves()
    {
        const VectorDocument document = SvgImporter::parse(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='25' fill='blue'/></svg>");
        const VectorObject &circle = *paths(document).front();
        const Contour &contour = circle.path.contours.front();
        QVERIFY(contour.closed);
        QCOMPARE(contour.nodes.size(), size_t(4));
        for (const PathNode &node : contour.nodes) {
            QVERIFY(node.hasIn() && node.hasOut());
            QVERIFY(node.smooth);
        }
        QVERIFY(near(circle.path.bounds(), {25, 25, 50, 50}, 0.05));
    }

    void transformsAreApplied()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
            "<g transform='translate(10,20) scale(2)'><rect width='10' height='5' transform='rotate(90)'/></g></svg>");
        // rotate(90) takes (10, 5) to (-5, 10); scaled then moved.
        QVERIFY(near(paths(document).front()->path.bounds(), {0, 20, 10, 20}));
    }

    void viewBoxScalesToTheArtboard()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='400' height='200' viewBox='0 0 200 100'>"
            "<rect x='10' y='10' width='20' height='20'/></svg>");
        QCOMPARE(document.size, QSizeF(400, 200));
        QVERIFY(near(paths(document).front()->path.bounds(), {20, 20, 40, 40}));
    }

    void strokeStyleIsRead()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
            "<path d='M10 10 L90 10 L90 90' fill='none' stroke='#00ff00' stroke-opacity='0.5' stroke-width='4'"
                  "stroke-linecap='round' stroke-linejoin='bevel' stroke-miterlimit='7' stroke-dasharray='5 3' opacity='0.25'/></svg>");
        const VectorObject &path = *paths(document).front();
        QVERIFY(!path.fill.isVisible());
        QVERIFY(!path.path.contours.front().closed);
        QCOMPARE(path.path.contours.front().nodes.size(), size_t(3));
        QCOMPARE(path.stroke.paint.kind, PaintKind::solid);
        QCOMPARE(path.stroke.paint.color.green(), 255);
        QVERIFY(std::abs(path.stroke.paint.color.alphaF() - 0.5) < 0.01);
        QCOMPARE(path.stroke.width, 4.0);
        QCOMPARE(path.stroke.cap, Qt::RoundCap);
        QCOMPARE(path.stroke.join, Qt::BevelJoin);
        QCOMPARE(path.stroke.miterLimit, 7.0);
        QCOMPARE(path.stroke.dashes, (std::vector<double>{5, 3}));
        QCOMPARE(path.opacity, 0.25);
    }

    void evenOddCompoundPathKeepsItsHole()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='100' height='100'>"
            "<path fill-rule='evenodd' d='M0 0 H100 V100 H0 Z M25 25 H75 V75 H25 Z' fill='black'/></svg>");
        const VectorObject &path = *paths(document).front();
        QCOMPARE(path.path.fillRule, Qt::OddEvenFill);
        QCOMPARE(path.path.contours.size(), size_t(2));
        const QPainterPath painter = path.path.painterPath();
        QVERIFY(painter.contains(QPointF(10, 10)));
        QVERIFY(!painter.contains(QPointF(50, 50)));
    }

    void nonzeroIsTheDefault()
    {
        const VectorDocument document = SvgImporter::parse(
            "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><path d='M0 0 H10 V10 Z'/></svg>");
        const VectorObject &path = *paths(document).front();
        QCOMPARE(path.path.fillRule, Qt::WindingFill);
        // SVG fills black when nothing says otherwise.
        QCOMPARE(path.fill.color, QColor(Qt::black));
    }

    void linearGradientBecomesFractions()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'>"
            "<defs><linearGradient id='g' x1='0' y1='0' x2='0' y2='1'>"
              "<stop offset='0' stop-color='red'/><stop offset='1' stop-color='blue' stop-opacity='0.5'/></linearGradient></defs>"
            "<rect x='50' y='50' width='100' height='40' fill='url(#g)'/></svg>");
        const Paint &fill = paths(document).front()->fill;
        QCOMPARE(fill.kind, PaintKind::linearGradient);
        QCOMPARE(fill.stops.size(), size_t(2));
        QCOMPARE(fill.stops.front().color, QColor(Qt::red));
        QCOMPARE(fill.stops.back().color.blue(), 255);
        QVERIFY(std::abs(fill.stops.back().color.alphaF() - 0.5) < 0.01);
        QVERIFY2(QLineF(fill.start, QPointF(0, 0)).length() < 1e-3,
                 qPrintable(QStringLiteral("%1,%2 %3,%4").arg(fill.start.x()).arg(fill.start.y()).arg(fill.end.x()).arg(fill.end.y())));
        QVERIFY(QLineF(fill.end, QPointF(0, 1)).length() < 1e-3);
    }

    void radialGradientInUserSpace()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'>"
            "<radialGradient id='r' gradientUnits='userSpaceOnUse' cx='100' cy='100' r='50'>"
              "<stop offset='0' stop-color='white'/><stop offset='1' stop-color='black'/></radialGradient>"
            "<rect x='0' y='0' width='200' height='200' fill='url(#r)'/></svg>");
        const Paint &fill = paths(document).front()->fill;
        QCOMPARE(fill.kind, PaintKind::radialGradient);
        QVERIFY(QLineF(fill.start, QPointF(0.5, 0.5)).length() < 1e-3);
        QVERIFY(QLineF(fill.end, QPointF(0.75, 0.5)).length() < 1e-3);
    }

    void hiddenShapesArriveHidden()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<rect width='5' height='5' display='none'/><rect width='5' height='5'/></svg>");
        const auto shapes = paths(document);
        QCOMPARE(shapes.size(), size_t(2));
        QVERIFY(!shapes[0]->isVisible);
        QVERIFY(shapes[1]->isVisible);
    }

    void readNamesTheLayerAfterTheFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Logo Mark.svg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'/></svg>");
        file.close();
        const VectorDocument document = SvgImporter::read(path);
        QCOMPARE(document.find(document.layers().front())->name, QStringLiteral("Logo Mark"));
    }

#ifdef OMASTRATOR_HAVE_ZLIB
    void svgzIsGunzippedBeforeParsing()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("compressed.svgz"));
        const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                                "<rect width='5' height='5'/></svg>";
        QByteArray gzip;
        gzip.resize(svg.size() + 128);
        z_stream stream{};
        deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(svg.constData()));
        stream.avail_in = static_cast<uInt>(svg.size());
        stream.next_out = reinterpret_cast<Bytef *>(gzip.data());
        stream.avail_out = static_cast<uInt>(gzip.size());
        QCOMPARE(deflate(&stream, Z_FINISH), Z_STREAM_END);
        gzip.resize(static_cast<int>(stream.total_out));
        deflateEnd(&stream);

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(gzip);
        file.close();
        const VectorDocument document = SvgImporter::read(path);
        QCOMPARE(document.size, QSizeF(10, 10));
        QCOMPARE(paths(document).size(), size_t(1));
    }
#endif

    void topLevelGroupsBecomeLayers()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' "
            "xmlns:inkscape='http://www.inkscape.org/namespaces/inkscape' width='100' height='100'>"
            "<g inkscape:label='Back' id='layer1' data-name='Ignored'><rect width='10' height='10'/></g>"
            "<rect id='loose' width='5' height='5'/>"
            "<g data-name='Art' id='g2'><g id='Face'><circle cx='5' cy='5' r='2'/><rect width='1' height='1'/></g></g>"
            "<g id='Top'/></svg>");
        const std::vector<QUuid> layers = document.layers();
        QCOMPARE(layers.size(), size_t(4));
        QCOMPARE(document.find(layers[0])->name, QStringLiteral("Back"));
        QCOMPARE(document.find(layers[1])->name, QStringLiteral("Layer 1"));
        QCOMPARE(document.find(layers[2])->name, QStringLiteral("Art"));
        QCOMPARE(document.find(layers[3])->name, QStringLiteral("Top"));
        QCOMPARE(document.find(layers[0])->kind, ObjectKind::layer);
        QVERIFY(document.find(layers[0])->layerColor != document.find(layers[1])->layerColor);
        QCOMPARE(children(document, layers[1]).front()->name, QStringLiteral("loose"));
        const auto art = children(document, layers[2]);
        QCOMPARE(art.size(), size_t(1));
        QCOMPARE(art.front()->kind, ObjectKind::group);
        QCOMPARE(art.front()->name, QStringLiteral("Face"));
        const auto face = children(document, art.front()->id);
        QCOMPARE(face.size(), size_t(2));
        // Shapes without an id don't take their group's.
        QCOMPARE(face[0]->name, QStringLiteral("Path"));
        QCOMPARE(face[1]->name, QStringLiteral("Path"));
    }

    void inkscapeLayerAttributesSetLockAndVisibility()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' "
            "xmlns:inkscape='http://www.inkscape.org/namespaces/inkscape' "
            "xmlns:sodipodi='http://sodipodi.sourceforge.net/DTD/sodipodi-0.0.dtd' width='100' height='100'>"
            "<g inkscape:groupmode='layer' inkscape:label='Locked' sodipodi:insensitive='true'>"
            "<rect width='10' height='10'/></g>"
            "<g inkscape:groupmode='layer' inkscape:label='Hidden' style='display:none'>"
            "<rect width='10' height='10'/></g>"
            "<g inkscape:groupmode='layer' inkscape:label='Open'><rect width='10' height='10'/></g></svg>");
        const std::vector<QUuid> layers = document.layers();
        QCOMPARE(layers.size(), size_t(3));
        QVERIFY(document.find(layers[0])->isLocked);
        QVERIFY(document.find(layers[0])->isVisible);
        QVERIFY(!document.find(layers[1])->isLocked);
        QVERIFY(!document.find(layers[1])->isVisible);
        QVERIFY(!document.find(layers[2])->isLocked);
        QVERIFY(document.find(layers[2])->isVisible);
    }

    void groupOpacityStaysOnTheGroup()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><g><g id='G' opacity='0.5'>"
            "<rect id='A' width='5' height='5'/><rect id='B' width='5' height='5' style='opacity:0.25'/></g></g></svg>");
        QCOMPARE(named(document, QStringLiteral("G"))->opacity, 0.5);
        QCOMPARE(named(document, QStringLiteral("A"))->opacity, 1.0);
        QCOMPARE(named(document, QStringLiteral("B"))->opacity, 0.25);
    }

    void blendModesAndHiddenObjects()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<g id='L' style='mix-blend-mode: multiply'><g id='Off' visibility='hidden'><rect id='In' width='5' height='5'/></g>"
            "<rect id='Gone' style='display:none' width='5' height='5'/><rect id='Screen' mix-blend-mode='screen' width='5' height='5'/></g></svg>");
        QCOMPARE(named(document, QStringLiteral("L"))->blendMode, LayerBlendMode::multiply);
        QCOMPARE(named(document, QStringLiteral("Screen"))->blendMode, LayerBlendMode::screen);
        QVERIFY(!named(document, QStringLiteral("Off"))->isVisible);
        QVERIFY(named(document, QStringLiteral("In"))->isVisible);
        QVERIFY(!document.isEffectivelyVisible(named(document, QStringLiteral("In"))->id));
        QVERIFY(!named(document, QStringLiteral("Gone"))->isVisible);
    }

    void textWithSpansBecomesLines()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'>"
            "<text id='T' x='10' y='40' font-family=\"'DejaVu Sans', sans-serif\" font-size='20px' font-weight='700' font-style='italic'"
            " text-anchor='end' fill='#ff0000' stroke='blue' stroke-width='2' letter-spacing='1.5' transform='rotate(90)'>"
            "<tspan x='10' y='40'>Hello</tspan><tspan x='10' dy='30'>wide <tspan font-weight='normal'>World</tspan></tspan></text></svg>");
        const VectorObject *text = named(document, QStringLiteral("T"));
        QVERIFY(text);
        QCOMPARE(text->kind, ObjectKind::text);
        QCOMPARE(text->text.text, QStringLiteral("Hello\nwide World"));
        QCOMPARE(text->text.family, QStringLiteral("DejaVu Sans"));
        QCOMPARE(text->text.size, 20.0);
        QVERIFY(text->text.isBold());
        QVERIFY(text->text.isItalic());
        QCOMPARE(text->text.alignment, TextAlignment::right);
        // 30 pt between baselines; 1.5 pt letter spacing is 75/1000 em at 20 pt.
        QCOMPARE(text->text.leading, std::optional<double>(30));
        QCOMPARE(text->text.tracking, 75.0);
        QCOMPARE(text->fill.color, QColor(Qt::red));
        QCOMPARE(text->stroke.paint.color, QColor(Qt::blue));
        QCOMPARE(text->stroke.width, 2.0);
        // rotate(90) takes the baseline origin (10, 40) to (-40, 10).
        QVERIFY(near(text->transform.map(QPointF(0, 0)), {-40, 10}));
        QVERIFY(near(text->transform.map(QPointF(1, 0)), {-40, 11}));
    }

    void textInheritsFromGroupsAndCollapsesSpaces()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200' viewBox='0 0 100 100'>"
            "<g style='font-size:12pt; fill:#0000ff; font-family:serif' transform='translate(5 0)'><text x='5' y='6'>\n   Hello \n  there  </text></g>"
            "<text y='50'>   </text></svg>");
        std::vector<const VectorObject *> texts;
        for (const VectorObject &object : document.objects) {
            if (object.kind == ObjectKind::text)
                texts.push_back(&object);
        }
        QCOMPARE(texts.size(), size_t(1));
        const VectorObject &text = *texts.front();
        QCOMPARE(text.text.text, QStringLiteral("Hello there"));
        QCOMPARE(text.name, QStringLiteral("Hello there"));
        QCOMPARE(text.text.family, QStringLiteral("Serif"));
        QVERIFY(std::abs(text.text.size - 16) < 1e-9);
        QCOMPARE(text.fill.color, QColor(Qt::blue));
        QVERIFY(!text.stroke.isVisible());
        // The viewBox doubles everything.
        QVERIFY(near(text.transform.map(QPointF(0, 0)), {20, 12}));
        QVERIFY(near(text.transform.map(QPointF(1, 0)), {22, 12}));
    }

    void embeddedImagesArePlaced()
    {
        QImage pixels(4, 2, QImage::Format_ARGB32);
        pixels.fill(QColor(0, 200, 0));
        const QString uri = pngURI(pixels);
        const VectorDocument document = SvgImporter::parse(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='200' height='200'>"
            "<image id='Meet' x='10' y='20' width='40' height='40' xlink:href='%1'/>"
            "<image id='Stretch' x='0' y='0' width='8' height='8' preserveAspectRatio='none' transform='translate(100 100)' href='%1'/>"
            "<image id='Slice' x='0' y='100' width='20' height='20' preserveAspectRatio='xMinYMin slice' href='%1'/></svg>").arg(uri).toUtf8());
        const VectorObject *meet = named(document, QStringLiteral("Meet"));
        QVERIFY(meet);
        QCOMPARE(meet->kind, ObjectKind::image);
        QCOMPARE(meet->image.size(), QSize(4, 2));
        QCOMPARE(meet->image.pixelColor(1, 1).green(), 200);
        // Fitted: 10 times, centred in the 40 × 40 box.
        QVERIFY(near(document.bounds(meet->id).topLeft(), {10, 30}));
        QVERIFY(near(document.bounds(meet->id).bottomRight(), {50, 50}));
        const VectorObject *stretch = named(document, QStringLiteral("Stretch"));
        QVERIFY(near(document.bounds(stretch->id).bottomRight(), {108, 108}));
        const VectorObject *slice = named(document, QStringLiteral("Slice"));
        QVERIFY(near(document.bounds(slice->id).bottomRight(), {40, 120}));
        const VectorObject *crop = document.find(*slice->parentID);
        QVERIFY(crop->isClipGroup);
        const VectorObject *clip = children(document, crop->id).front();
        QCOMPARE(clip->kind, ObjectKind::path);
        QVERIFY(near(clip->path.bounds().bottomRight(), {20, 120}));
    }

    void linkedImagesResolveBesideTheFile()
    {
        QTemporaryDir dir;
        QImage pixels(3, 3, QImage::Format_RGB32);
        pixels.fill(Qt::red);
        QDir(dir.path()).mkdir(QStringLiteral("art"));
        QVERIFY(pixels.save(dir.filePath(QStringLiteral("art/red dot.png"))));
        const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
                               "<image id='Dot' width='3' height='3' href='art/red%20dot.png'/></svg>";
        const QString path = dir.filePath(QStringLiteral("linked.svg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(svg);
        file.close();
        QStringList warnings;
        const VectorDocument document = SvgImporter::read(path, &warnings);
        QVERIFY(warnings.isEmpty());
        const VectorObject *dot = named(document, QStringLiteral("Dot"));
        QVERIFY(dot);
        QCOMPARE(dot->image.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(document.find(document.layers().front())->name, QStringLiteral("linked"));

        // From bytes there is no folder to look in.
        const VectorDocument unlinked = SvgImporter::parse(svg, &warnings);
        QVERIFY(!named(unlinked, QStringLiteral("Dot")));
        QCOMPARE(warnings.size(), 1);
        QVERIFY(warnings.front().contains(QStringLiteral("Linked images")));
    }

    void clipPathOnAGroupMakesAClipGroup()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='200' height='200'>"
            "<defs><clipPath id='c'><circle cx='50' cy='50' r='40'/></clipPath>"
            "<clipPath id='e' transform='translate(0 10)'><rect width='10' height='10' clip-rule='evenodd'/></clipPath></defs>"
            "<g id='Layer'><g id='Art' clip-path='url(#c)' transform='translate(10 0)'><rect id='Box' width='100' height='100' fill='red'/></g>"
            "<rect id='Shape' clip-path='url(#e)' transform='translate(100 0)' width='50' height='50'/></g>"
            "<g id='Cropped' clip-path='url(#c)'><rect id='Under' width='5' height='5'/></g></svg>");
        const VectorObject *art = named(document, QStringLiteral("Art"));
        QVERIFY(art);
        QVERIFY(art->isClipGroup);
        const auto inside = children(document, art->id);
        QCOMPARE(inside.size(), size_t(2));
        QCOMPARE(inside[0]->name, QStringLiteral("Clipping Path"));
        QVERIFY(!inside[0]->fill.isVisible());
        QVERIFY(!inside[0]->stroke.isVisible());
        QVERIFY(near(inside[0]->path.bounds(), {20, 10, 80, 80}, 0.05));
        QCOMPARE(inside[1]->name, QStringLiteral("Box"));
        QVERIFY(near(inside[1]->path.bounds(), {10, 0, 100, 100}));

        const VectorObject *shape = named(document, QStringLiteral("Shape"));
        const VectorObject *wrapper = document.find(*shape->parentID);
        QVERIFY(wrapper->isClipGroup);
        QCOMPARE(wrapper->kind, ObjectKind::group);
        const VectorObject *clip = children(document, wrapper->id).front();
        QCOMPARE(clip->path.fillRule, Qt::OddEvenFill);
        QVERIFY(near(clip->path.bounds(), {100, 10, 10, 10}));
        // A layer can't clip: a clip group inside it does.
        const VectorObject *cropped = named(document, QStringLiteral("Cropped"));
        QCOMPARE(cropped->kind, ObjectKind::layer);
        QVERIFY(!cropped->isClipGroup);
        const VectorObject *under = named(document, QStringLiteral("Under"));
        QVERIFY(document.find(*under->parentID)->isClipGroup);
        QCOMPARE(document.find(*under->parentID)->parentID, cropped->id);
    }

    void styleSheetOpacityStillApplies()
    {
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<style>.half{opacity:0.5}</style><rect id='R' class='half' width='5' height='5'/></svg>");
        QCOMPARE(named(document, QStringLiteral("R"))->opacity, 0.5);
    }

    void leftOutFeaturesAreReported()
    {
        QStringList warnings;
        const VectorDocument document = SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<defs><filter id='f'/><mask id='m'/></defs><rect id='Blur' filter='url(#f)' width='5' height='5'/>"
            "<g style='mask:url(#m)'><rect width='5' height='5'/></g><use href='#Blur'/></svg>", &warnings);
        QVERIFY(named(document, QStringLiteral("Blur")));
        QCOMPARE(warnings, SvgImporter::lastWarnings());
        QVERIFY(warnings.contains(QStringLiteral("Filters were left out.")));
        QVERIFY(warnings.contains(QStringLiteral("Masks were left out.")));
        QCOMPARE(warnings.size(), 3);
        SvgImporter::parse("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'/></svg>");
        QVERIFY(SvgImporter::lastWarnings().isEmpty());
    }

    void longAndUnicodeNamesSurvive()
    {
        const QString longName = QStringLiteral("a").repeated(100);
        const VectorDocument document = SvgImporter::parse(QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'>"
            "<g data-name='Smile 😀 ☺'><rect id='%1' width='5' height='5'/><rect data-name='Ω 😀' width='5' height='5'/></g></svg>").arg(longName).toUtf8());
        QVERIFY(named(document, QStringLiteral("Smile 😀 ☺")));
        QVERIFY(named(document, longName));
        QVERIFY(named(document, QStringLiteral("Ω 😀")));
    }

    void malformedXmlStillImportsItsShapes()
    {
        QStringList warnings;
        const VectorDocument document = SvgImporter::parse(
            "<svg xmlns='http://www.w3.org/2000/svg' width='10' height='10'><rect width='5' height='5'><g></svg>", &warnings);
        QCOMPARE(paths(document).size(), size_t(1));
        QCOMPARE(document.layers().size(), size_t(1));
        QCOMPARE(warnings.size(), 1);
    }

    void notSvgIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::parse("hello, world"));
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::parse("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>"));
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::read(QStringLiteral("/nonexistent/file.svg")));
    }
};

QTEST_MAIN(SvgImportTests)
#include "SvgImportTests.moc"
