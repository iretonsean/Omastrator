#include "Canvas/EditorCanvas.h"
#include "Canvas/Rulers.h"
#include <QApplication>
#include <QInputDialog>
#include <QTest>
#include <cmath>
#include <numbers>

// The canvas side of rulers and guides, snap to pixel, live corners, scissors,
// isolation and the key object.
namespace {
struct Fixture {
    EditorSession session;
    EditorCanvas canvas{session};

    Fixture()
    {
        session.createDocument({400, 300});
        // Smart guides stay off: ruler guides must snap without them.
        session.usesSmartGuides = false;
        canvas.resize(800, 600);
        canvas.show();
        if (!QTest::qWaitForWindowExposed(&canvas))
            qWarning("canvas never exposed");
        session.actualSize();
        canvas.setFocus();
    }

    QPointF view(QPointF document) const { return session.viewport.viewPoint(document, session.document()->size); }
    QPointF document(QPointF view) const { return session.viewport.documentPoint(view, session.document()->size); }

    void moveTo(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier, Qt::MouseButtons buttons = Qt::LeftButton)
    {
        QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }

    // A drag in view points.
    void dragView(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, modifiers, from.toPoint());
        for (int step = 1; step <= 4; ++step)
            moveTo(from + (to - from) * step / 4.0, modifiers);
        QTest::mouseRelease(&canvas, Qt::LeftButton, modifiers, to.toPoint());
    }

    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { dragView(view(from), view(to), modifiers); }
    void click(QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QTest::mouseClick(&canvas, Qt::LeftButton, modifiers, view(at).toPoint());
    }
    void doubleClick(QPointF at) { QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(at).toPoint()); }

    QUuid box(const QRectF &rect, const QColor &color = Qt::black)
    {
        VectorObject object = session.pathObject(Shapes::rectangle(rect), QStringLiteral("Box"));
        object.fill = Paint::solid(color);
        object.stroke.paint = Paint::none();
        return session.addObject(object, QStringLiteral("Draw Box"));
    }

    QUuid liveBox(const QRectF &rect)
    {
        LiveRectangle shape;
        shape.rect = rect;
        VectorObject object = session.pathObject({}, QStringLiteral("Rectangle"));
        EditorSession::reshape(object, shape);
        return session.addObject(object, QStringLiteral("Draw Rectangle"));
    }

    QColor pixel(QPointF documentPoint)
    {
        return canvas.grab().toImage().pixelColor(view(documentPoint).toPoint());
    }
};
}

class GuidesCornersTests : public QObject {
    Q_OBJECT

private slots:
    void rulersShowAndFollowThePointer()
    {
        Fixture f;
        auto *rulers = f.canvas.findChild<Rulers *>();
        QVERIFY(rulers && !rulers->isVisible());
        f.session.setShowsRulers(true);
        QVERIFY(rulers->isVisible());
        QCOMPARE(rulers->geometry(), f.canvas.rect());
        f.moveTo(QPointF(300, 200), Qt::NoModifier, Qt::NoButton);
        QCOMPARE(rulers->marker(), std::optional(QPointF(300, 200)));
        // Labels at least 50 view points apart, on 1, 2 and 5 steps.
        QCOMPARE(Rulers::labelStep(1), 50.0);
        QCOMPARE(Rulers::labelStep(4), 20.0);
        QCOMPARE(Rulers::labelStep(0.1), 500.0);
    }

    void aGuideDrawsOutOfARuler()
    {
        Fixture f;
        f.session.setShowsRulers(true);
        const QPointF target = f.view({150, 120});
        f.dragView(QPointF(target.x(), 6), target);
        QCOMPARE(f.session.document()->guides.size(), size_t(1));
        const Guide guide = f.session.document()->guides.front();
        QCOMPARE(guide.orientation, Qt::Horizontal);
        QVERIFY(std::abs(guide.position - 120) < 1);
        QCOMPARE(f.session.undoName(), QString("Add Guide"));
        // The left ruler gives a vertical one; dropping it back on the ruler makes none.
        f.dragView(QPointF(6, target.y()), target);
        QCOMPARE(f.session.document()->guides.back().orientation, Qt::Vertical);
        f.dragView(QPointF(6, target.y()), QPointF(8, target.y() + 20));
        QCOMPARE(f.session.document()->guides.size(), size_t(2));
    }

    void movedObjectsSnapToGuides()
    {
        Fixture f;
        f.session.addGuide({Qt::Vertical, 200});
        const QUuid id = f.box({100, 100, 40, 40});
        // Left edge dragged to 197: within the tolerance, it lands on the guide.
        f.drag({120, 120}, {217, 124});
        QCOMPARE(f.session.document()->bounds(id).left(), 200.0);
        // Hidden guides pull nothing.
        f.session.undo();
        f.session.setShowsGuides(false);
        f.drag({120, 120}, {217, 124});
        QCOMPARE(f.session.document()->bounds(id).left(), 197.0);
    }

