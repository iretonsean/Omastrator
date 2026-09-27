#include "Document/EditorSession.h"
#include "Document/ShapeBuilder.h"
#include <QTest>
#include <cmath>

namespace {
// Area by sampling a one-point grid: slow but independent of how the path is built.
double areaOf(const VectorPath &path)
{
    const QPainterPath shape = path.painterPath();
    const QRectF bounds = shape.boundingRect();
    double count = 0;
    for (double y = std::floor(bounds.top()) + 0.5; y < bounds.bottom(); y += 1) {
        for (double x = std::floor(bounds.left()) + 0.5; x < bounds.right(); x += 1)
            count += shape.contains(QPointF(x, y)) ? 1 : 0;
    }
    return count;
}

ShapeBuilder::Gesture merging(std::vector<int> regions)
{
    ShapeBuilder::Gesture gesture;
    gesture.regions = std::move(regions);
    return gesture;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

bool near(QRectF a, QRectF b, double tolerance = 0.5)
{
    return near(a.left(), b.left(), tolerance) && near(a.top(), b.top(), tolerance) && near(a.right(), b.right(), tolerance)
        && near(a.bottom(), b.bottom(), tolerance);
}

struct Scene {
    EditorSession session;
    Scene() { session.createDocument({400, 400}); }

    QUuid add(const VectorPath &path, const QColor &fill)
    {
        const QUuid id = session.addPath(path, QStringLiteral("Shape"));
        VectorObject object = *session.document()->find(id);
        object.fill = Paint::solid(fill);
        session.updateObject(object, QStringLiteral("Fill"));
        return id;
    }

    ShapeBuilder::Arrangement arrangement() const
    {
        return ShapeBuilder::arrange(*session.document(), session.selectedLeaves(), session.shapeBuilder);
    }

    int regionAt(const ShapeBuilder::Arrangement &arrangement, QPointF point) const
    {
        const std::optional<int> region = arrangement.regionAt(point);
        return region ? *region : -1;
    }

    // One gesture, as the canvas runs it: begin, preview, commit.
    bool gesture(const ShapeBuilder::Gesture &gesture)
    {
        const ShapeBuilder::Arrangement built = arrangement();
        session.beginInteraction(QStringLiteral("Shape Builder"));
        const bool changed = session.previewShapeBuild(built, gesture);
        session.commitInteraction();
        return changed;
    }

    std::vector<const VectorObject *> paths() const
    {
        std::vector<const VectorObject *> result;
        for (const VectorObject &object : session.document()->objects) {
            if (object.kind == ObjectKind::path)
                result.push_back(&object);
        }
        return result;
    }

    const VectorObject *pathAt(QPointF point) const
    {
        const VectorObject *found = nullptr;
        for (const VectorObject *object : paths()) {
            if (object->path.painterPath().contains(point))
                found = object;
        }
        return found;
    }
};
}

class ShapeBuilderTests : public QObject {
    Q_OBJECT

private slots:
    void twoOverlappingShapesMakeThreeRegions()
    {
        for (const bool circles : {true, false}) {
            Scene scene;
            auto shape = [&](QRectF rect) { return circles ? Shapes::ellipse(rect) : Shapes::rectangle(rect); };
            scene.add(shape({0, 0, 100, 100}), Qt::red);
            scene.add(shape({50, 0, 100, 100}), Qt::blue);
            scene.session.selectAll();
            const ShapeBuilder::Arrangement arrangement = scene.arrangement();
            QVERIFY(!arrangement.truncated);
            QCOMPARE(arrangement.sources.size(), size_t(2));
            QCOMPARE(arrangement.regions.size(), size_t(3));
            const int lens = scene.regionAt(arrangement, {75, 50});
            QVERIFY(lens >= 0);
            QCOMPARE(arrangement.regions[size_t(lens)].owners, (std::vector<int>{0, 1}));
            QCOMPARE(scene.regionAt(arrangement, {300, 300}), -1);
        }
    }

