#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>

namespace {
// A canvas on a 400×300 artboard at 1:1, so document points are view points.
struct Fixture {
    EditorSession session;
    EditorCanvas canvas{session};

    Fixture()
    {
        session.createDocument({400, 300});
        // Guides would pull test drags onto round numbers; their own test turns them on.
        session.usesSmartGuides = false;
        canvas.resize(800, 600);
        canvas.show();
        if (!QTest::qWaitForWindowExposed(&canvas))
            qWarning("canvas never exposed");
        session.actualSize();
        canvas.setFocus();
    }

    QPoint view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size).toPoint(); }

    // Pressed at `from`, moved in steps, released at `to`.
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, view(from));
        for (int step = 1; step <= 4; ++step)
            move(from + (to - from) * step / 4.0, modifiers);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, view(to));
    }

    // QTest's moves carry no modifiers; a real drag's do.
    void move(QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        const QPointF at = view(to);
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }

    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseClick(&canvas, Qt::LeftButton, modifiers, view(at));
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

    const VectorObject &object(const QUuid &id) const { return *session.document()->find(id); }
};

bool near(QRectF a, QRectF b, double tolerance = 1)
{
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance
        && std::abs(a.right() - b.right()) <= tolerance && std::abs(a.bottom() - b.bottom()) <= tolerance;
}

bool near(QPointF a, QPointF b, double tolerance = 1)
{
    return QLineF(a, b).length() <= tolerance;
}
}

class EditorCanvasTests : public QObject {
    Q_OBJECT

private slots:
    void viewportFollowsTheWidget()
    {
        Fixture f;
        QCOMPARE(f.session.viewport.viewSize, QSizeF(800, 600));
        QCOMPARE(f.session.viewport.pointsPerPixel(), 1.0);
        QVERIFY(near(f.session.viewport.documentRect({400, 300}), QRectF(200, 150, 400, 300), 0.01));
    }