    void guidesMoveUnlessLocked()
    {
        Fixture f;
        f.session.setShowsRulers(true);
        f.session.addGuide({Qt::Vertical, 300});
        f.session.setGuidesLocked(true);
        f.drag({300, 200}, {320, 200});
        QCOMPARE(f.session.document()->guides.front().position, 300.0);
        f.session.setGuidesLocked(false);
        f.drag({300, 200}, {320, 200});
        QCOMPARE(f.session.document()->guides.front().position, 320.0);
        QCOMPARE(f.session.undoName(), QString("Move Guide"));
        // Dragged back onto its ruler, it goes.
        f.dragView(f.view({320, 200}), QPointF(8, f.view({320, 200}).y()));
        QVERIFY(f.session.document()->guides.empty());
        QCOMPARE(f.session.undoName(), QString("Delete Guide"));
    }

    void doubleClickTypesAGuidesPlace()
    {
        Fixture f;
        f.session.addGuide({Qt::Horizontal, 150});
        f.doubleClick({200, 150});
        auto *dialog = f.canvas.findChild<QInputDialog *>(QStringLiteral("guidePositionDialog"));
        QVERIFY(dialog);
        QCOMPARE(dialog->doubleValue(), 150.0);
        dialog->setDoubleValue(42.5);
        dialog->accept();
        QCOMPARE(f.session.document()->guides.front().position, 42.5);
    }

    void snapToPixelLandsOnWholePoints()
    {
        Fixture f;
        f.session.setZoom(3, QPointF(400, 300));
        f.session.selectTool(Tool::rectangle);
        const QPointF from(301, 203), to(377, 259);
        f.dragView(from, to);
        const QRectF loose = f.session.document()->bounds(f.session.selection().front());
        QVERIFY(loose.left() != std::round(loose.left()) || loose.right() != std::round(loose.right()));
        f.session.setSnapsToPixel(true);
        f.dragView(from, to);
        const QRectF snapped = f.session.document()->bounds(f.session.selection().front());
        for (const double edge : {snapped.left(), snapped.top(), snapped.right(), snapped.bottom()})
            QCOMPARE(edge, std::round(edge));
        // Moving keeps it on whole points.
        f.session.selectTool(Tool::select);
        f.dragView(f.view(snapped.center()), f.view(snapped.center()) + QPointF(7, 5));
        const QRectF moved = f.session.document()->bounds(f.session.selection().front());
        QVERIFY(moved != snapped);
        QCOMPARE(moved.left(), std::round(moved.left()));
        QCOMPARE(moved.top(), std::round(moved.top()));
    }

    void thePixelGridShowsFrom600Percent()
    {
        Fixture f;
        const auto marked = [&] {
            const QImage image = f.canvas.grab().toImage();
            int count = 0;
            const QRect area = QRectF(f.view({2, 2}), f.view({6, 6})).toRect().intersected(image.rect());
            for (int y = area.top(); y <= area.bottom(); ++y) {
                for (int x = area.left(); x <= area.right(); ++x)
                    count += image.pixelColor(x, y) != QColor(Qt::white);
            }
            return count;
        };
        f.session.setZoom(5, f.view({4, 4}));
        QCOMPARE(marked(), 0);
        f.session.setZoom(8, f.view({4, 4}));
        QVERIFY(marked() > 0);
        f.session.setShowsPixelGrid(false);
        QCOMPARE(marked(), 0);
    }

    void scissorsCutWhereClicked()
    {
        Fixture f;
        const QUuid id = f.box({100, 100, 100, 100});
        f.session.selectTool(Tool::scissors);
        f.click({150, 100});
        QCOMPARE(f.session.undoName(), QString("Cut Path"));
        const Contour &contour = f.session.document()->find(id)->path.contours.front();
        QVERIFY(!contour.closed);
        QVERIFY(QLineF(contour.nodes.front().anchor, QPointF(150, 100)).length() < 1);
        QVERIFY(QLineF(contour.nodes.back().anchor, QPointF(150, 100)).length() < 1);
        // C picks it from the keyboard.
        f.session.selectTool(Tool::select);
        QTest::keyClick(&f.canvas, Qt::Key_C);
        QCOMPARE(f.session.tool(), Tool::scissors);
    }

    void theRectangleToolsDrawLiveShapes()
    {
        Fixture f;
        f.session.selectTool(Tool::roundedRectangle);
        f.session.cornerRadius = 9;
        f.drag({50, 50}, {150, 110});
        const LiveRectangle *shape = f.session.document()->find(f.session.selection().front())->liveShape();
        QVERIFY(shape);
        QCOMPARE(shape->rect, QRectF(50, 50, 100, 60));
        QCOMPARE(shape->radii, (std::array<double, 4>{9, 9, 9, 9}));
        f.session.selectTool(Tool::rectangle);
        f.drag({200, 50}, {260, 110});
        QVERIFY(f.session.document()->find(f.session.selection().front())->liveShape());
    }

