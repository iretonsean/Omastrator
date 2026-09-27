#include "IO/SvgImporter.h"
#include <QTemporaryDir>
#include <QTest>

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

    void notSvgIsAFileError()
    {
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::parse("hello, world"));
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::parse("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>"));
        QVERIFY_THROWS_EXCEPTION(FileError, SvgImporter::read(QStringLiteral("/nonexistent/file.svg")));
    }
};

QTEST_MAIN(SvgImportTests)
#include "SvgImportTests.moc"