    void drawsARectangleAsOneStep()
    {
        Fixture f;
        f.session.selectTool(Tool::rectangle);
        f.drag({50, 40}, {150, 120});
        QCOMPARE(f.paths().size(), size_t(1));
        const QUuid id = f.paths().front();
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 40, 100, 80)));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{id});
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Rectangle"));
        f.session.undo();
        QVERIFY(f.paths().empty());
        QVERIFY(!f.session.canUndo());
    }

    void shapeModifiersSquareAndCentre()
    {
        Fixture f;
        f.session.selectTool(Tool::ellipse);
        // Shift: a circle as wide as the longer side; Alt: about the press.
        f.drag({100, 100}, {140, 120}, Qt::ShiftModifier | Qt::AltModifier);
        QVERIFY(near(f.session.document()->bounds(f.paths().front()), QRectF(60, 60, 80, 80)));
        f.session.selectTool(Tool::star);
        f.drag({200, 50}, {260, 110});
        QVERIFY(near(f.session.document()->bounds(f.paths().back()), QRectF(200, 50, 60, 60)));
        QCOMPARE(f.object(f.paths().back()).path.nodeCount(), f.session.starPoints * 2);
    }

    void aClickWithAShapeToolAddsNothing()
    {
        Fixture f;
        f.session.selectTool(Tool::rectangle);
        f.click({80, 80});
        QVERIFY(f.paths().empty());
        QVERIFY(!f.session.canUndo());
    }

    void selectionToolMovesTheObject()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 60, 40}), QStringLiteral("Rectangle"));
        f.session.deselectAll();
        f.session.selectTool(Tool::select);
        f.drag({80, 70}, {110, 90});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{id});
        QVERIFY(near(f.session.document()->bounds(id), QRectF(80, 70, 60, 40)));
        QCOMPARE(f.session.undoName(), QStringLiteral("Move"));
    }

    void altDragMovesACopy()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 60, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        f.drag({80, 70}, {180, 70}, Qt::AltModifier);
        QCOMPARE(f.paths().size(), size_t(2));
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 60, 40)));
        QVERIFY(near(f.session.selectionBounds(), QRectF(150, 50, 60, 40)));
        // One step: undo takes the copy and leaves the original.
        QCOMPARE(f.session.undoName(), QStringLiteral("Move Copy"));
        f.session.undo();
        QCOMPARE(f.paths(), std::vector<QUuid>{id});
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 60, 40)));
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void escapeDuringAltDragRemovesTheCopy()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 60, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({80, 70}));
        f.move({110, 70}, Qt::AltModifier);
        f.move({150, 70}, Qt::AltModifier);
        QCOMPARE(f.paths().size(), size_t(2));
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::AltModifier, f.view({150, 70}));
        QCOMPARE(f.paths(), std::vector<QUuid>{id});
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 60, 40)));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{id});
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void cornerHandleScales()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 100, 100}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        f.drag({150, 150}, {200, 250});
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 150, 200)));
        QCOMPARE(f.session.undoName(), QStringLiteral("Scale"));
        // Shift keeps the proportions.
        f.session.undo();
        f.drag({150, 150}, {200, 170}, Qt::ShiftModifier);
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 150, 150)));
    }

    void draggingOutsideACornerRotates()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({100, 100, 100, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        // From beyond the top-right corner, a quarter turn about the centre (150, 120).
        f.drag({210, 90}, {180, 180}, Qt::ShiftModifier);
        QVERIFY(near(f.session.document()->bounds(id), QRectF(130, 70, 40, 100)));
        QCOMPARE(f.session.undoName(), QStringLiteral("Rotate"));
    }

    void marqueeSelectsAndShiftAdds()
    {
        Fixture f;
        const QUuid first = f.session.addPath(Shapes::rectangle({20, 20, 40, 40}), QStringLiteral("Rectangle"));
        const QUuid second = f.session.addPath(Shapes::rectangle({200, 200, 40, 40}), QStringLiteral("Rectangle"));
        f.session.deselectAll();
        f.session.selectTool(Tool::select);
        f.drag({5, 5}, {70, 70});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{first});
        f.drag({190, 190}, {250, 250}, Qt::ShiftModifier);
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{first, second}));
        // A click on nothing clears.
        f.click({300, 50});
        QVERIFY(f.session.selection().empty());
    }

    void penDrawsAClosedPathWithACurve()
    {
        Fixture f;
        f.session.selectTool(Tool::pen);
        f.click({50, 50});
        f.drag({150, 50}, {180, 80});
        f.click({100, 150});
        f.click({50, 50});
        QCOMPARE(f.paths().size(), size_t(1));
        const VectorObject &path = f.object(f.paths().front());
        QCOMPARE(path.path.contours.size(), size_t(1));
        const Contour &contour = path.path.contours.front();
        QVERIFY(contour.closed);
        QCOMPARE(contour.nodes.size(), size_t(3));
        QVERIFY(contour.nodes[1].smooth);
        QVERIFY(near(contour.nodes[1].out, {180, 80}));
        QVERIFY(near(contour.nodes[1].in, {120, 20}));
        QVERIFY(!contour.nodes[0].hasOut());
        QVERIFY(!f.session.isInteracting());
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Path"));
        f.session.undo();
        QVERIFY(f.paths().empty());
    }

    void penEndsOpenOnEnterAndDropsALoneAnchor()
    {
        Fixture f;
        f.session.selectTool(Tool::pen);
        f.click({50, 50});
        f.click({150, 60});
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.paths().size(), size_t(1));
        QVERIFY(!f.object(f.paths().front()).path.contours.front().closed);
        // One click, then another tool: no path.
        f.click({200, 200});
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QCOMPARE(f.paths().size(), size_t(1));
        QVERIFY(!f.session.isInteracting());
    }

    void pencilFitsAFreehandPath()
    {
        Fixture f;
        f.session.selectTool(Tool::pencil);
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({100, 100}));
        for (int step = 1; step <= 36; ++step) {
            const double angle = step * 10 * M_PI / 180;
            f.move(QPointF(150, 100) + QPointF(-std::cos(angle), std::sin(angle)) * 50);
        }
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({100, 100}));
        QCOMPARE(f.paths().size(), size_t(1));
        const VectorPath &path = f.object(f.paths().front()).path;
        QVERIFY(path.contours.front().closed);
        QVERIFY(path.nodeCount() >= 4);
        QVERIFY(near(path.bounds(), QRectF(100, 50, 100, 100), 4));
        // An open stroke stays open.
        f.drag({50, 250}, {300, 260});
        QCOMPARE(f.paths().size(), size_t(2));
        QVERIFY(!f.object(f.paths().back()).path.contours.front().closed);
    }

    void directSelectionDragsOneAnchor()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 100, 100}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::directSelect);
        f.session.select({id});
        f.drag({50, 50}, {30, 20});
        const Contour &contour = f.object(id).path.contours.front();
        QVERIFY(std::any_of(contour.nodes.begin(), contour.nodes.end(), [](const PathNode &node) { return near(node.anchor, {30, 20}); }));
        QVERIFY(std::any_of(contour.nodes.begin(), contour.nodes.end(), [](const PathNode &node) { return near(node.anchor, {150, 150}); }));
        QCOMPARE(f.session.pickedNodes().size(), size_t(1));
        QCOMPARE(f.session.undoName(), QStringLiteral("Move Points"));
    }

    void directSelectionMarqueePicksAnchors()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 100, 100}), QStringLiteral("Rectangle"));
        f.session.deselectAll();
        f.session.selectTool(Tool::directSelect);
        f.drag({170, 170}, {130, 130});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{id});
        QCOMPARE(f.session.pickedNodes().size(), size_t(1));
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QCOMPARE(f.object(id).path.nodeCount(), 3);
    }

    void altDraggingAHandleBreaksSmoothness()
    {
        Fixture f;
        VectorPath path;
        Contour contour;
        contour.nodes = {PathNode({50, 100}), PathNode({150, 100}, {110, 100}, {190, 100}, true), PathNode({250, 100})};
        path.contours = {contour};
        const QUuid id = f.session.addPath(path, QStringLiteral("Path"));
        f.session.selectTool(Tool::directSelect);
        f.session.pickNodes({{id, {0, 1}}});
        f.drag({190, 100}, {190, 60}, Qt::AltModifier);
        const PathNode &node = f.object(id).path.contours.front().nodes[1];
        QVERIFY(near(node.out, {190, 60}));
        QVERIFY(near(node.in, {110, 100}));
        QVERIFY(!node.smooth);
    }

    void typeIsTypedInPlace()
    {
        Fixture f;
        QSignalSpy editing(&f.canvas, &EditorCanvas::textEditingChanged);
        f.session.selectTool(Tool::text);
        f.click({100, 100});
        QVERIFY(f.canvas.isEditingText());
        QTest::keyClicks(&f.canvas, QStringLiteral("Hi"));
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QTest::keyClicks(&f.canvas, QStringLiteral("yox"));
        QTest::keyClick(&f.canvas, Qt::Key_Backspace);
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QVERIFY(!f.canvas.isEditingText());
        QCOMPARE(editing.count(), 2);
        QCOMPARE(f.paths().size(), size_t(1));
        const VectorObject &text = f.object(f.paths().front());
        QCOMPARE(text.kind, ObjectKind::text);
        QCOMPARE(text.text.text, QStringLiteral("Hi\nyo"));
        QCOMPARE(text.name, QStringLiteral("Hi"));
        QVERIFY(near(text.transform.map(QPointF(0, 0)), {100, 100}));
        f.session.undo();
        QVERIFY(f.paths().empty());
    }

    void emptyTypeLeavesNothing()
    {
        Fixture f;
        f.session.selectTool(Tool::text);
        f.click({100, 100});
        QTest::keyClick(&f.canvas, Qt::Key_A);
        QTest::keyClick(&f.canvas, Qt::Key_Backspace);
        // A click elsewhere ends it, starting new type there.
        f.click({200, 200});
        QVERIFY(f.canvas.isEditingText());
        f.canvas.finishTextEditing();
        QVERIFY(!f.canvas.isEditingText());
        QVERIFY(f.paths().empty());
        QVERIFY(!f.session.canUndo());
    }

    void clickingTypeEditsItAtTheCaret()
    {
        Fixture f;
        const QUuid id = f.session.addText({100, 100}, QStringLiteral("abc"));
        f.session.selectTool(Tool::text);
        const QRectF box = f.session.document()->bounds(id);
        // Left of the text: the caret starts at 0.
        f.click({box.left() + 1, box.center().y()});
        QVERIFY(f.canvas.isEditingText());
        QTest::keyClicks(&f.canvas, QStringLiteral("X"));
        f.canvas.finishTextEditing();
        QCOMPARE(f.object(id).text.text, QStringLiteral("Xabc"));
        QCOMPARE(f.session.undoName(), QStringLiteral("Edit Type"));
    }

    void zoomToolClicksAndDrags()
    {
        Fixture f;
        f.session.selectTool(Tool::zoom);
        const QPoint at = f.view({100, 100});
        f.click({100, 100});
        QCOMPARE(f.session.viewport.zoom(), 2.0);
        QVERIFY(near(f.session.viewport.documentPoint(at, {400, 300}), {100, 100}));
        f.click({100, 100}, Qt::AltModifier);
        QCOMPARE(f.session.viewport.zoom(), 1.0);
        // A dragged 100×75 box fills the 800×600 view: 8x.
        f.drag({100, 100}, {200, 175});
        QVERIFY(std::abs(f.session.viewport.zoom() - 8) < 0.2);
        QVERIFY(near(f.session.viewport.documentPoint(f.session.viewport.center(), {400, 300}), {150, 137.5}));
    }

    void ctrlWheelZoomsAndWheelScrolls()
    {
        Fixture f;
        const QPointF at = f.view({100, 100});
        QWheelEvent zoom(at, f.canvas.mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&f.canvas, &zoom);
        QVERIFY(f.session.viewport.zoom() > 1);
        QVERIFY(near(f.session.viewport.documentPoint(at, {400, 300}), {100, 100}));
        const QPointF before = f.session.viewport.viewPoint({0, 0}, {400, 300});
        QWheelEvent scroll(at, f.canvas.mapToGlobal(at), QPoint(0, -30), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&f.canvas, &scroll);
        QVERIFY(near(f.session.viewport.viewPoint({0, 0}, {400, 300}), before + QPointF(0, -30), 0.01));
    }

    void handAndSpacePan()
    {
        Fixture f;
        f.session.selectTool(Tool::rectangle);
        const QPointF before = f.session.viewport.viewPoint({0, 0}, {400, 300});
        QTest::keyPress(&f.canvas, Qt::Key_Space);
        // View points, fixed: the document moves under them.
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(300, 250));
        for (const QPoint at : {QPoint(320, 260), QPoint(340, 270), QPoint(350, 280)}) {
            QMouseEvent move(QEvent::MouseMove, at, f.canvas.mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&f.canvas, &move);
        }
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(350, 280));
        QTest::keyRelease(&f.canvas, Qt::Key_Space);
        QVERIFY(near(f.session.viewport.viewPoint({0, 0}, {400, 300}), before + QPointF(50, 30)));
        QVERIFY(f.paths().empty());
    }

    void smartGuidesSnapAMoveToAnEdge()
    {
        Fixture f;
        f.session.usesSmartGuides = true;
        f.session.addPath(Shapes::rectangle({230, 20, 50, 50}), QStringLiteral("Rectangle"));
        const QUuid moving = f.session.addPath(Shapes::rectangle({50, 160, 40, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        // Left edge would land at 227: three points short of the other's 230.
        f.drag({70, 180}, {247, 180});
        QCOMPARE(f.session.document()->bounds(moving).left(), 230.0);
        // Drawn points snap too: a rectangle started near the corner starts on it.
        f.session.selectTool(Tool::rectangle);
        f.drag({283, 72}, {330, 120});
        QCOMPARE(f.session.document()->bounds(f.session.selection().front()).topLeft(), QPointF(280, 70));
    }

    void gridSnapsWhenOn()
    {
        Fixture f;
        f.session.snapsToGrid = true;
        f.session.selectTool(Tool::rectangle);
        f.drag({52, 38}, {148, 123});
        QVERIFY(near(f.session.document()->bounds(f.paths().front()), QRectF(50, 40, 100, 80), 0.01));
    }

    void keysSwitchToolsDeleteAndNudge()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 20, 20}), QStringLiteral("Rectangle"));
        QTest::keyClick(&f.canvas, Qt::Key_P);
        QCOMPARE(f.session.tool(), Tool::pen);
        QTest::keyClick(&f.canvas, Qt::Key_Backslash);
        QCOMPARE(f.session.tool(), Tool::line);
        QTest::keyClick(&f.canvas, Qt::Key_V);
        QCOMPARE(f.session.tool(), Tool::select);
        QTest::keyClick(&f.canvas, Qt::Key_Right);
        QTest::keyClick(&f.canvas, Qt::Key_Down, Qt::ShiftModifier);
        QVERIFY(near(f.session.document()->bounds(id), QRectF(51, 60, 20, 20), 0.01));
        QTest::keyClick(&f.canvas, Qt::Key_Delete);
        QVERIFY(f.paths().empty());
    }

    void escapeCancelsADrag()
    {
        Fixture f;
        const QUuid id = f.session.addPath(Shapes::rectangle({50, 50, 60, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        QTest::mousePress(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({70, 70}));
        f.move({90, 90});
        f.move({120, 120});
        QVERIFY(f.session.isInteracting());
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QTest::mouseRelease(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({120, 120}));
        QVERIFY(near(f.session.document()->bounds(id), QRectF(50, 50, 60, 40), 0.01));
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void doubleClickEntersAGroup()
    {
        Fixture f;
        const QUuid a = f.session.addPath(Shapes::rectangle({50, 50, 40, 40}), QStringLiteral("Rectangle"));
        const QUuid b = f.session.addPath(Shapes::rectangle({150, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        f.session.selectTool(Tool::select);
        f.click({70, 70});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{group});
        QTest::mouseDClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({170, 70}));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{b});
        f.click({70, 70});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{a});
    }

    void marqueeInAnEnteredGroupPicksItsChildren()
    {
        Fixture f;
        const QUuid a = f.session.addPath(Shapes::rectangle({50, 50, 40, 40}), QStringLiteral("Rectangle"));
        const QUuid b = f.session.addPath(Shapes::rectangle({150, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        const QUuid outside = f.session.addPath(Shapes::rectangle({250, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::select);
        QTest::mouseDClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({170, 70}));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{b});
        // Across both children and the outside object: only the children.
        f.drag({40, 40}, {300, 100});
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{a, b}));
        f.drag({140, 40}, {200, 100});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{b});
        // A click on nothing leaves the group; marquees pick top-level objects again.
        f.click({300, 250});
        f.drag({40, 40}, {300, 100});
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{group, outside}));
    }

    void drawingToolsShowGuidesWhileHovering()
    {
        Fixture f;
        f.session.usesSmartGuides = true;
        f.session.addPath(Shapes::rectangle({230, 20, 50, 50}), QStringLiteral("Rectangle"));
        f.session.deselectAll();
        const auto magenta = [&] {
            const QImage image = f.canvas.grab().toImage();
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    // Guide magenta, softened by antialiasing.
                    count += pixel.red() > 150 && pixel.red() - pixel.green() > 80 && pixel.blue() > 90 && pixel.red() > pixel.blue();
                }
            }
            return count;
        };
        // Near the corner, no button held: the next click's snap shows.
        f.session.selectTool(Tool::rectangle);
        f.move({284, 74}, Qt::NoModifier, Qt::NoButton);
        QVERIFY(magenta() > 20);
        f.session.selectTool(Tool::pen);
        f.move({284, 75}, Qt::NoModifier, Qt::NoButton);
        QVERIFY(magenta() > 20);
        // The selection tool shows none until it drags.
        f.session.selectTool(Tool::select);
        f.move({284, 74}, Qt::NoModifier, Qt::NoButton);
        QCOMPARE(magenta(), 0);
    }

    void eyedropperPicksStyle()
    {
        Fixture f;
        const QUuid source = f.session.addPath(Shapes::rectangle({50, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.setFillOfSelection(Paint::solid(Qt::red));
        const QUuid target = f.session.addPath(Shapes::rectangle({150, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.selectTool(Tool::eyedropper);
        f.session.select({target});
        f.click({70, 70});
        QCOMPARE(f.object(target).fill, Paint::solid(Qt::red));
        QCOMPARE(f.object(source).fill, Paint::solid(Qt::red));
    }

    void pointerMovedReportsDocumentPoints()
    {
        Fixture f;
        QSignalSpy moved(&f.canvas, &EditorCanvas::pointerMoved);
        QTest::mouseMove(&f.canvas, f.view({120, 80}));
        QVERIFY(!moved.isEmpty());
        const auto point = moved.last().first().value<std::optional<QPointF>>();
        QVERIFY(point && near(*point, {120, 80}));
        QTest::mouseMove(&f.canvas, QPoint(5, 5));
        QVERIFY(!moved.last().first().value<std::optional<QPointF>>());
    }

    void paintsInEveryMode()
    {
        Fixture f;
        f.session.addPath(Shapes::rectangle({50, 50, 40, 40}), QStringLiteral("Rectangle"));
        f.session.addText({100, 200}, QStringLiteral("Type"));
        f.session.setShowsGrid(true);
        for (const Tool tool : allTools) {
            f.session.selectTool(tool);
            QTest::mouseMove(&f.canvas, f.view({70, 70}));
            QVERIFY(!f.canvas.grab().isNull());
        }
        f.session.setShowsOutline(true);
        QVERIFY(!f.canvas.grab().isNull());
    }
};

QTEST_MAIN(EditorCanvasTests)
#include "EditorCanvasTests.moc"
