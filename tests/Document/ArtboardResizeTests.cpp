#include "Document/EditorSession.h"
#include <QTest>

// An artboard resized like a Figma frame: its art follows its constraints, and moves with it.
namespace {
struct Board {
    EditorSession session;
    QUuid corner;
    QUuid edge;
    QUuid outside;

    // A 200 pt artboard with a square hugging its right side and one far off it.
    Board()
    {
        session.createDocument({200, 200});
        corner = session.addPath(Shapes::rectangle({10, 10, 20, 20}), QStringLiteral("Corner"));
        edge = session.addPath(Shapes::rectangle({170, 80, 20, 20}), QStringLiteral("Edge"));
        outside = session.addPath(Shapes::rectangle({500, 500, 20, 20}), QStringLiteral("Outside"));
        session.deselectAll();
    }
    const VectorDocument &document() const { return *session.document(); }
    QRectF bounds(const QUuid &id) const { return document().bounds(id); }
    void resize(QRectF rect)
    {
        session.beginInteraction(QStringLiteral("Resize Artboard"));
        session.previewArtboardRect(0, rect);
        session.commitInteraction();
    }
};
}

class ArtboardResizeTests : public QObject {
    Q_OBJECT

private slots:
    void byDefaultArtFollowsTheTopLeftCorner()
    {
        Board board;
        // Growing from the bottom right leaves left-and-top art where it is.
        board.resize({0, 0, 300, 300});
        QCOMPARE(board.bounds(board.corner), QRectF(10, 10, 20, 20));
        QCOMPARE(board.bounds(board.edge), QRectF(170, 80, 20, 20));
        // Dragging the top-left corner in carries it, as a frame's Left and Top children go.
        board.resize({50, 40, 250, 260});
        QCOMPARE(board.bounds(board.corner), QRectF(60, 50, 20, 20));
    }

    void constraintsPlaceArtOnAResize()
    {
        Board board;
        board.session.select({board.edge});
        board.session.setConstraint(Qt::Horizontal, LayoutConstraint::end);
        board.session.setConstraint(Qt::Vertical, LayoutConstraint::center);
        board.session.select({board.corner});
        board.session.setConstraint(Qt::Horizontal, LayoutConstraint::scale);
        board.session.deselectAll();
        board.resize({0, 0, 400, 300});
        QCOMPARE(board.bounds(board.edge), QRectF(370, 130, 20, 20));
        // Scale stretches with the artboard, from its left edge.
        QCOMPARE(board.bounds(board.corner), QRectF(20, 10, 40, 20));
    }

    void artOffTheArtboardStaysPut()
    {
        Board board;
        board.resize({50, 50, 250, 250});
        QCOMPARE(board.bounds(board.outside), QRectF(500, 500, 20, 20));
    }

    void movingItCarriesItsArtWhateverTheConstraints()
    {
        Board board;
        board.session.select({board.edge});
        board.session.setConstraint(Qt::Horizontal, LayoutConstraint::end);
        board.session.deselectAll();
        board.resize({30, 40, 200, 200});
        QCOMPARE(board.bounds(board.edge), QRectF(200, 120, 20, 20));
        QCOMPARE(board.bounds(board.corner), QRectF(40, 50, 20, 20));
        QCOMPARE(board.document().artboard(0).rect, QRectF(30, 40, 200, 200));
        QCOMPARE(board.session.undoName(), QStringLiteral("Resize Artboard"));
    }

    void withoutMoveArtWithArtboardTheArtStays()
    {
        Board board;
        board.session.artboardMovesArt = false;
        board.resize({30, 40, 330, 340});
        QCOMPARE(board.bounds(board.corner), QRectF(10, 10, 20, 20));
    }

    void widthAndHeightFollowTheConstraintsToo()
    {
        Board board;
        board.session.select({board.edge});
        board.session.setConstraint(Qt::Horizontal, LayoutConstraint::end);
        board.session.deselectAll();
        board.session.setArtboardSize({260, 200});
        QCOMPARE(board.bounds(board.edge), QRectF(230, 80, 20, 20));
        QCOMPARE(board.session.undoName(), QStringLiteral("Artboard Size"));
        board.session.undo();
        QCOMPARE(board.bounds(board.edge), QRectF(170, 80, 20, 20));
        QCOMPARE(board.document().size, QSizeF(200, 200));
    }

    void anArtboardIsSelectedUntilSomethingElseIs()
    {
        Board board;
        board.session.addArtboard({300, 0, 100, 100});
        board.session.selectArtboard(1);
        QVERIFY(board.session.artboardSelected());
        QCOMPARE(board.session.activeArtboard(), 1);
        QVERIFY(!board.session.hasSelection());
        board.session.select({board.corner});
        QVERIFY(!board.session.artboardSelected());
        board.session.selectArtboard(0);
        QVERIFY(board.session.artboardSelected());
        QVERIFY(!board.session.hasSelection());
        board.session.deselectAll();
        QVERIFY(!board.session.artboardSelected());
    }
};

QTEST_MAIN(ArtboardResizeTests)
#include "ArtboardResizeTests.moc"
