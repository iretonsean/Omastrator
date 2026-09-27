#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QLineF>
#include <QTest>

// The Width tool's on-canvas annotator (P2-7): dragging on a stroke to add or move a point.
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
}

class WidthToolTests : public QObject {
    Q_OBJECT

private slots:
    void shiftWIsTheWidthTool()
    {
        Fixture f;
        QTest::keyClick(&f.canvas, Qt::Key_W, Qt::ShiftModifier);
        QCOMPARE(f.session.tool(), Tool::width);
        QCOMPARE(rawValue(Tool::width), QStringLiteral("width"));
        QCOMPARE(title(Tool::width), QStringLiteral("Width"));
    }

    void draggingOnTheStrokeAddsAPointInOneStep()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::line({50, 100}, {350, 100}), QStringLiteral("Line"));
        StrokeStyle stroke;
        stroke.paint = Paint::solid(Qt::black);
        stroke.width = 8;
        f.session.setStrokeOfSelection(stroke);
        f.session.select({id});
        f.session.selectTool(Tool::width);
        // A drag straight down from the middle of the stroke widens it there.
        f.drag({200, 100}, {200, 130});
        const StrokeStyle &after = f.object(id).stroke;
        QCOMPARE(after.widthPoints.size(), size_t(1));
        QVERIFY(after.widthPoints.front().left > 20);
        QCOMPARE(after.widthProfile, StrokeWidthProfile::custom);
        QCOMPARE(f.session.undoName(), QStringLiteral("Width Point"));
        f.session.undo();
        QVERIFY(f.object(id).stroke.widthPoints.empty());
    }

    void altDragMovesOnlyOneSide()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::line({50, 100}, {350, 100}), QStringLiteral("Line"));
        StrokeStyle stroke;
        stroke.paint = Paint::solid(Qt::black);
        stroke.width = 8;
        f.session.setStrokeOfSelection(stroke);
        f.session.select({id});
        f.session.selectTool(Tool::width);
        f.drag({200, 100}, {200, 130}, Qt::AltModifier);
        const StrokeWidthPoint &point = f.object(id).stroke.widthPoints.front();
        QVERIFY(point.left > 20);
        QCOMPARE(point.right, 4.0);
    }

    void deleteRemovesTheLastTouchedPoint()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::line({50, 100}, {350, 100}), QStringLiteral("Line"));
        StrokeStyle stroke;
        stroke.paint = Paint::solid(Qt::black);
        stroke.width = 8;
        f.session.setStrokeOfSelection(stroke);
        f.session.select({id});
        f.session.selectTool(Tool::width);
        f.drag({200, 100}, {200, 130});
        QCOMPARE(f.object(id).stroke.widthPoints.size(), size_t(1));
        QTest::keyClick(&f.canvas, Qt::Key_Backspace);
        QVERIFY(f.object(id).stroke.widthPoints.empty());
        QCOMPARE(f.session.undoName(), QStringLiteral("Remove Width Point"));
    }
};

QTEST_MAIN(WidthToolTests)
#include "WidthToolTests.moc"
