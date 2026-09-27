#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QLineF>
#include <QTest>

// The Gradient tool's on-canvas annotator, and the eyedropper's Alt-click.
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

    const VectorObject &object(const QUuid &id) const { return *session.document()->find(id); }
};

bool near(QPointF a, QPointF b, double tolerance = 0.02)
{
    return QLineF(a, b).length() <= tolerance;
}
}

class GradientToolTests : public QObject {
    Q_OBJECT

private slots:
    void gIsTheGradientTool()
    {
        Fixture f;
        QTest::keyClick(&f.canvas, Qt::Key_G);
        QCOMPARE(f.session.tool(), Tool::gradient);
        QCOMPARE(rawValue(Tool::gradient), QStringLiteral("gradient"));
        QCOMPARE(title(Tool::gradient), QStringLiteral("Gradient"));
    }

    void aDragAcrossTheSelectionSetsAGradientInOneStep()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({100, 100, 200, 100}), QStringLiteral("Rectangle"));
        f.session.setFillOfSelection(Paint::solid(Qt::red));
        f.session.selectTool(Tool::gradient);
        f.drag({100, 150}, {300, 150});
        const Paint &fill = f.object(id).fill;
        QCOMPARE(fill.kind, PaintKind::linearGradient);
        QCOMPARE(fill.stops.front().color, QColor(Qt::red));
        QVERIFY(near(fill.start, {0, 0.5}));
        QVERIFY(near(fill.end, {1, 0.5}));
        QCOMPARE(f.session.undoName(), QStringLiteral("Gradient"));
        f.session.undo();
        QCOMPARE(f.object(id).fill, Paint::solid(Qt::red));
    }

    void draggingTheEndChangesTheAngleLive()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({100, 100, 200, 100}), QStringLiteral("Rectangle"));
        f.session.setFillOfSelection(Paint::linear(Qt::black, Qt::white));
        f.session.selectTool(Tool::gradient);
        const Paint before = f.object(id).fill;
        // The end sits at the right edge's middle: (300, 150).
        f.press({300, 150});
        f.move({290, 160});
        f.move({200, 200});
        // Live: the document already shows the new angle while the button is down.
        QVERIFY(near(f.object(id).fill.end, {0.5, 1}));
        QVERIFY(near(f.object(id).fill.start, before.start));
        f.release({200, 200});
        QCOMPARE(f.session.undoName(), QStringLiteral("Gradient"));
        f.session.undo();
        QCOMPARE(f.object(id).fill, before);
        QVERIFY(!f.session.canUndo() || f.session.undoName() != QStringLiteral("Gradient"));
    }

    void draggingAStopSlidesItAlongTheBar()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({100, 100, 200, 100}), QStringLiteral("Rectangle"));
        f.session.setFillOfSelection(Paint::linear(Qt::black, Qt::white));
        f.session.selectTool(Tool::gradient);
        // The first stop's swatch sits 14 points beside the bar's start, below a left-to-right bar.
        f.drag({100, 164}, {150, 164});
        QVERIFY(std::abs(f.object(id).fill.stops.front().offset - 0.25) < 0.02);
        QVERIFY(near(f.object(id).fill.start, {0, 0.5}));
        QCOMPARE(f.session.undoName(), QStringLiteral("Gradient"));
    }

    void aClickOnAnotherObjectSelectsIt()
    {
        Fixture f;
        const QUuid first = f.session.addPath(Shapes::rectangle({10, 10, 50, 50}), QStringLiteral("Rectangle"));
        const QUuid second = f.session.addPath(Shapes::rectangle({200, 10, 50, 50}), QStringLiteral("Rectangle"));
        f.session.select({first});
        f.session.selectTool(Tool::gradient);
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({225, 35}));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{second});
        // A click alone changes no paint.
        QCOMPARE(f.object(second).fill.kind, PaintKind::solid);
        QVERIFY(!f.canvas.grab().isNull());
    }

    void eyedropperAltClickPaintsTheClickedObject()
    {
        Fixture f;
        const QUuid target = f.session.addPath(Shapes::rectangle({150, 50, 40, 40}), QStringLiteral("Rectangle"));
        const QUuid source = f.session.addPath(Shapes::rectangle({50, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.setFillOfSelection(Paint::solid(Qt::green));
        f.session.selectTool(Tool::eyedropper);
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({170, 70}));
        QCOMPARE(f.object(target).fill, Paint::solid(Qt::green));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{source});
    }
};

QTEST_MAIN(GradientToolTests)
#include "GradientToolTests.moc"
