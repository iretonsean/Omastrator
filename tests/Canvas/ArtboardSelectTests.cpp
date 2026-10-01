#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QTest>

// Artboards on the Select tool, as Figma's frames: the name selects, the handles resize, a drag
// on the name moves it (with its art), each drag one undo step.
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
        session.selectTool(Tool::select);
    }

    const VectorDocument &document() const { return *session.document(); }
    QPoint view(QPointF point) const { return session.viewport.viewPoint(point, session.document()->size).toPoint(); }
    // Somewhere on artboard `index`'s name, which sits just above its top left corner.
    QPointF nameOf(int index) const { return document().artboard(index).rect.topLeft() + QPointF(10, -12); }

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
    void click(QPointF at) { drag(at, at); }
    // Selects artboard 0 through its name.
    void selectArtboard()
    {
        click(nameOf(0));
        QVERIFY(session.artboardSelected());
    }
};
}

class ArtboardSelectTests : public QObject {
    Q_OBJECT

private slots:
    void clickingTheNameSelectsTheArtboardAndShowsHandles()
    {
        Fixture f;
        QVERIFY(!f.session.artboardSelected());
        f.click(f.nameOf(0));
        QVERIFY(f.session.artboardSelected());
        QVERIFY(!f.session.hasSelection());
        QCOMPARE(f.canvas.selectionHandles().size(), size_t(8));
        // A click changes nothing in the file.
        QVERIFY(!f.session.canUndo());
    }

    void theNameOfASecondArtboardSelectsThatOne()
    {
        Fixture f;
        f.session.addArtboard({300, 0, 100, 100});
        f.click(f.nameOf(1));
        QVERIFY(f.session.artboardSelected());
        QCOMPARE(f.session.activeArtboard(), 1);
        f.click(f.nameOf(0));
        QCOMPARE(f.session.activeArtboard(), 0);
    }

    void clickingElsewhereLetsGo()
    {
        Fixture f;
        f.selectArtboard();
        f.click({100, 100});
        QVERIFY(!f.session.artboardSelected());
        QVERIFY(f.canvas.selectionHandles().empty());
        // And picking an object takes the selection from it.
        const QUuid square = f.session.addPath(Shapes::rectangle({50, 50, 20, 20}), QStringLiteral("Square"));
        f.session.deselectAll();
        f.selectArtboard();
        f.click({60, 60});
        QVERIFY(!f.session.artboardSelected());
        QCOMPARE(f.session.selection(), std::vector<QUuid>{square});
    }

    void draggingTheNameMovesTheArtboardAndItsArtAsOneStep()
    {
        Fixture f;
        const QUuid square = f.session.addPath(Shapes::rectangle({50, 50, 20, 20}), QStringLiteral("Square"));
        f.session.deselectAll();
        f.drag(f.nameOf(0), f.nameOf(0) + QPointF(60, 40));
        QCOMPARE(f.document().artboard(0).rect.topLeft(), QPointF(60, 40));
        QCOMPARE(f.document().bounds(square).topLeft(), QPointF(110, 90));
        QCOMPARE(f.session.undoName(), QStringLiteral("Move Artboard"));
        QVERIFY(f.session.artboardSelected());
        f.session.undo();
        QCOMPARE(f.document().artboard(0).rect.topLeft(), QPointF(0, 0));
        QCOMPARE(f.document().bounds(square).topLeft(), QPointF(50, 50));
        QVERIFY(f.session.undoName() != QStringLiteral("Move Artboard"));
    }

    void altDraggingTheNameDuplicatesAndSelectsTheCopy()
    {
        Fixture f;
        f.drag(f.nameOf(0), f.nameOf(0) + QPointF(300, 0), Qt::AltModifier);
        QCOMPARE(f.document().artboardCount(), 2);
        QCOMPARE(f.session.activeArtboard(), 1);
        QVERIFY(f.session.artboardSelected());
    }

