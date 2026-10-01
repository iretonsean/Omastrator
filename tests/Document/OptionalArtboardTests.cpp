#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Rendering/VectorRenderer.h"
#include <QTest>

// A page may have no artboard: every place that assumed one handles zero.
namespace {
QUuid rectangle(EditorSession &session, QRectF rect)
{
    return session.addPath(Shapes::rectangle(rect), QStringLiteral("Rectangle"));
}

// A one-artboard document with the artboard deleted.
void withoutArtboard(EditorSession &session, QSizeF size = {200, 100})
{
    session.createDocument(size);
    session.deleteArtboard(0);
}
}

class OptionalArtboardTests : public QObject {
    Q_OBJECT

private slots:
    void deletingTheLastArtboardIsOneUndoStep()
    {
        EditorSession session;
        session.createDocument({200, 100});
        const VectorDocument before = *session.document();
        session.deleteArtboard(0);
        QCOMPARE(session.document()->artboardCount(), 0);
        QCOMPARE(session.activeArtboard(), -1);
        QCOMPARE(session.undoName(), QStringLiteral("Delete Artboard"));
        session.undo();
        QCOMPARE(session.document()->artboardCount(), 1);
        QCOMPARE(session.document()->artboard(0).rect, QRectF(0, 0, 200, 100));
        QVERIFY(*session.document() == before);
        session.redo();
        QCOMPARE(session.document()->artboardCount(), 0);
    }

    void aSecondPageDeleteAndAddBringsOneBack()
    {
        EditorSession session;
        withoutArtboard(session);
        // Artboard-only commands do nothing with none; New Artboard starts from the document's size.
        session.setArtboardSize({50, 50});
        session.switchArtboardOrientation(0);
        QCOMPARE(session.document()->artboardCount(), 0);
        session.addArtboard();
        QCOMPARE(session.document()->artboardCount(), 1);
        QCOMPARE(session.document()->artboard(0).rect.size(), QSizeF(200, 100));
        QCOMPARE(session.activeArtboard(), 0);
    }

    void newPageMakesNoArtboardAndKeepsTheOthers()
    {
        EditorSession session;
        session.createDocument({300, 200});
        session.addPage(QStringLiteral("Two"));
        QCOMPARE(session.document()->artboardCount(), 0);
        QCOMPARE(session.activeArtboard(), -1);
        QCOMPARE(session.undoName(), QStringLiteral("New Page"));
        session.showPage(false);
        QCOMPARE(session.document()->artboardCount(), 1);
        QCOMPARE(session.document()->artboard(0).rect.size(), QSizeF(300, 200));
        // The page with none keeps none through a duplicate.
        session.duplicatePage(session.document()->allPages()[1].id);
        QCOMPARE(session.document()->pageCount(), 3);
        QCOMPARE(session.document()->artboardCount(), 0);
    }

    void aDocumentWithoutArtboardsStaysSizedAndSavesAsZero()
    {
        EditorSession session;
        withoutArtboard(session, {640, 480});
        QCOMPARE(session.document()->size, QSizeF(640, 480));
        QCOMPARE(session.document()->viewSize(), QSizeF(640, 480));
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(*session.document()));
        QCOMPARE(back.artboardCount(), 0);
        QVERIFY(back.artboards.empty());
        QCOMPARE(back.size, QSizeF(640, 480));
        // With pages: one page without, one with.
        session.createDocument({100, 100});
        session.addPage(QStringLiteral("Two"));
        const VectorDocument pages = DocumentCodec::decode(DocumentCodec::encode(*session.document()));
        QCOMPARE(pages.artboardsOn(pages.allPages()[0].id).size(), size_t(1));
        QCOMPARE(pages.artboardsOn(pages.allPages()[1].id).size(), size_t(0));
    }

    void anOldFileWithArtboardsLoadsUnchanged()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 400, 300), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(500, 0, 100, 100), Qt::white}});
        QJsonObject json = DocumentCodec::encode(document);
        QVERIFY(!json.contains(QStringLiteral("artboardsListed")));
        QCOMPARE(DocumentCodec::decode(json).artboardCount(), 2);
        // A file with no artboard key at all is still one implicit artboard.
        json.remove(QStringLiteral("artboards"));
        QCOMPARE(DocumentCodec::decode(json).artboardCount(), 1);
    }

    void paintingWithoutAnArtboardDrawsNoPaper()
    {
        EditorSession session;
        withoutArtboard(session);
        rectangle(session, {20, 20, 10, 10});
        QImage image(200, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        VectorRenderer::draw(painter, *session.document(), {});
        painter.end();
        // No paper: the corner stays clear, the object is there.
        QCOMPARE(image.pixelColor(2, 2).alpha(), 0);
        QVERIFY(image.pixelColor(25, 25).alpha() > 0);
        // render() crops to the content instead of the document size.
        const QImage cropped = VectorRenderer::render(*session.document(), 1, false);
        QVERIFY(cropped.width() <= 12 && cropped.width() >= 10);
    }

    void fitFallsBackToFramesThenContentThenTheDefaultView()
    {
        EditorSession session;
        withoutArtboard(session);
        session.viewport.viewSize = QSizeF(800, 600);
        // Nothing: the default view, no crash.
        session.zoomToFit();
        session.fitAllArtboards();
        QVERIFY(session.viewport.zoom() > 0);
        // Content far away: the fit finds it.
        rectangle(session, {5000, 5000, 100, 100});
        session.zoomToFit();
        const QPointF shown = session.viewport.viewPoint(QPointF(5050, 5050), session.document()->viewSize());
        QVERIFY(QRectF(0, 0, 800, 600).contains(shown));
        // A frame wins over the content outside it.
        const QUuid frame = session.addFrame({-3000, -3000, 200, 200});
        QVERIFY(!frame.isNull());
        QCOMPARE(session.document()->viewBounds(), session.document()->bounds(std::vector<QUuid>{frame}, true));
        session.fitAllArtboards();
        const QPointF inFrame = session.viewport.viewPoint(QPointF(-2900, -2900), session.document()->viewSize());
        QVERIFY(QRectF(0, 0, 800, 600).contains(inFrame));
    }

    void placingAndAligningWorkWithoutAnArtboard()
    {
        EditorSession session;
        withoutArtboard(session);
        const QUuid a = rectangle(session, {0, 0, 10, 10});
        const QUuid b = rectangle(session, {50, 30, 10, 10});
        session.select({a, b});
        session.align(AlignEdge::left, AlignTarget::artboard);
        QCOMPARE(session.document()->bounds(b).left(), 0.0);
        // One object has nothing to align to: no edit.
        session.select({a});
        const QString name = session.undoName();
        session.align(AlignEdge::right, AlignTarget::artboard);
        QCOMPARE(session.undoName(), name);
        QCOMPARE(session.paperRect(), session.document()->viewBounds());
    }
};

QTEST_MAIN(OptionalArtboardTests)
#include "OptionalArtboardTests.moc"
