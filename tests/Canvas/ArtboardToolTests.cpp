#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QTest>

// The Artboard tool (Shift-O): drawing, moving, resizing, duplicating and deleting artboards
// on the canvas (docs/QOL-RESEARCH.md P2-1).
namespace {
struct Fixture {
    EditorSession session;
    EditorCanvas canvas{session};

    Fixture()
    {
        session.createDocument({200, 200});
        session.usesSmartGuides = false;
        canvas.resize(1200, 900);
        canvas.show();
        if (!QTest::qWaitForWindowExposed(&canvas))
            qWarning("canvas never exposed");
        session.actualSize();
        canvas.setFocus();
        session.selectTool(Tool::artboard);
    }

    QPoint view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size).toPoint(); }

    void press(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, view(at));
    }
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }
    void release(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, view(at));
    }
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        press(from, modifiers);
        for (int step = 1; step <= 4; ++step)
            move(from + (to - from) * step / 4.0, modifiers);
        release(to, modifiers);
    }
};
}

class ArtboardToolTests : public QObject {
    Q_OBJECT

private slots:
    void shiftOSelectsTheArtboardTool()
    {
        EditorSession session;
        EditorCanvas canvas(session);
        session.createDocument({200, 200});
        QTest::keyClick(&canvas, Qt::Key_O, Qt::ShiftModifier);
        QCOMPARE(session.tool(), Tool::artboard);
    }

    void dragOnEmptyCanvasDrawsANewArtboard()
    {
        Fixture f;
        f.drag({300, 20}, {450, 120});
        QCOMPARE(f.session.document()->artboardCount(), 2);
        const QRectF rect = f.session.document()->artboard(1).rect;
        QVERIFY2(QLineF(rect.topLeft(), QPointF(300, 20)).length() < 1, qPrintable(QString::number(rect.left())));
        QVERIFY(std::abs(rect.width() - 150) < 1 && std::abs(rect.height() - 100) < 1);
        QCOMPARE(f.session.activeArtboard(), 1);
    }

    void clickInsideAnArtboardActivatesItWithoutAddingOne()
    {
        Fixture f;
        f.drag({300, 20}, {450, 120});
        QCOMPARE(f.session.document()->artboardCount(), 2);
        f.press({50, 50});
        f.release({50, 50});
        QCOMPARE(f.session.document()->artboardCount(), 2);
        QCOMPARE(f.session.activeArtboard(), 0);
    }

    void draggingItsBodyMovesItAndItsArtWithIt()
    {
        Fixture f;
        f.session.selectTool(Tool::select);
        const QUuid rect = f.session.addPath(Shapes::rectangle({50, 50, 20, 20}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::artboard);
        QVERIFY(f.session.artboardMovesArt);
        f.drag({100, 100}, {130, 140});
        const QRectF board = f.session.document()->artboard(0).rect;
        QCOMPARE(board.topLeft(), QPointF(30, 40));
        QCOMPARE(f.session.document()->bounds(rect).topLeft(), QPointF(80, 90));
        QCOMPARE(f.session.undoName(), QStringLiteral("Move Artboard"));
    }

    void draggingACornerHandleResizesIt()
    {
        Fixture f;
        // The bottom-right handle, on the artboard's own corner.
        f.drag({200, 200}, {260, 240});
        const QRectF board = f.session.document()->artboard(0).rect;
        QCOMPARE(board.topLeft(), QPointF(0, 0));
        QCOMPARE(board.bottomRight(), QPointF(260, 240));
        QCOMPARE(f.session.undoName(), QStringLiteral("Resize Artboard"));
    }

    void altDragDuplicatesIt()
    {
        Fixture f;
        f.drag({100, 100}, {130, 100}, Qt::AltModifier);
        QCOMPARE(f.session.document()->artboardCount(), 2);
        QVERIFY(f.session.document()->artboard(0).id != f.session.document()->artboard(1).id);
    }

    void deleteRemovesTheActiveArtboardButNeverTheLast()
    {
        Fixture f;
        f.drag({300, 20}, {450, 120});
        QCOMPARE(f.session.document()->artboardCount(), 2);
        QCOMPARE(f.session.activeArtboard(), 1);
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QCOMPARE(f.session.document()->artboardCount(), 1);
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QCOMPARE(f.session.document()->artboardCount(), 1);
    }
};

QTEST_MAIN(ArtboardToolTests)
#include "ArtboardToolTests.moc"
