#include "Document/EditorSession.h"
#include <QTest>

// Object ▸ Path: Join, Average, Scissors, Reverse Path Direction, and the fill rule.
namespace {
VectorPath polyline(std::initializer_list<QPointF> points, bool closed = false)
{
    Contour contour;
    contour.closed = closed;
    for (const QPointF point : points)
        contour.nodes.emplace_back(point);
    VectorPath path;
    path.contours.push_back(contour);
    return path;
}

struct Fixture {
    EditorSession session;
    Fixture() { session.createDocument({400, 300}); }
    QUuid add(const VectorPath &path) { return session.addPath(path, QStringLiteral("Path")); }
    const VectorObject *object(const QUuid &id) const { return session.document()->find(id); }
    int paths() const
    {
        int count = 0;
        for (const VectorObject &object : session.document()->objects)
            count += object.kind == ObjectKind::path;
        return count;
    }
};
}

class PathEditingTests : public QObject {
    Q_OBJECT

private slots:
    void joiningTwoOpenPathsMakesOne()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}}));
        const QUuid b = f.add(polyline({{100, 0}, {60, 0}}));
        f.session.select({a, b});
        QVERIFY(f.session.canJoin());
        f.session.joinPaths();
        QCOMPARE(f.session.undoName(), QString("Join"));
        QCOMPARE(f.paths(), 1);
        const VectorPath &joined = f.object(a)->path;
        QCOMPARE(joined.contours.size(), size_t(1));
        QVERIFY(!joined.contours.front().closed);
        // Nearest ends meet with a straight segment: 50 to 60.
        std::vector<QPointF> anchors;
        for (const PathNode &node : joined.contours.front().nodes)
            anchors.push_back(node.anchor);
        QCOMPARE(anchors, (std::vector<QPointF>{{0, 0}, {50, 0}, {60, 0}, {100, 0}}));
        f.session.undo();
        QCOMPARE(f.paths(), 2);
    }

    void coincidingEndsMerge()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}}));
        const QUuid b = f.add(polyline({{50, 0}, {100, 50}}));
        f.session.select({a, b});
        f.session.joinPaths();
        QCOMPARE(f.object(a)->path.nodeCount(), 3);
        QVERIFY(!f.object(b));
    }

    void oneOpenPathCloses()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}, {50, 50}}));
        f.session.select({a});
        f.session.joinPaths();
        QVERIFY(f.object(a)->path.contours.front().closed);
        QCOMPARE(f.object(a)->path.nodeCount(), 3);
        QVERIFY(!f.session.canJoin());
    }

    void pickedEndsJoinWithDirectSelection()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}, {50, 50}}));
        const QUuid b = f.add(polyline({{200, 0}, {150, 0}, {150, 50}}));
        f.session.selectTool(Tool::directSelect);
        f.session.select({a, b});
        // The first anchor of one and the first of the other, not the nearest pair.
        f.session.pickNodes({{a, {0, 0}}, {b, {0, 0}}});
        QVERIFY(f.session.canJoin());
        f.session.joinPaths();
        QCOMPARE(f.paths(), 1);
        const auto &nodes = f.object(a)->path.contours.front().nodes;
        QCOMPARE(nodes.size(), size_t(6));
        QCOMPARE(nodes.front().anchor, QPointF(50, 50));
        QCOMPARE(nodes[2].anchor, QPointF(0, 0));
        QCOMPARE(nodes[3].anchor, QPointF(200, 0));
    }

    void averageLinesAnchorsUp()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {40, 20}, {80, 100}}));
        f.session.selectTool(Tool::directSelect);
        f.session.select({a});
        f.session.pickNodes({{a, {0, 0}}, {a, {0, 1}}});
        QVERIFY(f.session.canAverage());
        f.session.averagePoints(Qt::Horizontal);
        QCOMPARE(f.session.undoName(), QString("Average"));
        const auto &nodes = f.object(a)->path.contours.front().nodes;
        QCOMPARE(nodes[0].anchor, QPointF(0, 10));
        QCOMPARE(nodes[1].anchor, QPointF(40, 10));
        // Unpicked anchors stay.
        QCOMPARE(nodes[2].anchor, QPointF(80, 100));
        f.session.averagePoints(Qt::Horizontal | Qt::Vertical);
        QCOMPARE(f.object(a)->path.contours.front().nodes[0].anchor, QPointF(20, 10));
        QCOMPARE(f.object(a)->path.contours.front().nodes[1].anchor, QPointF(20, 10));
    }

    void scissorsOpenAClosedPathAtTheClick()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true));
        // Halfway along the top edge.
        QVERIFY(f.session.cutPath(a, {0, 0}, 0.5));
        QCOMPARE(f.session.undoName(), QString("Cut Path"));
        QCOMPARE(f.paths(), 1);
        const Contour &contour = f.object(a)->path.contours.front();
        QVERIFY(!contour.closed);
        QCOMPARE(contour.nodes.front().anchor, QPointF(50, 0));
        QCOMPARE(contour.nodes.back().anchor, QPointF(50, 0));
        QCOMPARE(contour.nodes.size(), size_t(6));
        QCOMPARE(f.object(a)->path.bounds(), QRectF(0, 0, 100, 100));
    }

    void scissorsSplitAnOpenPathInTwo()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}, {100, 0}}));
        QVERIFY(f.session.cutPath(a, {0, 1}));
        QCOMPARE(f.paths(), 2);
        QCOMPARE(f.session.selection().size(), size_t(2));
        const QUuid b = f.session.selection().back();
        QCOMPARE(f.object(a)->path.contours.front().nodes.back().anchor, QPointF(50, 0));
        QCOMPARE(f.object(b)->path.contours.front().nodes.front().anchor, QPointF(50, 0));
        QCOMPARE(f.object(b)->path.contours.front().nodes.back().anchor, QPointF(100, 0));
        // An end has nothing to cut.
        QVERIFY(!f.session.cutPath(a, {0, 0}));
        f.session.undo();
        QCOMPARE(f.paths(), 1);
    }

    void reverseTurnsTheDirection()
    {
        Fixture f;
        const QUuid a = f.add(polyline({{0, 0}, {50, 0}, {50, 50}}));
        f.session.select({a});
        f.session.reversePaths();
        QCOMPARE(f.session.undoName(), QString("Reverse Path Direction"));
        QCOMPARE(f.object(a)->path.contours.front().nodes.front().anchor, QPointF(50, 50));
        QCOMPARE(f.object(a)->path.contours.front().nodes.back().anchor, QPointF(0, 0));
    }

    void compoundPathsTakeAFillRule()
    {
        Fixture f;
        const QUuid outer = f.add(polyline({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, true));
        const QUuid inner = f.add(polyline({{25, 25}, {75, 25}, {75, 75}, {25, 75}}, true));
        f.session.select({outer});
        QVERIFY(f.session.selectedCompoundPaths().empty());
        f.session.select({outer, inner});
        f.session.makeCompoundPath();
        const std::vector<QUuid> compound = f.session.selectedCompoundPaths();
        QCOMPARE(compound.size(), size_t(1));
        f.session.setFillRuleOfSelection(Qt::WindingFill);
        QCOMPARE(f.object(compound.front())->path.fillRule, Qt::WindingFill);
        QCOMPARE(f.session.undoName(), QString("Fill Rule"));
        f.session.setFillRuleOfSelection(Qt::OddEvenFill);
        QCOMPARE(f.object(compound.front())->path.fillRule, Qt::OddEvenFill);
        QVERIFY(!f.object(compound.front())->path.painterPath().contains(QPointF(50, 50)));
    }
};

QTEST_MAIN(PathEditingTests)
#include "PathEditingTests.moc"