    void aHandleResizesItAsOneStep()
    {
        Fixture f;
        f.selectArtboard();
        f.drag({200, 200}, {260, 240});
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 260, 240));
        QCOMPARE(f.document().size, QSizeF(260, 240));
        QCOMPARE(f.session.undoName(), QStringLiteral("Resize Artboard"));
        f.session.undo();
        QCOMPARE(f.document().size, QSizeF(200, 200));
        QVERIFY(!f.session.canUndo());
        QVERIFY(f.session.artboardSelected());
    }

    void theTopLeftHandleMovesTheCornerAndTheArtBesideIt()
    {
        Fixture f;
        const QUuid square = f.session.addPath(Shapes::rectangle({50, 50, 20, 20}), QStringLiteral("Square"));
        f.session.deselectAll();
        f.selectArtboard();
        f.drag({0, 0}, {30, 20});
        QCOMPARE(f.document().artboard(0).rect, QRectF(30, 20, 170, 180));
        // Left and Top, the default, rides the corner.
        QCOMPARE(f.document().bounds(square).topLeft(), QPointF(80, 70));
    }

    void artFollowsItsConstraintsWhenAHandleResizes()
    {
        Fixture f;
        const QUuid edge = f.session.addPath(Shapes::rectangle({170, 80, 20, 20}), QStringLiteral("Edge"));
        f.session.setConstraint(Qt::Horizontal, LayoutConstraint::end);
        f.session.deselectAll();
        f.selectArtboard();
        f.drag({200, 100}, {300, 100});
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 300, 200));
        QCOMPARE(f.document().bounds(edge), QRectF(270, 80, 20, 20));
    }

    void shiftKeepsTheRatioAndAltWorksFromTheCentre()
    {
        Fixture f;
        f.selectArtboard();
        f.drag({200, 200}, {300, 210}, Qt::ShiftModifier);
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 300, 300));
        f.session.undo();
        f.drag({200, 200}, {250, 230}, Qt::AltModifier);
        // Each side moved out by what the corner did: 50 by 30.
        QCOMPARE(f.document().artboard(0).rect, QRectF(-50, -30, 300, 260));
    }

    void aSideHandleWithShiftScalesBothWays()
    {
        Fixture f;
        f.selectArtboard();
        f.drag({200, 100}, {300, 100}, Qt::ShiftModifier);
        QCOMPARE(f.document().artboard(0).rect.size(), QSizeF(300, 300));
    }

    void handlesSnapToOtherArt()
    {
        Fixture f;
        f.session.usesSmartGuides = true;
        f.session.addPath(Shapes::rectangle({400, 20, 40, 40}), QStringLiteral("Target"));
        f.session.deselectAll();
        f.selectArtboard();
        // 397 is within reach of the target's left edge at 400.
        f.drag({200, 100}, {397, 100});
        QCOMPARE(f.document().artboard(0).rect.right(), 400.0);
    }

    void movingSnapsToOtherArtboards()
    {
        Fixture f;
        f.session.usesSmartGuides = true;
        f.session.addArtboard({300, 0, 100, 100});
        f.session.setActiveArtboard(0);
        // Moved 97, its right edge (297) is 3 short of the next artboard's left edge, at 300.
        f.drag(f.nameOf(0), f.nameOf(0) + QPointF(97, 0));
        QCOMPARE(f.document().artboard(0).rect.left(), 100.0);
    }

    void anArtboardHasNoRotateZone()
    {
        Fixture f;
        f.selectArtboard();
        f.drag({208, 208}, {260, 240});
        QVERIFY(!f.session.canUndo());
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 200, 200));
    }

    void escapeMidDragPutsItBack()
    {
        Fixture f;
        f.selectArtboard();
        f.press({200, 200});
        f.move({260, 240});
        f.move({280, 260});
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 280, 260));
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        f.release({280, 260});
        QCOMPARE(f.document().artboard(0).rect, QRectF(0, 0, 200, 200));
        QVERIFY(!f.session.canUndo());
    }

    void deleteRemovesTheSelectedArtboardEvenTheLast()
    {
        Fixture f;
        f.session.addArtboard({300, 0, 100, 100});
        f.click(f.nameOf(1));
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QCOMPARE(f.document().artboardCount(), 1);
        f.selectArtboard();
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QCOMPARE(f.document().artboardCount(), 0);
        f.session.undo();
        QCOMPARE(f.document().artboardCount(), 1);
    }

    void itsNameStacksAboveAFramesNameAtTheSameCorner()
    {
        Fixture f;
        f.session.addFrame({0, 0, 100, 100});
        f.session.deselectAll();
        // The frame's name is where the artboard's would be, so a click there is the frame's.
        f.click(f.nameOf(0));
        QVERIFY(!f.session.artboardSelected());
        QCOMPARE(f.session.selection().size(), size_t(1));
        // The artboard's own sits a line higher.
        f.click(f.nameOf(0) - QPointF(0, 15));
        QVERIFY(f.session.artboardSelected());
        // For a look at it: OMASTRATOR_TEST_GRAB=out.png.
        if (const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB"); !grab.isEmpty())
            f.canvas.grab().save(QString::fromLocal8Bit(grab));
    }
};

QTEST_MAIN(ArtboardSelectTests)
#include "ArtboardSelectTests.moc"
