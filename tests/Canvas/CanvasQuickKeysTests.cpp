#include "Canvas/EditorCanvas.h"
#include "Canvas/SmartGuides.h"
#include "Document/PathOperations.h"
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Number-key opacity, the keyboard increment, Alt-hover measuring, drag readouts
// and right-clicks on the canvas.
namespace {
struct Fixture {
    EditorSession session;
    EditorCanvas canvas{session};

    Fixture()
    {
        session.createDocument({400, 300});
        session.usesSmartGuides = false;
        canvas.resize(800, 600);
        canvas.show();
        if (!QTest::qWaitForWindowExposed(&canvas))
            qWarning("canvas never exposed");
        session.actualSize();
        canvas.setFocus();
    }

    QPoint view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size).toPoint(); }

    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButtons buttons = Qt::NoButton)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }

    void rightClick(QPointF at)
    {
        QContextMenuEvent event(QContextMenuEvent::Mouse, view(at), canvas.mapToGlobal(view(at)));
        QApplication::sendEvent(&canvas, &event);
    }

    QUuid box(QRectF rect) { return session.addPath(Shapes::rectangle(rect), QStringLiteral("Box")); }
    double opacity(const QUuid &id) const { return session.document()->find(id)->opacity; }
    QRectF bounds(const QUuid &id) const { return session.document()->bounds(id); }
};
}

class CanvasQuickKeysTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cleanup() { EditorCanvas::setKeyboardIncrement(1); }

    void digitsSetOpacity()
    {
        Fixture f;
        const QUuid id = f.box({50, 50, 40, 40});
        QTest::keyClick(&f.canvas, Qt::Key_5);
        QCOMPARE(f.opacity(id), 0.5);
        // The step lands once the wait for a second digit is over.
        QTRY_VERIFY(!f.session.isInteracting());
        QCOMPARE(f.session.undoName(), QStringLiteral("Opacity"));
        QTest::keyClick(&f.canvas, Qt::Key_0);
        QTRY_VERIFY(!f.session.isInteracting());
        QCOMPARE(f.opacity(id), 1.0);
        // Two quick digits are one value and one step.
        QTest::keyClick(&f.canvas, Qt::Key_2);
        QTest::keyClick(&f.canvas, Qt::Key_5);
        QVERIFY(!f.session.isInteracting());
        QCOMPARE(f.opacity(id), 0.25);
        f.session.undo();
        QCOMPARE(f.opacity(id), 1.0);
        // Without a selection a digit does nothing.
        f.session.deselectAll();
        QTest::keyClick(&f.canvas, Qt::Key_3);
        QVERIFY(!f.session.isInteracting());
        QCOMPARE(f.opacity(id), 1.0);
    }

    void aClickEndsTheOpacityWait()
    {
        Fixture f;
        const QUuid id = f.box({50, 50, 40, 40});
        QTest::keyClick(&f.canvas, Qt::Key_4);
        QVERIFY(f.session.isInteracting());
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({300, 250}));
        QVERIFY(!f.session.isInteracting());
        QCOMPARE(f.opacity(id), 0.4);
    }

    void arrowsMoveByTheKeyboardIncrement()
    {
        Fixture f;
        const QUuid id = f.box({50, 50, 20, 20});
        EditorCanvas::setKeyboardIncrement(0.5);
        QCOMPARE(EditorCanvas::keyboardIncrement(), 0.5);
        QTest::keyClick(&f.canvas, Qt::Key_Right);
        QTest::keyClick(&f.canvas, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(f.bounds(id), QRectF(50.5, 55, 20, 20));
        // Nothing and negatives keep the old value.
        EditorCanvas::setKeyboardIncrement(0);
        QCOMPARE(EditorCanvas::keyboardIncrement(), 0.5);
    }

    void altArrowsMoveACopy()
    {
        Fixture f;
        const QUuid id = f.box({50, 50, 20, 20});
        QTest::keyClick(&f.canvas, Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(f.session.document()->children(f.session.activeLayer().value()).size(), size_t(2));
        QCOMPARE(f.bounds(id), QRectF(50, 50, 20, 20));
        QCOMPARE(f.session.selectionBounds(), QRectF(51, 50, 20, 20));
        // Transform Again keeps stepping copies.
        f.session.transformAgain();
        QCOMPARE(f.session.selectionBounds(), QRectF(52, 50, 20, 20));
        QCOMPARE(f.session.document()->children(f.session.activeLayer().value()).size(), size_t(3));
    }

    void altHoverMeasuresToWhatIsUnderThePointer()
    {
        Fixture f;
        const QUuid a = f.box({10, 10, 40, 40});
        f.box({110, 10, 40, 40});
        f.session.select({a});
        f.move({130, 30});
        QVERIFY(f.canvas.measurements().empty());
        f.move({130, 30}, Qt::AltModifier);
        const std::vector<QLineF> lines = f.canvas.measurements();
        QCOMPARE(lines.size(), size_t(1));
        QCOMPARE(lines.front().length(), 60.0);
        // Over nothing: to the artboard's four edges.
        f.move({300, 250}, Qt::AltModifier);
        QCOMPARE(f.canvas.measurements().size(), size_t(4));
        // Letting go of Alt clears them.
        QTest::keyRelease(&f.canvas, Qt::Key_Alt);
        QVERIFY(f.canvas.measurements().empty());
    }

    void distancesAndLabels()
    {
        const std::vector<QLineF> inside = SmartGuides::distances({20, 20, 10, 10}, {0, 0, 100, 50});
        QCOMPARE(inside.size(), size_t(4));
        QCOMPARE(inside[0], QLineF(0, 25, 20, 25));
        QCOMPARE(inside[1], QLineF(30, 25, 100, 25));
        const std::vector<QLineF> apart = SmartGuides::distances({0, 0, 10, 10}, {30, 40, 10, 10});
        QCOMPARE(apart.size(), size_t(2));
        QCOMPARE(apart[0].length(), 20.0);
        QCOMPARE(apart[1].length(), 30.0);
        QCOMPARE(SmartGuides::label(12.0), QStringLiteral("12"));
        QCOMPARE(SmartGuides::label(12.504), QStringLiteral("12.5"));
        QCOMPARE(SmartGuides::label(-0.001), QStringLiteral("0"));
    }

    void dragsShowWhatChanges()
    {
        Fixture f;
        const QUuid id = f.box({50, 50, 40, 40});
        f.session.selectTool(Tool::select);
        QVERIFY(f.canvas.dragReadout().isEmpty());
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({70, 70}));
        f.move({80, 70}, Qt::NoModifier, Qt::LeftButton);
        f.move({95, 60}, Qt::NoModifier, Qt::LeftButton);
        QCOMPARE(f.canvas.dragReadout(), QStringLiteral("Δx 25  Δy -10"));
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({95, 60}));
        QVERIFY(f.canvas.dragReadout().isEmpty());
        // A corner handle reads the new size.
        const QRectF box = f.bounds(id);
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view(box.bottomRight()));
        f.move(box.bottomRight() + QPointF(5, 5), Qt::NoModifier, Qt::LeftButton);
        f.move(box.bottomRight() + QPointF(10, 20), Qt::NoModifier, Qt::LeftButton);
        QCOMPARE(f.canvas.dragReadout(), QStringLiteral("50 × 60"));
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view(box.bottomRight() + QPointF(10, 20)));
    }

    void rightClickPicksItsTargetAndListsTheStack()
    {
        Fixture f;
        const QUuid under = f.box({50, 50, 60, 60});
        const QUuid over = f.box({80, 80, 60, 60});
        const QUuid apart = f.box({250, 50, 40, 40});
        QSignalSpy requested(&f.canvas, &EditorCanvas::contextMenuRequested);
        f.session.select({apart});
        f.rightClick({90, 90});
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(1).value<QList<QUuid>>(), (QList<QUuid>{over, under}));
        // An unselected object becomes the selection.
        QCOMPARE(f.session.selection(), std::vector<QUuid>{over});
        // Inside the selection it stays as it was.
        f.session.select({under, over});
        f.rightClick({60, 60});
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{under, over}));
        // Empty canvas clears it.
        f.rightClick({350, 250});
        QVERIFY(f.session.selection().empty());
        QVERIFY(requested.last().at(1).value<QList<QUuid>>().isEmpty());
    }

    void isolationEntersAndLeavesAGroup()
    {
        Fixture f;
        const QUuid a = f.box({50, 50, 40, 40});
        const QUuid b = f.box({150, 50, 40, 40});
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        f.canvas.isolateGroup(group);
        QCOMPARE(f.canvas.isolatedGroup(), std::optional(group));
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({170, 70}));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{b});
        f.canvas.exitIsolation();
        QVERIFY(!f.canvas.isolatedGroup());
        QCOMPARE(f.session.selection(), std::vector<QUuid>{group});
        // Only groups isolate.
        f.canvas.isolateGroup(a);
        QVERIFY(!f.canvas.isolatedGroup());
    }

    void typeHasItsOwnMenu()
    {
        Fixture f;
        f.session.selectTool(Tool::text);
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({100, 100}));
        QTest::keyClicks(&f.canvas, QStringLiteral("hello there"));
        QTest::keyClick(&f.canvas, Qt::Key_A, Qt::ControlModifier);
        std::unique_ptr<QMenu> menu(f.canvas.textEditingMenu(nullptr));
        QVERIFY(menu->findChild<QAction *>(QStringLiteral("textCopy"))->isEnabled());
        QMenu *cases = menu->findChild<QAction *>(QStringLiteral("textChangeCase"))->menu();
        QVERIFY(cases->isEnabled());
        for (QAction *entry : cases->actions()) {
            if (entry->text() == QStringLiteral("Title Case"))
                entry->trigger();
        }
        f.canvas.finishTextEditing();
        const std::vector<QUuid> texts = f.session.document()->children(f.session.activeLayer().value());
        QCOMPARE(f.session.document()->find(texts.front())->text.text, QStringLiteral("Hello There"));
    }
};

QTEST_MAIN(CanvasQuickKeysTests)
#include "CanvasQuickKeysTests.moc"
