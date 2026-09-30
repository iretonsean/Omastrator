#include "Canvas/EditorCanvas.h"
#include <QApplication>
#include <QTest>

// Frames on the canvas as Figma's: the handles resize the box and leave type alone, what's drawn over a frame goes in
// it, and a move drops the selection into the frame under the pointer or out of it.
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

    void drag(QPointF from, QPointF to)
    {
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, view(from));
        for (int step = 1; step <= 4; ++step) {
            const QPointF at = view(from + (to - from) * step / 4.0);
            QMouseEvent event(QEvent::MouseMove, at, canvas.mapToGlobal(at), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &event);
        }
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(to));
    }

    template <typename Change> void edit(Change change)
    {
        VectorDocument document = *session.document();
        change(document);
        session.loadDocument(document);
    }

    const VectorObject &read(const QUuid &id) const { return *session.document()->find(id); }

    // The one text object in the document.
    std::optional<QUuid> text() const
    {
        for (const VectorObject &object : session.document()->objects) {
            if (object.kind == ObjectKind::text)
                return object.id;
        }
        return std::nullopt;
    }

    // Types `words` as point type at `at` with the Type tool, then leaves it.
    QUuid type(QPointF at, const QString &words)
    {
        session.selectTool(Tool::text);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(at));
        QTest::keyClicks(&canvas, words);
        QTest::keyClick(&canvas, Qt::Key_Escape);
        session.selectTool(Tool::select);
        return text().value_or(QUuid());
    }
};
}

class FrameCanvasTests : public QObject {
    Q_OBJECT

private slots:
    void theHandlesResizeAFrameAndLeaveItsTypeAlone()
    {
        Fixture f;
        const QUuid frame = f.session.addFrame({50, 50, 200, 150});
        VectorObject words = f.session.textObject({70, 90}, QStringLiteral("Hello"));
        const double size = words.text.size;
        f.edit([&](VectorDocument &document) { document.insert(words, frame); });
        f.session.selectTool(Tool::select);
        f.session.select({frame});
        f.drag({250, 200}, {330, 260});
        const QRectF box = f.session.document()->bounds(frame);
        QVERIFY(std::abs(box.width() - 280) < 1 && std::abs(box.height() - 210) < 1);
        QCOMPARE(f.read(words.id).text.size, size);
        QVERIFY(f.read(words.id).transform.type() <= QTransform::TxTranslate);
        QCOMPARE(f.session.undoName(), QStringLiteral("Scale"));
    }

    void theHandlesResizeAnImportedFramesTranslatedBox()
    {
        Fixture f;
        const QUuid frame = f.session.addFrame({50, 50, 200, 150});
        VectorObject words = f.session.textObject({70, 90}, QStringLiteral("Hello"));
        const double size = words.text.size;
        f.edit([&](VectorDocument &document) {
            document.insert(words, frame);
            VectorObject *object = document.find(frame);
            object->shape->rect = QRectF(0, 0, 200, 150);
            object->shape->placement = QTransform::fromTranslate(50, 50);
            object->path = object->shape->path();
        });
        f.session.selectTool(Tool::select);
        f.session.select({frame});
        f.drag({250, 200}, {330, 260});
        QCOMPARE(f.read(words.id).text.size, size);
        QVERIFY(f.read(words.id).transform.type() <= QTransform::TxTranslate);
        QVERIFY(QLineF(f.read(words.id).transform.map(QPointF()), QPointF(70, 90)).length() < 1e-6);
    }

    void typeTypedOverAFrameGoesInItAndKeepsItsSize()
    {
        Fixture f;
        const QUuid frame = f.session.addFrame({50, 50, 200, 150});
        const QUuid text = f.type({80, 100}, QStringLiteral("Hi"));
        QVERIFY(!text.isNull());
        QCOMPARE(f.read(text).parentID, frame);
        const double size = f.read(text).text.size;
        f.session.select({frame});
        f.drag({250, 200}, {330, 260});
        QCOMPARE(f.read(text).text.size, size);
        QVERIFY(f.read(text).transform.type() <= QTransform::TxTranslate);
        // Outside every frame it goes on the page, even with the type in the frame selected.
        f.session.select({text});
        f.session.selectTool(Tool::rectangle);
        f.drag({340, 20}, {380, 40});
        QCOMPARE(f.read(f.session.selection().front()).parentID, f.session.document()->find(frame)->parentID);
    }

