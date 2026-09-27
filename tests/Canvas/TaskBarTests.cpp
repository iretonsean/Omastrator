#include "Canvas/EditorCanvas.h"
#include "Canvas/TaskBar.h"
#include "Document/PathOperations.h"
#include <QApplication>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

// The contextual task bar's place, visibility and fading, with a stand-in filler.
namespace {
struct Fixture {
    EditorSession session;
    EditorCanvas canvas{session};
    TaskBar *bar = new TaskBar(canvas);
    bool blocked = false;

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
        bar->setFiller([this] { return session.hasSelection() ? QStringLiteral("any") : QString(); },
                       [](QHBoxLayout &row) {
                           auto *button = new QToolButton;
                           button->setObjectName(QStringLiteral("stand-in"));
                           button->setText(QStringLiteral("Group"));
                           row.addWidget(button);
                       });
        bar->setBlocked([this] { return blocked; });
    }

    QPoint view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size).toPoint(); }

    void move(QPointF to, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
    }

    QUuid box(QRectF rect) { return session.addPath(Shapes::rectangle(rect), QStringLiteral("Box")); }
};
}

class TaskBarTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        for (const char *key : {"view/contextualTaskBar", "view/taskBarOffset", "view/taskBarPinned", "view/taskBarPinnedAt"})
            QSettings().remove(QString::fromLatin1(key));
    }

    void placesBelowThenAboveThenOverTheView()
    {
        const QSize bar(200, 40), view(800, 600);
        // Room below: centred under it, clear of the rotate zone.
        const QRect below = TaskBar::place(QRectF(300, 100, 200, 100), bar, view);
        QCOMPARE(below.topLeft(), QPoint(300, 200 + TaskBar::gap));
        // None below: above.
        const QRect above = TaskBar::place(QRectF(300, 500, 200, 80), bar, view);
        QCOMPARE(above.bottom() + 1, 500 - TaskBar::gap);
        // Neither: over the bottom of the view.
        const QRect over = TaskBar::place(QRectF(0, 20, 800, 570), bar, view);
        QCOMPARE(over.bottom() + 1, 600 - TaskBar::margin);
        // Near an edge it stays inside the view, and so does a moved one.
        const QRect edge = TaskBar::place(QRectF(-80, 100, 100, 100), bar, view);
        QCOMPARE(edge.left(), TaskBar::margin);
        const QRect moved = TaskBar::place(QRectF(300, 100, 200, 100), bar, view, QPoint(5000, 5000));
        QVERIFY(QRect(QPoint(), view).contains(moved));
    }

    void showsUnderTheSelectionInsideTheView()
    {
        Fixture f;
        QVERIFY(!f.bar->isVisible());
        f.box({100, 60, 120, 80});
        QVERIFY(f.bar->isVisible());
        QCOMPARE(f.bar->shownKind(), QStringLiteral("any"));
        QVERIFY(f.bar->findChild<QToolButton *>(QStringLiteral("stand-in")));
        const QRectF selection = *f.canvas.selectionViewRect();
        QVERIFY(QRect(QPoint(), f.canvas.size()).contains(f.bar->geometry()));
        QVERIFY(!f.bar->geometry().intersects(selection.toAlignedRect()));
        QVERIFY(f.bar->y() >= selection.bottom() + TaskBar::gap - 1);
        QVERIFY(std::abs(f.bar->geometry().center().x() - selection.center().x()) <= 2);
        // It follows the selection as the view pans.
        const QPoint before = f.bar->pos();
        f.session.panView(QSizeF(-40, -30));
        const QPointF moved = f.canvas.selectionViewRect()->topLeft() - selection.topLeft();
        QVERIFY(!moved.isNull());
        QCOMPARE(f.bar->pos(), before + moved.toPoint());
        f.session.deselectAll();
        QVERIFY(!f.bar->isVisible());
    }

    void hidesWhileDraggingAndComesBack()
    {
        Fixture f;
        f.box({100, 60, 120, 80});
        QVERIFY(f.bar->isVisible());
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({160, 100}));
        QVERIFY(f.bar->isVisible());
        f.move({170, 110});
        f.move({200, 130});
        QVERIFY(f.canvas.isGesturing());
        QVERIFY(!f.bar->isVisible());
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({200, 130}));
        QVERIFY(!f.canvas.isGesturing());
        QVERIFY(f.bar->isVisible());
    }

    void hidesWhileTypingOnTheCanvas()
    {
        Fixture f;
        f.box({100, 60, 120, 80});
        f.session.selectTool(Tool::text);
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({300, 250}));
        QVERIFY(f.canvas.isEditingText());
        QVERIFY(!f.bar->isVisible());
        QTest::keyClicks(&f.canvas, QStringLiteral("Hi"));
        QVERIFY(!f.bar->isVisible());
        f.canvas.finishTextEditing();
        QVERIFY(f.bar->isVisible());
    }

    void hidesWhilePausedOrBlocked()
    {
        Fixture f;
        f.box({100, 60, 120, 80});
        f.canvas.setPaused(true);
        QVERIFY(!f.bar->isVisible());
        f.canvas.setPaused(false);
        QVERIFY(f.bar->isVisible());
        f.blocked = true;
        f.bar->refresh();
        QVERIFY(!f.bar->isVisible());
    }

    void turningItOffIsRemembered()
    {
        {
            Fixture f;
            f.box({100, 60, 120, 80});
            TaskBar::setTurnedOn(false);
            QVERIFY(!f.bar->isVisible());
            QCOMPARE(QSettings().value(QStringLiteral("view/contextualTaskBar")).toBool(), false);
        }
        Fixture again;
        again.box({100, 60, 120, 80});
        QVERIFY(!again.bar->isVisible());
        TaskBar::setTurnedOn(true);
        QVERIFY(again.bar->isVisible());
    }

    void theGripMovesItAndTheOffsetIsKept()
    {
        {
            Fixture f;
            f.box({100, 60, 120, 80});
            const QPoint before = f.bar->pos();
            QWidget *grip = f.bar->findChild<QWidget *>(QStringLiteral("taskBarGrip"));
            QVERIFY(grip);
            QCOMPARE(grip->accessibleName(), QStringLiteral("Move Task Bar"));
            QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
            QMouseEvent drag(QEvent::MouseMove, QPointF(35, 45), grip->mapToGlobal(QPointF(35, 45)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(grip, &drag);
            // The grip moved with the pointer: the release lands where it was pressed on it.
            QTest::mouseRelease(grip, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
            QCOMPARE(f.bar->pos(), before + QPoint(30, 40));
            QCOMPARE(TaskBar::savedOffset(), QPoint(30, 40));
        }
        // A new canvas, and another selection: the same offset from its spot.
        Fixture f;
        f.box({150, 40, 60, 60});
        const QRect spot = TaskBar::place(*f.canvas.selectionViewRect(), f.bar->size(), f.canvas.size());
        QCOMPARE(f.bar->pos(), spot.topLeft() + QPoint(30, 40));
        // Pinned, it stays put while the selection moves.
        TaskBar::setPinned(true, QPoint(20, 30));
        f.bar->refresh();
        QCOMPARE(f.bar->pos(), QPoint(20, 30));
        f.session.moveSelection({50, 50});
        QCOMPARE(f.bar->pos(), QPoint(20, 30));
    }

    void fadesNearAHandleAndLetsClicksThrough()
    {
        Fixture f;
        // Wide and short: the bar sits right under the bottom handles.
        f.box({100, 60, 200, 20});
        QVERIFY(f.bar->isVisible());
        QVERIFY(!f.bar->isFaded());
        f.move({200, 80}, Qt::NoButton);
        QVERIFY(f.bar->isFaded());
        QVERIFY(f.bar->testAttribute(Qt::WA_TransparentForMouseEvents));
        f.move({200, 200}, Qt::NoButton);
        QVERIFY(!f.bar->isFaded());
        QVERIFY(!f.bar->testAttribute(Qt::WA_TransparentForMouseEvents));
    }
};

QTEST_MAIN(TaskBarTests)
#include "TaskBarTests.moc"
