#include "Canvas/SmartGuides.h"
#include "Document/PathOperations.h"
#include <QTest>

class SmartGuidesTests : public QObject {
    Q_OBJECT

private:
    static VectorDocument documentWith(const std::vector<QRectF> &rects, std::vector<QUuid> *ids = nullptr)
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        const QUuid layer = document.layers().back();
        for (const QRectF &rect : rects) {
            VectorObject object;
            object.path = Shapes::rectangle(rect);
            if (ids)
                ids->push_back(object.id);
            document.insert(object, layer);
        }
        return document;
    }

private slots:
    void movingBoundsSnapToAnObjectsEdge()
    {
        const SmartGuides guides(documentWith({{230, 50, 50, 50}}), {});
        // Three points off at zoom 1: within the six-point reach.
        const auto result = guides.movement({50, 160, 40, 40}, {177, 0}, 1, false);
        QCOMPARE(result.delta.x(), 180.0);
        QVERIFY(result.snappedX);
        QVERIFY(!result.lines.empty());
        QCOMPARE(result.lines.front().x1(), 230.0);
    }

    void toleranceIsInScreenPoints()
    {
        const SmartGuides guides(documentWith({{230, 50, 50, 50}}), {});
        // At 4x the same three units are twelve screen points: too far.
        const auto result = guides.movement({50, 160, 40, 40}, {177, 0}, 4, false);
        QCOMPARE(result.delta.x(), 177.0);
        QVERIFY(!result.snappedX);
    }

    void centresSnapToTheArtboardsCentre()
    {
        const SmartGuides guides(documentWith({}), {});
        const auto result = guides.movement({0, 0, 20, 20}, {188, 137}, 1, false);
        QCOMPARE(result.delta, QPointF(190, 140));
    }

    void excludedObjectsAreNoTargets()
    {
        std::vector<QUuid> ids;
        const VectorDocument document = documentWith({{230, 50, 50, 50}}, &ids);
        const SmartGuides guides(document, ids);
        QVERIFY(guides.objects().empty());
    }

    void equalSpacingBetweenNeighbours()
    {
        // Two boxes 20 apart; a third after them snaps 20 on.
        const SmartGuides guides(documentWith({{10, 100, 20, 20}, {50, 100, 20, 20}}), {});
        const auto result = guides.movement({0, 100, 20, 20}, {88, 0}, 1, false);
        QCOMPARE(result.delta.x(), 90.0);
        QCOMPARE(result.gaps.size(), size_t(2));
    }

    void pointsSnapAndConstrain()
    {
        const SmartGuides guides(documentWith({{100, 100, 50, 50}}), {});
        const auto point = guides.point({98, 40}, 1);
        QCOMPARE(point.delta, QPointF(100, 40));
        QCOMPARE(constrain45({10, 9}), QPointF(9.5, 9.5));
        QCOMPARE(constrain45({10, 1}), QPointF(10, 0));
    }
};

QTEST_MAIN(SmartGuidesTests)
#include "SmartGuidesTests.moc"
