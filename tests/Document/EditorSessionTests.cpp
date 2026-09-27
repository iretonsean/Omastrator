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
};

QTEST_MAIN(EditorSessionTests)
#include "EditorSessionTests.moc"
