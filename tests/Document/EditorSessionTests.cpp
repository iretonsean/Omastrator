#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Rendering/VectorRenderer.h"
#include <QApplication>
#include <QClipboard>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

class EditorSessionTests : public QObject {
    Q_OBJECT

private:
    static QUuid rectangle(EditorSession &session, QRectF rect)
    {
        return session.addPath(Shapes::rectangle(rect), QStringLiteral("Rectangle"));
    }

private slots:
    void newDocumentHasOneLayerAndNoHistory()
    {
        EditorSession session;
        QSignalSpy changed(&session, &EditorSession::changed);
        session.createDocument({612, 792});
        QVERIFY(session.hasDocument());
        QCOMPARE(session.document()->layers().size(), size_t(1));
        QCOMPARE(session.document()->size, QSizeF(612, 792));
        QVERIFY(!session.canUndo());
        QVERIFY(!session.isModified());
        QVERIFY(changed.count() >= 1);
    }

    void drawingAddsToTheActiveLayerAndUndoes()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid id = rectangle(session, {10, 10, 50, 40});
        const auto *object = session.document()->find(id);
        QVERIFY(object);
        QCOMPARE(object->parentID, session.activeLayer());
        QCOMPARE(session.selection(), std::vector<QUuid>{id});
        QCOMPARE(session.document()->bounds(id), QRectF(10, 10, 50, 40));
        QVERIFY(session.isModified());
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));
        session.undo();
        QVERIFY(!session.document()->find(id));
        QVERIFY(session.selection().empty());
        session.redo();
        QVERIFY(session.document()->find(id));
    }

    void interactionPreviewsThenCommitsOneStep()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid id = rectangle(session, {0, 0, 10, 10});
        session.beginInteraction(QStringLiteral("Move"));
        session.previewTransform(QTransform::fromTranslate(5, 0));
        session.previewTransform(QTransform::fromTranslate(20, 30));
        QCOMPARE(session.document()->bounds(id), QRectF(20, 30, 10, 10));
        session.commitInteraction();
        QCOMPARE(session.undoName(), QStringLiteral("Move"));
        session.undo();
        QCOMPARE(session.document()->bounds(id), QRectF(0, 0, 10, 10));
    }

    void cancelledInteractionLeavesNoStep()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid id = rectangle(session, {0, 0, 10, 10});
        session.beginInteraction(QStringLiteral("Move"));
        session.previewTransform(QTransform::fromTranslate(50, 50));
        session.cancelInteraction();
        QCOMPARE(session.document()->bounds(id), QRectF(0, 0, 10, 10));
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void groupAndUngroupKeepOrder()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        const QUuid b = rectangle(session, {20, 0, 10, 10});
        session.select({a, b});
        session.groupSelection();
        QCOMPARE(session.selection().size(), size_t(1));
        const QUuid group = session.selection().front();
        QCOMPARE(session.document()->children(group), (std::vector<QUuid>{a, b}));
        QCOMPARE(session.document()->bounds(group), QRectF(0, 0, 30, 10));
        session.ungroupSelection();
        QVERIFY(!session.document()->find(group));
        QCOMPARE(session.document()->children(*session.activeLayer()), (std::vector<QUuid>{a, b}));
    }

    void arrangeMovesWithinTheParent()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        const QUuid b = rectangle(session, {0, 0, 10, 10});
        const QUuid c = rectangle(session, {0, 0, 10, 10});
        const QUuid layer = *session.activeLayer();
        session.select({a});
        session.arrange(ArrangeOrder::bringForward);
        QCOMPARE(session.document()->children(layer), (std::vector<QUuid>{b, a, c}));
        session.arrange(ArrangeOrder::bringToFront);
        QCOMPARE(session.document()->children(layer), (std::vector<QUuid>{b, c, a}));
        session.arrange(ArrangeOrder::sendBackward);
        QCOMPARE(session.document()->children(layer), (std::vector<QUuid>{b, a, c}));
        session.arrange(ArrangeOrder::sendToBack);
        QCOMPARE(session.document()->children(layer), (std::vector<QUuid>{a, b, c}));
    }

    void alignAndDistribute()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        const QUuid b = rectangle(session, {30, 20, 10, 10});
        const QUuid c = rectangle(session, {100, 50, 10, 10});
        session.select({a, b, c});
        session.align(AlignEdge::top);
        QCOMPARE(session.document()->bounds(b).top(), 0.0);
        QCOMPARE(session.document()->bounds(c).top(), 0.0);
        session.distribute(DistributeAxis::horizontal);
        QCOMPARE(session.document()->bounds(b).center().x(), 55.0);
    }

    void pathfinderUnites()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 20, 20});
        const QUuid b = rectangle(session, {10, 0, 20, 20});
        session.select({a, b});
        QVERIFY(session.canCombine());
        session.combineSelection(BooleanOperation::unite);
        QCOMPARE(session.selection().size(), size_t(1));
        const QUuid result = session.selection().front();
        QVERIFY(!session.document()->find(a));
        QVERIFY(!session.document()->find(b));
        QCOMPARE(session.document()->bounds(result), QRectF(0, 0, 30, 20));
    }

    void pathfinderMinusFront()
    {
        EditorSession session;
        session.createDocument({200, 200});
        rectangle(session, {0, 0, 20, 20});
        rectangle(session, {10, 0, 20, 20});
        session.selectAll();
        session.combineSelection(BooleanOperation::minusFront);
        QCOMPARE(session.document()->bounds(session.selection().front()), QRectF(0, 0, 10, 20));
    }

    void copyPasteOffsets()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        session.select({a});
        session.copy();
        QVERIFY(session.canPaste());
        session.paste();
        QCOMPARE(session.selection().size(), size_t(1));
        QVERIFY(session.selection().front() != a);
        QCOMPARE(session.document()->bounds(session.selection().front()), QRectF(10, 10, 10, 10));
    }

    void codecRoundTrips()
    {
        EditorSession session;
        session.createDocument({300, 150});
        const QUuid a = session.addPath(Shapes::ellipse({0, 0, 40, 20}), QStringLiteral("Ellipse"));
        session.setFillOfSelection(Paint::linear(Qt::red, Qt::blue));
        session.addText({10, 100}, QStringLiteral("Hello"));
        session.select({a});
        session.groupSelection();
        const VectorDocument original = *session.document();
        const QByteArray json = QJsonDocument(DocumentCodec::encode(original)).toJson();
        const VectorDocument decoded = DocumentCodec::decode(QJsonDocument::fromJson(json).object());
        QCOMPARE(decoded.objects.size(), original.objects.size());
        QVERIFY(decoded == original);
    }

    void codecRejectsOrphans()
    {
        QJsonObject json = DocumentCodec::encode(VectorDocument::blank({10, 10}));
        VectorObject orphan;
        json["objects"] = QJsonArray{DocumentCodec::encode(orphan)};
        QVERIFY_THROWS_EXCEPTION(CodecError, DocumentCodec::decode(json));
    }

    void rendererFillsShapes()
    {
        EditorSession session;
        session.createDocument({20, 20});
        session.setDefaultFill(Paint::solid(Qt::red));
        {
            StrokeStyle none;
            none.paint = Paint::none();
            session.setDefaultStroke(none);
        }
        rectangle(session, {0, 0, 10, 20});
        const QImage image = VectorRenderer::render(*session.document(), 1, false);
        QCOMPARE(image.pixelColor(5, 10), QColor(Qt::red));
        QCOMPARE(image.pixelColor(15, 10), QColor(Qt::white));
    }

    void pathsRoundTripThroughQt()
    {
        const VectorPath ellipse = Shapes::ellipse({0, 0, 100, 50});
        const VectorPath back = VectorPath::fromPainterPath(ellipse.painterPath());
        QCOMPARE(back.contours.size(), size_t(1));
        QVERIFY(back.contours.front().closed);
        QCOMPARE(back.contours.front().nodes.size(), size_t(4));
        QCOMPARE(back.bounds(), ellipse.bounds());
    }

    void directSelectionMovesPoints()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        session.selectTool(Tool::directSelect);
        session.pickNodes({{a, {0, 2}}});
        session.moveSelection({5, 5});
        QCOMPARE(session.document()->bounds(a), QRectF(0, 0, 15, 15));
        session.deleteSelection();
        QCOMPARE(session.document()->find(a)->path.nodeCount(), 3);
    }

    void clippingMaskClipsToTheTopObject()
    {
        EditorSession session;
        session.createDocument({40, 20});
        {
            StrokeStyle none;
            none.paint = Paint::none();
            session.setDefaultStroke(none);
        }
        session.setDefaultFill(Paint::solid(Qt::blue));
        const QUuid art = rectangle(session, {0, 0, 40, 20});
        const QUuid clip = rectangle(session, {0, 0, 20, 20});
        session.select({art, clip});
        session.makeClippingMask();
        const QImage image = VectorRenderer::render(*session.document(), 1, false);
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(30, 10), QColor(Qt::white));
    }

    void opacityMaskFadesWithLuminanceAndKeepsThroughRelease()
    {
        EditorSession session;
        session.createDocument({100, 20});
        {
            StrokeStyle none;
            none.paint = Paint::none();
            session.setDefaultStroke(none);
        }
        session.setDefaultFill(Paint::solid(Qt::red));
        const QUuid art = rectangle(session, {0, 0, 100, 20});
        const QUuid maskShape = rectangle(session, {0, 0, 100, 20});
        session.select({maskShape});
        session.setFillOfSelection(Paint::linear(Qt::white, Qt::black));
        session.select({art, maskShape});
        session.makeOpacityMask();
        QCOMPARE(session.undoName(), QStringLiteral("Make Mask"));
        const QUuid group = session.selection().front();
        QVERIFY(session.document()->find(group)->mask.has_value());

        // White end: still strongly red. Black end: faded toward the white page behind it.
        const QImage image = VectorRenderer::render(*session.document(), 1, false);
        QVERIFY(image.pixelColor(2, 10).red() > 200);
        QVERIFY(qGray(image.pixelColor(98, 10).rgb()) > qGray(image.pixelColor(2, 10).rgb()));

        // Invert Mask flips which end fades.
        session.select({group});
        session.setOpacityMaskInverted(true);
        QCOMPARE(session.undoName(), QStringLiteral("Invert Mask"));
        const QImage inverted = VectorRenderer::render(*session.document(), 1, false);
        // Red vs. white only differs in green and blue, so gray (not the red channel) shows the fade.
        QVERIFY(qGray(inverted.pixelColor(98, 10).rgb()) < qGray(image.pixelColor(98, 10).rgb()));
        QVERIFY(qGray(inverted.pixelColor(2, 10).rgb()) > qGray(image.pixelColor(2, 10).rgb()));

        session.setOpacityMaskInverted(false);
        session.setOpacityMaskClip(false);
        QCOMPARE(session.undoName(), QStringLiteral("Mask Clip"));
        QVERIFY(!session.document()->find(group)->mask->clip);

        session.releaseOpacityMask();
        QCOMPARE(session.undoName(), QStringLiteral("Release Mask"));
        QVERIFY(!session.document()->find(group)->mask.has_value());
    }

    void layersMoveAmongLayersAsOneStep()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid first = session.activeLayer().value();
        const QUuid art = rectangle(session, {0, 0, 10, 10});
        const QUuid second = session.addLayer();
        const QUuid third = session.addLayer();
        QCOMPARE(session.document()->layers(), (std::vector<QUuid>{first, second, third}));
        // The bottom layer to the top, its art riding along.
        QVERIFY(session.moveLayer(first, 2));
        QCOMPARE(session.document()->layers(), (std::vector<QUuid>{second, third, first}));
        QCOMPARE(session.document()->find(art)->parentID, std::optional(first));
        QCOMPARE(session.undoName(), QStringLiteral("Move Layer"));
        session.undo();
        QCOMPARE(session.document()->layers(), (std::vector<QUuid>{first, second, third}));
        QVERIFY(session.moveLayer(third, 0));
        QCOMPARE(session.document()->layers(), (std::vector<QUuid>{third, first, second}));
        // Where it already is: no step. Art never goes to the top level, layers never into one another.
        const QString before = session.undoName();
        QVERIFY(!session.moveLayer(third, 0));
        QCOMPARE(session.undoName(), before);
        QVERIFY(!session.moveLayer(art, 0));
        QVERIFY(!session.moveObject(second, first, 0));
        QCOMPARE(session.document()->find(art)->parentID, std::optional(first));
    }

    void duplicateWithinAnInteractionIsOneStep()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid original = rectangle(session, {10, 10, 20, 20});
        session.beginInteraction(QStringLiteral("Move Copy"));
        session.previewDuplicateSelection();
        const QUuid copy = session.selection().front();
        QVERIFY(copy != original);
        session.previewTransform(QTransform::fromTranslate(50, 0));
        session.previewTransform(QTransform::fromTranslate(100, 0));
        QCOMPARE(session.document()->bounds(original), QRectF(10, 10, 20, 20));
        QCOMPARE(session.document()->bounds(copy), QRectF(110, 10, 20, 20));
        session.commitInteraction();
        QCOMPARE(session.undoName(), QStringLiteral("Move Copy"));
        session.undo();
        QVERIFY(!session.document()->find(copy));
        QCOMPARE(session.document()->bounds(original), QRectF(10, 10, 20, 20));
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));
        // Cancelled: no copy, the original selected again.
        session.select({original});
        session.beginInteraction(QStringLiteral("Move Copy"));
        session.previewDuplicateSelection();
        session.previewTransform(QTransform::fromTranslate(30, 0));
        session.cancelInteraction();
        QCOMPARE(session.document()->children(session.activeLayer().value()).size(), size_t(1));
        QCOMPARE(session.selection(), std::vector<QUuid>{original});
    }

    void switchingToolsDropsALoneAnchorPenPath()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid kept = rectangle(session, {10, 10, 20, 20});
        session.selectTool(Tool::pen);
        session.beginInteraction(QStringLiteral("Draw Path"));
        VectorPath path;
        path.contours.push_back({});
        path.contours.back().nodes.push_back(PathNode(QPointF(50, 50)));
        const QUuid lone = session.previewAddObject(session.pathObject(path, QStringLiteral("Path")));
        session.selectTool(Tool::select);
        QVERIFY(!session.isInteracting());
        QVERIFY(!session.document()->find(lone));
        QVERIFY(session.document()->find(kept));
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle"));
    }

    void splittingASegmentKeepsItsShape()
    {
        VectorPath path;
        Contour contour;
        contour.nodes = {PathNode({0, 0}, {0, 0}, {0, -50}), PathNode({100, 0}, {100, -50}, {100, 0})};
        path.contours = {contour};
        const VectorPath before = path;
        const auto hit = path.hitSegment({50, -37.5}, 2);
        QVERIFY(hit);
        QVERIFY(std::abs(hit->t - 0.5) < 0.01);
        const NodeRef added = path.splitSegment(hit->from, hit->t);
        QCOMPARE(added, (NodeRef{0, 1}));
        QCOMPARE(path.nodeCount(), 3);
        QVERIFY(path.node(added)->smooth);
        QVERIFY(QLineF(path.node(added)->anchor, QPointF(50, -37.5)).length() < 0.1);
        // Same outline: every sampled point of the old curve lies on the new one.
        for (int step = 0; step <= 20; ++step) {
            const QPointF point = before.painterPath().pointAtPercent(step / 20.0);
            QVERIFY2(path.distanceToOutline(point) < 0.1, qPrintable(QString::number(step)));
        }
        // A straight side splits into a corner with no handles.
        VectorPath line;
        line.contours = {Contour{{PathNode({0, 0}), PathNode({10, 0})}, false}};
        line.splitSegment({0, 0}, 0.25);
        QCOMPARE(line.contours.front().nodes[1], PathNode(QPointF(2.5, 0)));
        // Reversed runs the other way with handles swapped.
        const Contour back = reversed(contour);
        QCOMPARE(back.nodes.front().anchor, QPointF(100, 0));
        QCOMPARE(back.nodes.front().out, QPointF(100, -50));
    }

    // A scrub's edit is open while undo arrives: the undone step must stay undone, and the scrub must record on top.
    void undoAndRedoWaitForAnOpenEdit()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid first = rectangle(session, {10, 10, 50, 40});
        const QUuid second = rectangle(session, {80, 10, 50, 40});
        session.undo();
        QVERIFY(!session.document()->find(second));
        session.beginEdit(QStringLiteral("Scrub"));
        session.rename(first, QStringLiteral("Moved"));
        // Neither undo nor redo runs inside the open edit.
        session.undo();
        session.redo();
        QCOMPARE(session.document()->find(first)->name, QStringLiteral("Moved"));
        QVERIFY(!session.document()->find(second));
        session.endEdit();
        QCOMPARE(session.undoName(), QStringLiteral("Scrub"));
        QVERIFY(!session.canRedo());
        // Undoing the scrub does not bring the earlier undone step back.
        session.undo();
        QVERIFY(session.document()->find(first));
        QVERIFY(!session.document()->find(second));
        QCOMPARE(session.document()->find(first)->name, QStringLiteral("Rectangle"));
    }
};

QTEST_MAIN(EditorSessionTests)
#include "EditorSessionTests.moc"
