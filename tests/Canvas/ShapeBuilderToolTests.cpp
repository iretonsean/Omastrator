#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QTest>

// Shape Builder driven through the canvas: the gestures, one step each, and Escape.
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
        // Two overlapping squares: left, overlap and right regions.
        add(Shapes::rectangle({50, 50, 100, 100}), Qt::red);
        add(Shapes::rectangle({100, 50, 100, 100}), Qt::blue);
        session.selectAll();
        session.selectTool(Tool::shapeBuilder);
    }

    void add(const VectorPath &path, const QColor &colour)
    {
        const QUuid id = session.addPath(path, QStringLiteral("Rectangle"));
        VectorObject object = *session.document()->find(id);
        object.fill = Paint::solid(colour);
        session.updateObject(object, QStringLiteral("Fill"));
    }

    QPoint view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size).toPoint(); }

    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }

    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, view(from));
        for (int step = 1; step <= 8; ++step)
            move(from + (to - from) * step / 8.0, modifiers);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, view(to));
    }

    std::vector<const VectorObject *> paths() const
    {
        std::vector<const VectorObject *> found;
        for (const VectorObject &object : session.document()->objects) {
            if (object.kind == ObjectKind::path)
                found.push_back(&object);
        }
        return found;
    }

    const VectorObject *pathAt(QPointF point) const
    {
        const VectorObject *top = nullptr;
        for (const VectorObject *object : paths()) {
            if (object->path.painterPath().contains(point))
                top = object;
        }
        return top;
    }
};

bool near(QRectF a, QRectF b, double tolerance = 1)
{
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance
        && std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
}
}

class ShapeBuilderToolTests : public QObject {
    Q_OBJECT

private slots:
    void shiftMChoosesTheTool()
    {
        Fixture f;
        f.session.selectTool(Tool::select);
        QTest::keyClick(&f.canvas, Qt::Key_M, Qt::ShiftModifier);
        QCOMPARE(f.session.tool(), Tool::shapeBuilder);
        QTest::keyClick(&f.canvas, Qt::Key_M);
        QCOMPARE(f.session.tool(), Tool::rectangle);
    }

    void dragMergesInOneStep()
    {
        Fixture f;
        const QString before = f.session.undoName();
        f.drag({70, 100}, {170, 100});
        QCOMPARE(f.session.undoName(), QStringLiteral("Shape Builder"));
        QCOMPARE(f.paths().size(), size_t(1));
        QVERIFY(near(f.paths().front()->path.bounds(), QRectF(50, 50, 150, 100)));
        // Colour from artwork: the red square, where the drag began.
        QCOMPARE(f.paths().front()->fill.color, QColor(Qt::red));
        f.session.undo();
        QCOMPARE(f.paths().size(), size_t(2));
        QCOMPARE(f.session.undoName(), before);
    }

    void dragThroughEmptySpaceChangesNothing()
    {
        Fixture f;
        const VectorDocument before = *f.session.document();
        const QString name = f.session.undoName();
        f.drag({20, 250}, {350, 250});
        QVERIFY(*f.session.document() == before);
        QCOMPARE(f.session.undoName(), name);
    }

    void clickSeparatesTheOverlap()
    {
        Fixture f;
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({125, 100}));
        QCOMPARE(f.paths().size(), size_t(3));
        QVERIFY(near(f.pathAt({125, 100})->path.bounds(), QRectF(100, 50, 50, 100)));
    }

    void altClickDeletesARegion()
    {
        Fixture f;
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({125, 100}));
        QCOMPARE(f.paths().size(), size_t(2));
        QVERIFY(!f.pathAt({125, 100}));
        QVERIFY(f.pathAt({70, 100}) && f.pathAt({180, 100}));
    }

    void shiftMarqueeMerges()
    {
        Fixture f;
        f.drag({120, 20}, {190, 180}, Qt::ShiftModifier);
        QCOMPARE(f.paths().size(), size_t(2));
        const VectorObject *merged = f.pathAt({125, 100});
        QVERIFY(merged && merged == f.pathAt({180, 100}));
        QVERIFY(near(merged->path.bounds(), QRectF(100, 50, 100, 100)));
    }

    void escapeCancelsTheDrag()
    {
        Fixture f;
        const VectorDocument before = *f.session.document();
        const QString name = f.session.undoName();
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({70, 100}));
        f.move({120, 100});
        f.move({170, 100});
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({170, 100}));
        QVERIFY(*f.session.document() == before);
        QCOMPARE(f.session.undoName(), name);
    }
};

QTEST_MAIN(ShapeBuilderToolTests)
#include "ShapeBuilderToolTests.moc"
