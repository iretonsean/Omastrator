#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QJsonDocument>
#include <QTest>

// Live corners: rectangles keep their corners until an anchor is edited.
namespace {
VectorObject rectangle(const QRectF &rect, double radius = 0)
{
    LiveRectangle shape;
    shape.rect = rect;
    shape.radii.fill(radius);
    VectorObject object;
    object.kind = ObjectKind::path;
    object.fill = Paint::solid(Qt::black);
    EditorSession::reshape(object, shape);
    return object;
}

struct Fixture {
    EditorSession session;
    QUuid id;

    explicit Fixture(double radius = 0)
    {
        session.createDocument({400, 300});
        id = session.addObject(rectangle(QRectF(50, 50, 100, 60), radius), QStringLiteral("Draw Rectangle"));
    }
    const VectorObject &object() const { return *session.document()->find(id); }
};

bool near(QPointF a, QPointF b)
{
    return QLineF(a, b).length() < 1e-6;
}
}

class LiveCornersTests : public QObject {
    Q_OBJECT

private slots:
    void theShapeMakesItsPath()
    {
        const VectorObject square = rectangle(QRectF(10, 10, 100, 50));
        QVERIFY(square.liveShape());
        QCOMPARE(square.path.nodeCount(), 4);
        QCOMPARE(square.path.bounds(), QRectF(10, 10, 100, 50));
        const VectorObject rounded = rectangle(QRectF(10, 10, 100, 50), 8);
        QCOMPARE(rounded.path.nodeCount(), 8);
        QCOMPARE(rounded.path.bounds(), QRectF(10, 10, 100, 50));
        // It starts on the top edge, as the Rectangle tool's paths did.
        QVERIFY(near(rounded.path.contours.front().nodes.front().anchor, QPointF(18, 10)));
        // A radius past half the short side is clamped.
        QCOMPARE(rectangle(QRectF(0, 0, 100, 50), 80).liveShape()->effectiveRadius(0), 25.0);
    }

    void oneCornerLeavesTheOtherThree()
    {
        Fixture f;
        f.session.select({f.id});
        QCOMPARE(f.session.selectedShapes(), std::vector<QUuid>{f.id});
        f.session.setCornerRadius(12, 1);
        QCOMPARE(f.session.undoName(), QString("Corner Radius"));
        const LiveRectangle *shape = f.object().liveShape();
        QVERIFY(shape);
        QCOMPARE(shape->radii, (std::array<double, 4>{0, 12, 0, 0}));
        // Three square corners and one rounded one: five anchors.
        QCOMPARE(f.object().path.nodeCount(), 5);
        QCOMPARE(f.object().path.bounds(), QRectF(50, 50, 100, 60));
        f.session.setCornerRadius(6);
        QCOMPARE(f.object().liveShape()->radii, (std::array<double, 4>{6, 6, 6, 6}));
        f.session.undo();
        QCOMPARE(f.object().liveShape()->radii, (std::array<double, 4>{0, 12, 0, 0}));
    }

    void stylesTurnTheCorner()
    {
        Fixture f(20);
        f.session.select({f.id});
        // Just inside a rounded corner's arc, and inside the circle an inverted corner cuts away.
        const QPointF probe(50 + 20 * (1 - 1 / std::sqrt(2.0)) + 1.5, 50 + 20 * (1 - 1 / std::sqrt(2.0)) + 1.5);
        QVERIFY(f.object().path.painterPath().contains(probe));
        f.session.setCornerStyle(CornerStyle::inverted);
        QCOMPARE(f.session.undoName(), QString("Corner Style"));
        QVERIFY(!f.object().path.painterPath().contains(probe));
        QVERIFY(f.object().path.painterPath().contains(QPointF(100, 80)));
        // Chamfers are straight cuts: no handles anywhere.
        f.session.setCornerStyle(CornerStyle::chamfer, 0);
        const PathNode &first = f.object().path.contours.front().nodes.back();
        QVERIFY(!first.hasIn() && !first.hasOut());
        QCOMPARE(f.object().liveShape()->styles[0], CornerStyle::chamfer);
        QCOMPARE(f.object().liveShape()->styles[1], CornerStyle::inverted);
    }