    void threeOverlappingShapesMakeSevenRegions()
    {
        for (const bool circles : {true, false}) {
            Scene scene;
            auto shape = [&](QRectF rect) { return circles ? Shapes::ellipse(rect) : Shapes::rectangle(rect); };
            scene.add(shape({0, 0, 100, 100}), Qt::red);
            scene.add(shape({60, 0, 100, 100}), Qt::green);
            scene.add(shape({30, 50, 100, 100}), Qt::blue);
            scene.session.selectAll();
            const ShapeBuilder::Arrangement arrangement = scene.arrangement();
            QCOMPARE(arrangement.regions.size(), size_t(7));
            const int middle = scene.regionAt(arrangement, {80, 70});
            QVERIFY(middle >= 0);
            QCOMPARE(arrangement.regions[size_t(middle)].owners.size(), size_t(3));
        }
    }

    void mergingTwoRegionsMakesOnePath()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::rectangle({50, 0, 100, 100}), Qt::blue);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture(merging({scene.regionAt(arrangement, {20, 50}), scene.regionAt(arrangement, {75, 50})})));
        QCOMPARE(scene.paths().size(), size_t(2));
        const VectorObject *merged = scene.pathAt({20, 50});
        QVERIFY(merged && merged == scene.pathAt({75, 50}));
        QVERIFY(near(merged->path.bounds(), QRectF(0, 0, 100, 100)));
        QVERIFY(near(areaOf(merged->path), 10000, 50));
        // The rest of the top rectangle stands alone.
        const VectorObject *rest = scene.pathAt({125, 50});
        QVERIFY(rest && rest != merged);
        QVERIFY(near(rest->path.bounds(), QRectF(100, 0, 50, 100)));
        QCOMPARE(scene.session.undoName(), QStringLiteral("Shape Builder"));
        // Both results stay selected.
        QCOMPARE(scene.session.selection().size(), size_t(2));
    }

    void mergingThreeCirclesKeepsTheOtherRegions()
    {
        Scene scene;
        scene.add(Shapes::ellipse({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::ellipse({60, 0, 100, 100}), Qt::green);
        scene.add(Shapes::ellipse({30, 50, 100, 100}), Qt::blue);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture(merging({scene.regionAt(arrangement, {20, 40}), scene.regionAt(arrangement, {80, 25})})));
        QCOMPARE(scene.paths().size(), size_t(6));
        const VectorObject *merged = scene.pathAt({20, 40});
        QVERIFY(merged && merged == scene.pathAt({80, 25}));
        // Curves stay curves: a handful of anchors with handles, not a polygon.
        QVERIFY(merged->path.nodeCount() < 16);
        const auto &nodes = merged->path.contours.front().nodes;
        QVERIFY(std::any_of(nodes.begin(), nodes.end(), [](const PathNode &node) { return node.hasOut(); }));
    }

    void clickSeparatesARegion()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::rectangle({50, 0, 100, 100}), Qt::blue);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture({{scene.regionAt(arrangement, {75, 50})}, {}, false, true}));
        QCOMPARE(scene.paths().size(), size_t(3));
        const VectorObject *lens = scene.pathAt({75, 50});
        QVERIFY(near(lens->path.bounds(), QRectF(50, 0, 50, 100)));
        // Picked from artwork: the topmost shape over the region.
        QCOMPARE(lens->fill.color, QColor(Qt::blue));
        QCOMPARE(scene.pathAt({20, 50})->fill.color, QColor(Qt::red));
    }

    void altClickDeletesARegion()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 200, 200}), Qt::red);
        scene.add(Shapes::rectangle({50, 50, 50, 50}), Qt::blue);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QCOMPARE(arrangement.regions.size(), size_t(2));
        QVERIFY(scene.gesture({{scene.regionAt(arrangement, {75, 75})}, {}, true, true}));
        QCOMPARE(scene.paths().size(), size_t(1));
        // A hole where the small square was.
        const VectorObject *ring = scene.paths().front();
        QVERIFY(!ring->path.painterPath().contains(QPointF(75, 75)));
        QVERIFY(ring->path.painterPath().contains(QPointF(20, 20)));
        QVERIFY(near(areaOf(ring->path), 40000 - 2500, 60));
        QCOMPARE(ring->fill.color, QColor(Qt::red));
    }

    void altClickOnAnOverlapShrinksTheArea()
    {
        Scene scene;
        scene.add(Shapes::ellipse({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::ellipse({50, 0, 100, 100}), Qt::blue);
        scene.session.selectAll();
        double before = 0;
        for (const VectorObject *object : scene.paths())
            before += areaOf(object->path);
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        const int lens = scene.regionAt(arrangement, {75, 50});
        const double lensArea = arrangement.regions[size_t(lens)].size;
        QVERIFY(scene.gesture({{lens}, {}, true, true}));
        QCOMPARE(scene.paths().size(), size_t(2));
        QVERIFY(!scene.pathAt({75, 50}));
        double after = 0;
        for (const VectorObject *object : scene.paths())
            after += areaOf(object->path);
        QVERIFY2(near(after, before - 2 * lensArea, 80), qPrintable(QStringLiteral("%1 %2 %3").arg(before).arg(after).arg(lensArea)));
    }

    void mergeTakesTheStyleWhereTheDragBegan()
    {
        for (const auto &[start, colour] : {std::pair(QPointF(20, 50), QColor(Qt::red)), std::pair(QPointF(125, 50), QColor(Qt::blue)),
                                            std::pair(QPointF(75, 50), QColor(Qt::blue))}) {
            Scene scene;
            scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
            scene.add(Shapes::rectangle({50, 0, 100, 100}), Qt::blue);
            scene.session.selectAll();
            const ShapeBuilder::Arrangement arrangement = scene.arrangement();
            ShapeBuilder::Gesture gesture;
            gesture.regions.push_back(scene.regionAt(arrangement, start));
            for (const QPointF point : {QPointF(20, 50), QPointF(75, 50), QPointF(125, 50)})
                gesture.regions.push_back(scene.regionAt(arrangement, point));
            QVERIFY(scene.gesture(gesture));
            QCOMPARE(scene.paths().size(), size_t(1));
            QCOMPARE(scene.paths().front()->fill.color, colour);
            QVERIFY(near(scene.paths().front()->path.bounds(), QRectF(0, 0, 150, 100)));
        }
    }

    void swatchesStyleTheMergeWithTheCurrentColours()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::rectangle({50, 0, 100, 100}), Qt::blue);
        scene.session.selectAll();
        scene.session.setDefaultFill(Paint::solid(Qt::green));
        scene.session.shapeBuilder.colorFromArtwork = false;
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture(merging({scene.regionAt(arrangement, {20, 50}), scene.regionAt(arrangement, {75, 50})})));
        QCOMPARE(scene.pathAt({20, 50})->fill.color, QColor(Qt::green));
        QCOMPARE(scene.pathAt({125, 50})->fill.color, QColor(Qt::blue));
    }

    void oneUndoStepRestoresEverything()
    {
        Scene scene;
        scene.add(Shapes::ellipse({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::ellipse({60, 0, 100, 100}), Qt::green);
        scene.add(Shapes::ellipse({30, 50, 100, 100}), Qt::blue);
        scene.session.selectAll();
        const VectorDocument before = *scene.session.document();
        const std::vector<QUuid> selection = scene.session.selection();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture({{scene.regionAt(arrangement, {80, 70})}, {}, true, true}));
        QVERIFY(scene.gesture(merging({scene.regionAt(scene.arrangement(), {20, 40}), scene.regionAt(scene.arrangement(), {80, 25})})));
        QCOMPARE(scene.session.undoName(), QStringLiteral("Shape Builder"));
        scene.session.undo();
        scene.session.undo();
        QVERIFY(*scene.session.document() == before);
        QCOMPARE(scene.session.selection(), selection);
        QCOMPARE(scene.session.undoName(), QStringLiteral("Fill"));
    }

    void cancelLeavesNoStep()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::rectangle({50, 0, 100, 100}), Qt::blue);
        scene.session.selectAll();
        const VectorDocument before = *scene.session.document();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        scene.session.beginInteraction(QStringLiteral("Shape Builder"));
        QVERIFY(scene.session.previewShapeBuild(arrangement, {{scene.regionAt(arrangement, {75, 50})}, {}, true}));
        QVERIFY(*scene.session.document() != before);
        scene.session.cancelInteraction();
        QVERIFY(*scene.session.document() == before);
        QCOMPARE(scene.session.undoName(), QStringLiteral("Fill"));
    }

    void emptyGesturesChangeNothing()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.add(Shapes::rectangle({200, 200, 50, 50}), Qt::blue);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(!scene.gesture({}));
        // Merging a lone shape's only region is no change either.
        QVERIFY(!scene.gesture(merging({scene.regionAt(arrangement, {50, 50})})));
        QCOMPARE(scene.session.undoName(), QStringLiteral("Fill"));
        QCOMPARE(scene.paths().size(), size_t(2));
        // Outside the selection nothing is offered.
        scene.session.deselectAll();
        QVERIFY(scene.arrangement().regions.empty());
    }

    void openPathsCutRegionsAndSplitIntoEdges()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        const QUuid line = scene.session.addPath(Shapes::line({-20, 50}, {120, 50}), QStringLiteral("Line"));
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QCOMPARE(arrangement.regions.size(), size_t(2));
        QCOMPARE(arrangement.edges.size(), size_t(3));
        const std::optional<int> middle = arrangement.edgeAt({50, 50.5}, 2);
        QVERIFY(middle);
        QVERIFY(near(VectorPath{{arrangement.edges[size_t(*middle)].path}, Qt::WindingFill}.bounds(), QRectF(0, 50, 100, 0)));
        // Alt on the middle edge deletes that piece; the ends stay as two paths.
        QVERIFY(scene.gesture({{}, {*middle}, true, true}));
        QVERIFY(!scene.session.document()->find(line));
        int lines = 0;
        for (const VectorObject *object : scene.paths()) {
            if (!object->path.contours.front().closed) {
                ++lines;
                QVERIFY(near(object->path.bounds().width(), 20, 0.5));
            }
        }
        QCOMPARE(lines, 2);
    }

    void aClickSeparatesAHalfAnOpenPathCut()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.session.addPath(Shapes::line({-20, 50}, {120, 50}), QStringLiteral("Line"));
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(scene.gesture({{scene.regionAt(arrangement, {50, 25})}, {}, false, true}));
        QVERIFY(scene.pathAt({50, 25}) != scene.pathAt({50, 75}));
        QVERIFY(near(scene.pathAt({50, 25})->path.bounds(), QRectF(0, 0, 100, 50)));
        QVERIFY(near(scene.pathAt({50, 75})->path.bounds(), QRectF(0, 50, 100, 50)));
        // The halves merge back into the square.
        const ShapeBuilder::Arrangement halves = scene.arrangement();
        QVERIFY(scene.gesture(merging({scene.regionAt(halves, {50, 25}), scene.regionAt(halves, {50, 75})})));
        QVERIFY(near(scene.pathAt({50, 25})->path.bounds(), QRectF(0, 0, 100, 100)));
        QVERIFY(near(areaOf(scene.pathAt({50, 25})->path), 10000, 50));
    }

    void clickingAStrokeSplitsItWhenAsked()
    {
        Scene scene;
        scene.add(Shapes::rectangle({0, 0, 100, 100}), Qt::red);
        scene.session.addPath(Shapes::line({-20, 50}, {120, 50}), QStringLiteral("Line"));
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        const int middle = *arrangement.edgeAt({50, 50}, 2);
        QVERIFY(!scene.gesture({{}, {middle}, false, true}));
        scene.session.shapeBuilder.clickingStrokeSplits = true;
        QVERIFY(scene.gesture({{}, {middle}, false, true}));
        int lines = 0;
        for (const VectorObject *object : scene.paths())
            lines += object->path.contours.front().closed ? 0 : 1;
        QCOMPARE(lines, 3);
    }

    void gapDetectionClosesNearlyClosedPaths()
    {
        Scene scene;
        VectorPath open = Shapes::rectangle({0, 0, 100, 100});
        open.contours.front().closed = false;
        open.contours.front().nodes.back().anchor = QPointF(0, 2);
        open.contours.front().nodes.back().in = open.contours.front().nodes.back().out = QPointF(0, 2);
        scene.add(open, Qt::red);
        scene.session.selectAll();
        QVERIFY(scene.arrangement().regions.empty());
        scene.session.shapeBuilder.gapDetection = true;
        scene.session.shapeBuilder.gapLength = 3;
        QCOMPARE(scene.arrangement().regions.size(), size_t(1));
    }

    void tooManyPathsFallBackToNothing()
    {
        Scene scene;
        for (int index = 0; index <= ShapeBuilder::maximumSources; ++index)
            scene.add(Shapes::rectangle({double(index * 5), 0, 20, 20}), Qt::red);
        scene.session.selectAll();
        const ShapeBuilder::Arrangement arrangement = scene.arrangement();
        QVERIFY(arrangement.truncated);
        QVERIFY(!scene.gesture(merging({0})));
    }
};

QTEST_MAIN(ShapeBuilderTests)
#include "ShapeBuilderTests.moc"