    void shapesPenAndPencilDrawnOverAFrameGoInIt()
    {
        Fixture f;
        const QUuid outer = f.session.addFrame({50, 50, 300, 200});
        const QUuid inner = f.session.addFrame({200, 100, 100, 100});
        QCOMPARE(f.read(inner).parentID, outer);
        f.session.selectTool(Tool::rectangle);
        f.drag({70, 70}, {120, 120});
        QCOMPARE(f.read(f.session.selection().front()).parentID, outer);
        // The innermost frame under the press takes it.
        f.drag({220, 120}, {260, 160});
        QCOMPARE(f.read(f.session.selection().front()).parentID, inner);
        f.session.selectTool(Tool::pencil);
        f.drag({70, 150}, {150, 190});
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Path"));
        QCOMPARE(f.read(f.session.selection().front()).parentID, outer);
        f.session.selectTool(Tool::pen);
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({230, 180}));
        QTest::mouseClick(&f.canvas, Qt::LeftButton, Qt::NoModifier, f.view({280, 190}));
        QTest::keyClick(&f.canvas, Qt::Key_Return);
        QCOMPARE(f.read(f.session.selection().front()).parentID, inner);
    }

    void aMoveDropsIntoTheFrameUnderThePointerAndOutAgain()
    {
        Fixture f;
        const QUuid frame = f.session.addFrame({150, 50, 200, 200});
        const QUuid square = f.session.addPath(Shapes::rectangle({20, 20, 40, 40}), QStringLiteral("Square"));
        const std::optional<QUuid> layer = f.read(square).parentID;
        QVERIFY(layer != frame);
        f.session.selectTool(Tool::select);
        f.session.select({square});
        f.drag({40, 40}, {200, 100});
        QCOMPARE(f.read(square).parentID, frame);
        QCOMPARE(f.session.document()->bounds(square), QRectF(180, 80, 40, 40));
        QCOMPARE(f.session.undoName(), QStringLiteral("Move"));
        // The move and the new parent are one step.
        f.session.undo();
        QCOMPARE(f.read(square).parentID, layer);
        QCOMPARE(f.session.document()->bounds(square), QRectF(20, 20, 40, 40));
        f.session.redo();
        QCOMPARE(f.read(square).parentID, frame);
        // Let go outside every frame and it leaves, just above the frame.
        f.session.select({square});
        f.drag({200, 100}, {60, 200});
        QCOMPARE(f.read(square).parentID, layer);
        const std::vector<QUuid> siblings = f.session.document()->children(layer);
        const auto at = std::find(siblings.begin(), siblings.end(), square);
        QVERIFY(at != siblings.begin() && *(at - 1) == frame);
    }

    void aGroupKeepsItsChildrenWhenOneIsDraggedOverAFrame()
    {
        Fixture f;
        const QUuid frame = f.session.addFrame({150, 50, 200, 200});
        const QUuid a = f.session.addPath(Shapes::rectangle({20, 20, 20, 20}), QStringLiteral("A"));
        const QUuid b = f.session.addPath(Shapes::rectangle({20, 60, 20, 20}), QStringLiteral("B"));
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        f.session.selectTool(Tool::select);
        // The Selection tool drags the whole group, which joins the frame.
        f.drag({30, 30}, {200, 100});
        QCOMPARE(f.read(group).parentID, frame);
        QCOMPARE(f.read(a).parentID, group);
    }
};

QTEST_MAIN(FrameCanvasTests)
#include "FrameCanvasTests.moc"