    void movingAndTurningKeepItLive()
    {
        Fixture f(10);
        f.session.select({f.id});
        f.session.moveSelection({20, 5});
        QVERIFY(f.object().liveShape());
        QCOMPARE(f.object().path.bounds(), QRectF(70, 55, 100, 60));
        f.session.rotateSelection(30);
        const LiveRectangle *turned = f.object().liveShape();
        QVERIFY(turned);
        QCOMPARE(turned->rect.size(), QSizeF(100, 60));
        QCOMPARE(turned->radii[0], 10.0);
        // A uniform scale scales the corners with it (Scale Corners).
        f.session.undo();
        f.session.scaleSelection(2, 2);
        QVERIFY(f.object().liveShape());
        QCOMPARE(f.object().liveShape()->radii[2], 20.0);
        // Stretched across a turned box, it's skewed: a plain path from then on.
        f.session.undo();
        f.session.rotateSelection(30);
        f.session.scaleSelection(2, 1);
        QVERIFY(!f.object().liveShape());
        QVERIFY(!f.object().shape);
    }

    void scaleCornersOffKeepsRadiiClamped()
    {
        Fixture f(10);
        f.session.select({f.id});
        f.session.scaleCorners = false;
        f.session.scaleSelection(2, 2);
        QVERIFY(f.object().liveShape());
        // Off: the radius stays put.
        QCOMPARE(f.object().liveShape()->radii[0], 10.0);
        // A shrink past twice the radius clamps it to half the shorter side.
        f.session.scaleSelection(0.05, 0.05);
        QVERIFY(f.object().liveShape());
        const QRectF box = f.object().liveShape()->rect.normalized();
        QCOMPARE(f.object().liveShape()->effectiveRadius(0), std::min(box.width(), box.height()) / 2);
    }

    void editingAnAnchorExpandsTheShape()
    {
        Fixture f(10);
        VectorObject edited = f.object();
        edited.path.contours.front().nodes.front().translate({0, -15});
        f.session.updateObject(edited, QStringLiteral("Move Points"));
        QVERIFY(!f.object().liveShape());
        // Expanded for good, as Illustrator's Expand Shape.
        QVERIFY(!f.object().shape);
        f.session.select({f.id});
        QVERIFY(f.session.selectedShapes().empty());
        f.session.undo();
        QVERIFY(f.object().liveShape());
    }

    void liveShapesSaveInTheFile()
    {
        Fixture f(10);
        f.session.select({f.id});
        f.session.setCornerRadius(24, 3);
        f.session.setCornerStyle(CornerStyle::chamfer, 2);
        f.session.rotateSelection(15);
        // Through text, as a file holds it.
        const QByteArray text = QJsonDocument(DocumentCodec::encode(*f.session.document())).toJson();
        const VectorDocument loaded = DocumentCodec::decode(QJsonDocument::fromJson(text).object());
        const VectorObject *object = loaded.find(f.id);
        QVERIFY(object && object->liveShape());
        QCOMPARE(object->shape->radii, f.object().shape->radii);
        QCOMPARE(object->shape->styles, f.object().shape->styles);
        QCOMPARE(object->path, f.object().path);
        // A shape that no longer matches its path isn't kept.
        VectorObject stale = f.object();
        stale.path.contours.front().nodes.front().translate({3, 3});
        QVERIFY(!DocumentCodec::encode(stale).contains("shape"));
        QVERIFY(!DocumentCodec::decodeObject(DocumentCodec::encode(stale)).shape);
    }

    void copiesStayLive()
    {
        Fixture f(10);
        f.session.select({f.id});
        f.session.duplicateSelection();
        const QUuid copy = f.session.selection().front();
        QVERIFY(copy != f.id);
        QVERIFY(f.session.document()->find(copy)->liveShape());
    }
};

QTEST_MAIN(LiveCornersTests)
#include "LiveCornersTests.moc"
