#include "Document/EditorSession.h"
#include <QTest>

// Resizing a frame never scales its children (Figma's rule): type keeps its size and follows its constraints.
namespace {
// About `pivot`, as Properties' W and H and the canvas handles scale.
QTransform about(QPointF pivot, double sx, double sy)
{
    return QTransform::fromTranslate(-pivot.x(), -pivot.y()) * QTransform::fromScale(sx, sy) * QTransform::fromTranslate(pivot.x(), pivot.y());
}

struct Board {
    EditorSession session;
    QUuid frame;
    QUuid point;
    QUuid area;
    QUuid box;
    QUuid group;
    double pointSize = 0;

    // A 200 × 150 frame at (100, 100) holding point type, area type and a square; `translated` builds its box the way
    // importers do, at the origin and moved by its placement. `grouped` puts all three in a group first.
    explicit Board(bool translated = false, bool grouped = false)
    {
        session.createDocument({600, 600});
        frame = session.addFrame({100, 100, 200, 150});
        VectorObject words = session.textObject({120, 140}, QStringLiteral("Point"));
        pointSize = words.text.size;
        point = words.id;
        VectorObject wrapped = session.textObject({0, 0}, QStringLiteral("Area type that wraps"));
        wrapped.text.area = QSizeF(100, 0);
        wrapped.transform = QTransform::fromTranslate(120, 180);
        area = wrapped.id;
        box = session.addPath(Shapes::rectangle({240, 120, 40, 40}), QStringLiteral("Square"));
        edit([&](VectorDocument &document) {
            document.insert(words, frame);
            document.insert(wrapped, frame);
            document.move(box, frame, -1);
            if (translated) {
                VectorObject *object = document.find(frame);
                object->shape->rect = QRectF(0, 0, 200, 150);
                object->shape->placement = QTransform::fromTranslate(100, 100);
                object->path = object->shape->path();
            }
        });
        if (grouped) {
            session.select({point, area, box});
            session.groupSelection();
            group = session.selection().front();
            session.setConstraint(Qt::Horizontal, LayoutConstraint::scale);
            session.setConstraint(Qt::Vertical, LayoutConstraint::scale);
        }
        session.select({frame});
    }
    template <typename Change> void edit(Change change)
    {
        VectorDocument document = *session.document();
        change(document);
        session.loadDocument(document);
    }
    const VectorObject &read(const QUuid &id) const { return *session.document()->find(id); }
    QRectF frameBox() const { return session.document()->bounds(frame); }

    // Neither piece of type grew or shrank its glyphs.
    bool typeKeptItsSize() const
    {
        const VectorObject &p = read(point), &a = read(area);
        return p.text.size == pointSize && a.text.size == pointSize && p.transform.type() <= QTransform::TxTranslate
            && a.transform.type() <= QTransform::TxTranslate;
    }
    void resize(double sx, double sy, bool reflow = true)
    {
        session.transformSelection(about(frameBox().topLeft(), sx, sy), QStringLiteral("Scale"), reflow);
    }
};
}

class FrameResizeTests : public QObject {
    Q_OBJECT

private slots:
    void widthAndHeightKeepTypeInAFrame()
    {
        Board board;
        board.resize(1.5, 2);
        QCOMPARE(board.frameBox(), QRectF(100, 100, 300, 300));
        QVERIFY(board.typeKeptItsSize());
        QCOMPARE(board.read(board.area).text.area->width(), 100.0);
        QVERIFY(QLineF(board.read(board.point).transform.map(QPointF()), QPointF(120, 140)).length() < 1e-6);
    }

    void anImportedFramesTranslatedBoxResizesByConstraints()
    {
        Board board(true);
        board.resize(1.5, 2);
        QCOMPARE(board.frameBox(), QRectF(100, 100, 300, 300));
        QVERIFY(board.read(board.frame).shape->placement.isIdentity());
        QVERIFY(board.typeKeptItsSize());
        QCOMPARE(board.session.document()->bounds(board.box).size(), QSizeF(40, 40));
    }

    void typeInAGroupInAFrameGetsABoxNotBiggerGlyphs()
    {
        Board board(false, true);
        board.resize(2, 1);
        QVERIFY(board.typeKeptItsSize());
        // The group scales its children into its new box: the square stretches, area type rewraps wider.
        QCOMPARE(board.session.document()->bounds(board.box).width(), 80.0);
        QVERIFY(board.read(board.area).text.area->width() > 150);
        QCOMPARE(board.read(board.box).parentID, board.group);
    }

    void aSelectedChildMovesAndResizesOnceWithItsFrame()
    {
        Board board;
        board.session.select({board.frame, board.point, board.box});
        board.resize(2, 2);
        QVERIFY(board.typeKeptItsSize());
        QCOMPARE(board.session.document()->bounds(board.box).size(), QSizeF(40, 40));
        board.session.undo();
        board.session.select({board.frame, board.point});
        board.session.transformSelection(QTransform::fromTranslate(10, 0), QStringLiteral("Move"));
        QVERIFY(QLineF(board.read(board.point).transform.map(QPointF()), QPointF(130, 140)).length() < 1e-6);
    }

    void aHandlePulledPastTheOppositeSideFlipsNothing()
    {
        Board board;
        // The right side dragged 100 past the left: the box now runs from 0 to 100.
        board.resize(-0.5, 1);
        QCOMPARE(board.frameBox(), QRectF(0, 100, 100, 150));
        QVERIFY(board.typeKeptItsSize());
        QCOMPARE(board.read(board.frame).shape->placement, QTransform());
    }

    void transformAgainRepeatsABoxResize()
    {
        Board board;
        board.resize(1.5, 1.5);
        board.session.transformAgain();
        QCOMPARE(board.session.undoName(), QStringLiteral("Transform Again"));
        QVERIFY(board.typeKeptItsSize());
        // A handle drag's resize repeats as one too.
        Board dragged;
        dragged.session.beginInteraction(QStringLiteral("Scale"));
        dragged.session.previewTransform(about(dragged.frameBox().topLeft(), 1.5, 1), true);
        dragged.session.commitInteraction();
        dragged.session.transformAgain();
        QCOMPARE(dragged.frameBox().width(), 450.0);
        QVERIFY(dragged.typeKeptItsSize());
    }

    void theScaleToolStillScalesEverything()
    {
        Board board;
        board.resize(2, 2, false);
        QCOMPARE(board.read(board.point).transform.m11(), 2.0);
    }
};

QTEST_MAIN(FrameResizeTests)
#include "FrameResizeTests.moc"