    void cornerWidgetsSetTheRadius()
    {
        Fixture f;
        const QUuid id = f.liveBox({100, 100, 120, 80});
        f.session.selectTool(Tool::directSelect);
        f.session.select({id});
        // A square corner's widget rests 14 view points in along the diagonal, about (9.9, 9.9).
        const QPointF widget = f.view({100, 100}) + QPointF(10, 10);
        f.dragView(widget, widget + QPointF(10, 10));
        QCOMPARE(f.session.undoName(), QString("Corner Radius"));
        const LiveRectangle *shape = f.session.document()->find(id)->liveShape();
        QVERIFY(shape);
        for (const double radius : shape->radii)
            QVERIFY(std::abs(radius - 10) < 0.01);
        // Alt drags one corner alone: the top right's widget now sits at its arc's centre.
        const QPointF topRight = f.view({210, 110});
        f.dragView(topRight, topRight + QPointF(-5, 5), Qt::AltModifier);
        shape = f.session.document()->find(id)->liveShape();
        QVERIFY(std::abs(shape->radii[1] - 15) < 0.01);
        QVERIFY(std::abs(shape->radii[0] - 10) < 0.01 && std::abs(shape->radii[2] - 10) < 0.01);
        // Alt-click cycles the style: round, inverted, chamfer.
        const QPointF bottomLeft = f.view({110, 170});
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::AltModifier, bottomLeft.toPoint());
        QCOMPARE(f.session.document()->find(id)->liveShape()->styles[3], CornerStyle::inverted);
        QCOMPARE(f.session.undoName(), QString("Corner Style"));
    }

    void isolationDimsTheRestAndLeavesOnEscape()
    {
        Fixture f;
        const QUuid a = f.box({50, 50, 40, 40});
        const QUuid b = f.box({150, 50, 40, 40});
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        const QUuid outside = f.box({250, 50, 40, 40});
        f.session.deselectAll();
        QCOMPARE(f.pixel({270, 70}), QColor(Qt::black));
        f.doubleClick({170, 70});
        QCOMPARE(f.session.isolatedGroup(), std::optional(group));
        QCOMPARE(f.session.selection(), std::vector<QUuid>{b});
        // What's outside fades to half and can't be picked.
        const QColor faded = f.pixel({270, 70});
        QVERIFY(faded.red() > 100 && faded.red() < 160);
        QCOMPARE(f.pixel({70, 70}), QColor(Qt::black));
        f.click({270, 70});
        QVERIFY(f.session.selection().empty());
        QVERIFY(f.session.isolatedGroup());
        // New objects land inside.
        f.session.selectTool(Tool::rectangle);
        f.drag({60, 150}, {100, 190});
        QCOMPARE(f.session.document()->find(f.session.selection().front())->parentID, std::optional(group));
        f.session.selectTool(Tool::select);
        QTest::keyClick(&f.canvas, Qt::Key_Escape);
        QVERIFY(!f.session.isolatedGroup());
        QCOMPARE(f.pixel({270, 70}), QColor(Qt::black));
        f.click({270, 70});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{outside});
    }

    void doubleClickOutsideStepsOut()
    {
        Fixture f;
        const QUuid a = f.box({50, 50, 40, 40});
        const QUuid b = f.box({150, 50, 40, 40});
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        f.doubleClick({170, 70});
        QCOMPARE(f.session.isolatedGroup(), std::optional(group));
        f.doubleClick({350, 250});
        QVERIFY(!f.session.isolatedGroup());
        QCOMPARE(f.session.selection(), std::vector<QUuid>{group});
    }

    void aSecondClickMakesTheKeyObject()
    {
        Fixture f;
        const QUuid a = f.box({50, 50, 40, 40});
        const QUuid b = f.box({150, 100, 40, 40});
        f.session.select({a, b});
        f.click({170, 120});
        QCOMPARE(f.session.keyObject(), std::optional(b));
        QCOMPARE(f.session.selection().size(), size_t(2));
        // Aligned to it: the key stays, the other moves.
        f.session.align(AlignEdge::top);
        QCOMPARE(f.session.document()->bounds(a).top(), 100.0);
        QCOMPARE(f.session.document()->bounds(b).top(), 100.0);
        // Its heavy outline shows.
        const QColor layer = f.session.document()->find(f.session.document()->layers().front())->layerColor;
        QCOMPARE(f.pixel({150, 100}).name(), layer.name());
        // Clicked again, it's an ordinary selected object.
        f.click({170, 120});
        QVERIFY(!f.session.keyObject());
        // A drag is a move, not a key.
        f.drag({170, 120}, {180, 130});
        QVERIFY(!f.session.keyObject());
    }
};

QTEST_MAIN(GuidesCornersTests)
#include "GuidesCornersTests.moc"
