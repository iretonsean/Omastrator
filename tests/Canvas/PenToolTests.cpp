#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QTest>

// The pen on existing paths: continuing, closing, joining, adding, deleting and converting anchors.
namespace {
// A canvas on a 400×300 artboard at 1:1, so document points are view points.
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

    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }

    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, view(from));
        for (int step = 1; step <= 4; ++step)
            move(from + (to - from) * step / 4.0, modifiers);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, view(to));
    }

    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseClick(&canvas, Qt::LeftButton, modifiers, view(at));
    }

    // An open two-anchor line drawn with the pen and ended with Return.
    QUuid line(QPointF from, QPointF to)
    {
        session.selectTool(Tool::pen);
        click(from);
        click(to);
        QTest::keyClick(&canvas, Qt::Key_Return);
        return session.selection().front();
    }

    std::vector<QUuid> paths() const
    {
        std::vector<QUuid> ids;
        for (const VectorObject &object : session.document()->objects) {
            if (object.kind != ObjectKind::layer)
                ids.push_back(object.id);
        }
        return ids;
    }

    std::vector<QPointF> anchors(const QUuid &id) const
    {
        std::vector<QPointF> result;
        for (const PathNode &node : session.document()->find(id)->path.contours.front().nodes)
            result.push_back(node.anchor);
        return result;
    }
};

bool near(QPointF a, QPointF b, double tolerance = 1)
{
    return QLineF(a, b).length() <= tolerance;
}

bool near(const std::vector<QPointF> &a, const std::vector<QPointF> &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](QPointF p, QPointF q) { return near(p, q); });
}
}

class PenToolTests : public QObject {
    Q_OBJECT

private slots:
    void clickingTheLastEndContinuesThePath()
    {
        Fixture f;
        const QUuid id = f.line({50, 50}, {150, 50});
        f.click({150, 50});
        f.click({150, 150});
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.paths(), std::vector<QUuid>{id});
        QVERIFY(near(f.anchors(id), {{50, 50}, {150, 50}, {150, 150}}));
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Path"));
        f.session.undo();
        QVERIFY(near(f.anchors(id), {{50, 50}, {150, 50}}));
    }

    void clickingTheFirstEndPrependsAnchors()
    {
        Fixture f;
        const QUuid id = f.line({50, 50}, {150, 50});
        // Any unlocked path continues, selected or not.
        f.session.deselectAll();
        f.click({50, 50});
        f.click({50, 150});
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.paths(), std::vector<QUuid>{id});
        QVERIFY(near(f.anchors(id), {{50, 150}, {50, 50}, {150, 50}}));
        // Resumed and ended with nothing added: no step.
        const QString before = f.session.undoName();
        f.click({50, 150});
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.session.undoName(), before);
        QVERIFY(near(f.anchors(id), {{50, 150}, {50, 50}, {150, 50}}));
    }

    void clickingTheOtherEndCloses()
    {
        Fixture f;
        const QUuid id = f.line({50, 50}, {150, 50});
        f.click({150, 50});
        f.click({100, 150});
        f.click({50, 50});
        QVERIFY(f.session.document()->find(id)->path.contours.front().closed);
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 3);
        QVERIFY(!f.session.isInteracting());
    }

    void clickingAnotherPathsEndJoinsThem()
    {
        Fixture f;
        const QUuid first = f.line({50, 50}, {150, 50});
        const QUuid second = f.line({50, 200}, {150, 200});
        f.session.deselectAll();
        f.click({150, 50});
        f.click({150, 200});
        QCOMPARE(f.paths(), std::vector<QUuid>{first});
        QVERIFY(near(f.anchors(first), {{50, 50}, {150, 50}, {150, 200}, {50, 200}}));
        QVERIFY(!f.session.document()->find(first)->path.contours.front().closed);
        QVERIFY(!f.session.isInteracting());
        QCOMPARE(f.session.selection(), std::vector<QUuid>{first});
        f.session.undo();
        QCOMPARE(f.paths(), (std::vector<QUuid>{first, second}));
        QVERIFY(near(f.anchors(first), {{50, 50}, {150, 50}}));
    }

    void clicksOnASelectedPathAddAndDeleteAnchors()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 100, 100}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::pen);
        const QRectF bounds = f.session.document()->bounds(id);
        // On a side: a new anchor, the shape unchanged.
        f.click({100, 50});
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 5);
        QCOMPARE(f.session.document()->bounds(id), bounds);
        QCOMPARE(f.session.undoName(), QStringLiteral("Add Anchor Point"));
        // On an anchor: it goes.
        f.click({100, 50});
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 4);
        QCOMPARE(f.session.undoName(), QStringLiteral("Delete Anchor Point"));
        f.click({50, 50});
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 3);
        QCOMPARE(f.paths(), std::vector<QUuid>{id});
        // Unselected, a side is only somewhere to start a path.
        f.session.deselectAll();
        f.click({100, 150});
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 3);
    }

    void altClickConvertsAnAnchor()
    {
        Fixture f;
        VectorPath path;
        Contour contour;
        contour.nodes = {PathNode({50, 100}), PathNode({150, 100}, {110, 80}, {190, 120}, true), PathNode({250, 100})};
        path.contours = {contour};
        const QUuid id = f.session.addPath(path, QStringLiteral("Path"));
        f.session.selectTool(Tool::pen);
        // Smooth to corner: the handles retract.
        f.click({150, 100}, Qt::AltModifier);
        const PathNode *node = &f.session.document()->find(id)->path.contours.front().nodes[1];
        QVERIFY(!node->hasIn() && !node->hasOut());
        QVERIFY(!node->smooth);
        QCOMPARE(f.session.undoName(), QStringLiteral("Convert Anchor Point"));
        // Corner to smooth: a drag pulls symmetric handles.
        f.drag({150, 100}, {190, 100}, Qt::AltModifier);
        node = &f.session.document()->find(id)->path.contours.front().nodes[1];
        QVERIFY(near(node->out, {190, 100}));
        QVERIFY(near(node->in, {110, 100}));
        QVERIFY(node->smooth);
        QCOMPARE(f.session.document()->find(id)->path.nodeCount(), 3);
        // Escape mid-drag puts the anchor back.
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({150, 100}));
        f.move({150, 140}, Qt::AltModifier);
        f.move({150, 160}, Qt::AltModifier);
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({150, 160}));
        node = &f.session.document()->find(id)->path.contours.front().nodes[1];
        QVERIFY(near(node->out, {190, 100}));
        QVERIFY(!f.session.isInteracting());
    }

    void oneClickThenAnotherToolLeavesNothing()
    {
        Fixture f;
        f.session.selectTool(Tool::pen);
        f.click({50, 50});
        QVERIFY(f.session.isInteracting());
        // A toolbar click, not a key the canvas sees.
        f.session.selectTool(Tool::select);
        QVERIFY(f.paths().empty());
        QVERIFY(!f.session.canUndo());
    }

    void cursorShowsWhatAClickDoes()
    {
        Fixture f;
        f.line({50, 50}, {150, 50});
        const auto cursorAt = [&](QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            f.move(at, modifiers, Qt::NoButton);
            return f.canvas.cursor().pixmap().toImage();
        };
        const QImage start = cursorAt({250, 250});
        const QImage resume = cursorAt({150, 50});
        const QImage add = cursorAt({100, 50});
        QVERIFY(!start.isNull());
        QVERIFY(start != resume);
        QVERIFY(start != add);
        QVERIFY(resume != add);
    }
};

QTEST_MAIN(PenToolTests)
#include "PenToolTests.moc"
