#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QJsonDocument>
#include <QTest>

// Multiple artboards (docs/QOL-RESEARCH.md P2-1): the model, the EditorSession API, and
// the .omai format's version 5.
class ArtboardsTests : public QObject {
    Q_OBJECT

private:
    static QUuid rectangle(EditorSession &session, QRectF rect)
    {
        return session.addPath(Shapes::rectangle(rect), QStringLiteral("Rectangle"));
    }

private slots:
    void emptyArtboardsGivesOneImplicitAtTheOrigin()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        QCOMPARE(document.artboardCount(), 1);
        const std::vector<Artboard> boards = document.allArtboards();
        QCOMPARE(boards.size(), size_t(1));
        QCOMPARE(boards.front().name, QStringLiteral("Artboard 1"));
        QCOMPARE(boards.front().rect, QRectF(0, 0, 400, 300));
        QCOMPARE(boards.front().background, document.background);
    }

    void setArtboardsMakesTheFirstOneTheDocumentSSizeAndBackground()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 500, 200), Qt::black},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(600, 0, 100, 100), Qt::white}});
        QCOMPARE(document.size, QSizeF(500, 200));
        QCOMPARE(document.background, QColor(Qt::black));
        QCOMPARE(document.artboardCount(), 2);
        // Growing `size` alone keeps the first artboard's size in step.
        document.size = QSizeF(700, 250);
        QCOMPARE(document.allArtboards().front().rect, QRectF(0, 0, 700, 250));
        // Empty leaves none: the list is the truth now, and `size` stays as it was.
        document.setArtboards({});
        QCOMPARE(document.artboardCount(), 0);
        QCOMPARE(document.size, QSizeF(700, 250));
    }

    void artboardAtFindsTheLastOneListedFirst()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 100, 100), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(50, 50, 100, 100), Qt::white}});
        // The overlap goes to B, listed last.
        QCOMPARE(document.artboardAt({75, 75}), 1);
        QCOMPARE(document.artboardAt({10, 10}), 0);
        QCOMPARE(document.artboardAt({500, 500}), -1);
    }

    void objectsOnAnArtboardAreThoseThatOverlapItWithMoreThanOne()
    {
        VectorDocument document = VectorDocument::blank({300, 300});
        const QUuid layer = document.layers().front();
        VectorObject onFirst;
        onFirst.path = Shapes::rectangle({10, 10, 20, 20});
        document.insert(onFirst, layer);
        VectorObject onSecond;
        onSecond.path = Shapes::rectangle({210, 10, 20, 20});
        document.insert(onSecond, layer);
        // Still one artboard: every top-level object counts, regardless of position.
        QCOMPARE(document.objectsOn(0).size(), size_t(2));
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 100, 100), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(200, 0, 100, 100), Qt::white}});
        const std::vector<QUuid> onA = document.objectsOn(0);
        const std::vector<QUuid> onB = document.objectsOn(1);
        QCOMPARE(onA.size(), size_t(1));
        QCOMPARE(onA.front(), onFirst.id);
        QCOMPARE(onB.size(), size_t(1));
        QCOMPARE(onB.front(), onSecond.id);
    }

    void artboardDocumentMovesToTheOriginAndDropsWhatDoesNotOverlap()
    {
        VectorDocument document = VectorDocument::blank({300, 300});
        const QUuid layer = document.layers().front();
        VectorObject onFirst;
        onFirst.path = Shapes::rectangle({10, 10, 20, 20});
        document.insert(onFirst, layer);
        VectorObject onSecond;
        onSecond.path = Shapes::rectangle({220, 20, 20, 20});
        document.insert(onSecond, layer);
        document.guides.push_back({Qt::Vertical, 205});
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 100, 100), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(200, 0, 100, 100), QColor(Qt::red)}});
        const VectorDocument second = document.artboardDocument(1);
        QCOMPARE(second.size, QSizeF(100, 100));
        QCOMPARE(second.background, QColor(Qt::red));
        QVERIFY(second.artboards.empty());
        QCOMPARE(second.objects.size(), size_t(2)); // the layer, and onSecond
        QVERIFY(second.find(onSecond.id));
        QVERIFY(!second.find(onFirst.id));
        // Moved so the artboard's corner is the origin.
        QCOMPARE(second.bounds(onSecond.id), QRectF(20, 20, 20, 20));
        QCOMPARE(second.guides.size(), size_t(1));
        QCOMPARE(second.guides.front().position, 5.0);
        // With one artboard, exporting stays byte-identical.
        VectorDocument single = VectorDocument::blank({50, 50});
        single.insert(onFirst, single.layers().front());
        QCOMPARE(single.artboardDocument(0), single);
    }

    void v3FilesMigrateToOneArtboardNamedArtboardOne()
    {
        // A version-3 document has neither "artboards" nor "tokens".
        const QJsonObject json{{"format", "omastrator"}, {"version", 3}, {"width", 400.0}, {"height", 300.0},
                               {"background", "#ffffffff"}, {"objects", QJsonArray()}, {"guides", QJsonArray()}};
        const VectorDocument document = DocumentCodec::decode(json);
        QCOMPARE(document.artboardCount(), 1);
        QCOMPARE(document.allArtboards().front().name, QStringLiteral("Artboard 1"));
        QCOMPARE(document.allArtboards().front().rect, QRectF(0, 0, 400, 300));
    }

    void codecRoundTripsArtboardsAndExportAssets()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        const QUuid asset = document.layers().front();
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("Phone"), QRectF(0, 0, 375, 812), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("Desktop"), QRectF(400, 0, 1440, 900), QColor(Qt::black)}});
        document.exportAssets = {asset};
        const VectorDocument decoded = DocumentCodec::decode(DocumentCodec::encode(document));
        QCOMPARE(decoded.artboards.size(), document.artboards.size());
        QCOMPARE(decoded.allArtboards().at(1).name, QStringLiteral("Desktop"));
        QCOMPARE(decoded.allArtboards().at(1).rect, QRectF(400, 0, 1440, 900));
        QCOMPARE(decoded.exportAssets, document.exportAssets);
    }

    void theExportFlagDefaultsOnRoundTripsAndIsAnAdditiveKey()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("Phone"), QRectF(0, 0, 375, 812), Qt::white},
                               {QUuid::createUuid(), QStringLiteral("Notes"), QRectF(400, 0, 100, 100), Qt::white, QUuid(), false}});
        QVERIFY(document.artboards[0].exported);
        QCOMPARE(document.firstExportedArtboard(), 0);
        const QJsonObject json = DocumentCodec::encode(document);
        // Only the switched-off board writes the key, so a file nobody flagged is unchanged.
        QVERIFY(!json["artboards"].toArray().at(0).toObject().contains("exported"));
        QCOMPARE(json["artboards"].toArray().at(1).toObject()["exported"].toBool(true), false);
        const VectorDocument decoded = DocumentCodec::decode(json);
        QVERIFY(decoded.allArtboards().at(0).exported);
        QVERIFY(!decoded.allArtboards().at(1).exported);
        QCOMPARE(decoded.artboards, document.artboards);
    }

    void firstExportedArtboardSkipsSwitchedOffOnesAndSaysWhenNoneIsLeft()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        QCOMPARE(document.firstExportedArtboard(), 0);
        document.setArtboards({{QUuid::createUuid(), QStringLiteral("A"), QRectF(0, 0, 100, 100), Qt::white, QUuid(), false},
                               {QUuid::createUuid(), QStringLiteral("B"), QRectF(200, 0, 100, 100), Qt::white}});
        QCOMPARE(document.firstExportedArtboard(), 1);
        document.artboards[1].exported = false;
        QCOMPARE(document.firstExportedArtboard(), -1);
    }

    void switchingExportOffIsOneNamedUndoStepAndKeepsTheBoardEditable()
    {
        EditorSession session;
        session.createDocument({400, 300});
        // The lone implicit artboard can be switched off too: it becomes a listed one.
        session.setArtboardExported(0, false);
        QVERIFY(!session.document()->artboard(0).exported);
        QCOMPARE(session.document()->artboards.size(), size_t(1));
        QCOMPARE(session.undoName(), QStringLiteral("Don’t Export Artboard"));
        // Still there to edit.
        session.setArtboardSize({500, 300});
        QCOMPARE(session.document()->artboard(0).rect.size(), QSizeF(500, 300));
        QVERIFY(!session.document()->artboard(0).exported);
        session.undo();
        session.undo();
        QVERIFY(session.document()->artboard(0).exported);
        session.redo();
        session.setArtboardExported(0, true);
        QCOMPARE(session.undoName(), QStringLiteral("Export Artboard"));
        // Setting what's already set is no step.
        const QString name = session.undoName();
        session.setArtboardExported(0, true);
        QCOMPARE(session.undoName(), name);
    }

    void aDuplicateOfAnUnexportedBoardIsUnexportedToo()
    {
        EditorSession session;
        session.createDocument({400, 300});
        session.setArtboardExported(0, false);
        session.duplicateArtboard(0);
        QVERIFY(!session.document()->artboard(1).exported);
    }

    void editorSessionAddsRenamesAndDeletesArtboards()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid first = session.addArtboard(QRectF(500, 0, 400, 300));
        QCOMPARE(session.document()->artboardCount(), 2);
        QCOMPARE(session.activeArtboard(), 1);
        QCOMPARE(session.undoName(), QStringLiteral("New Artboard"));
        session.renameArtboard(1, QStringLiteral("Phone"));
        QCOMPARE(session.document()->artboard(1).name, QStringLiteral("Phone"));
        session.deleteArtboard(0);
        QCOMPARE(session.document()->artboardCount(), 1);
        QCOMPARE(session.document()->artboard(0).id, first);
        // The last one goes too, and an index past the end does nothing.
        session.deleteArtboard(0);
        QCOMPARE(session.document()->artboardCount(), 0);
        session.deleteArtboard(0);
        QCOMPARE(session.document()->artboardCount(), 0);
    }

    void duplicateArtboardCopiesItsArtToTheRight()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid rect = rectangle(session, {10, 10, 50, 50});
        const QUuid copy = session.duplicateArtboard(0);
        const int index = session.document()->artboardIndex(copy);
        QCOMPARE(session.document()->artboard(index).rect.left(), 220.0);
        const std::vector<QUuid> onCopy = session.document()->objectsOn(index);
        QCOMPARE(onCopy.size(), size_t(1));
        QVERIFY(onCopy.front() != rect);
        QCOMPARE(session.document()->bounds(onCopy.front()).left(), 230.0);
    }

    void fitArtboardToArtworkHugsTheArt()
    {
        EditorSession session;
        session.createDocument({400, 400});
        rectangle(session, {40, 60, 100, 30});
        session.fitArtboardToArtwork(0);
        // Strokes included: a new path's default 1 pt stroke pads the bounds by half a point.
        QCOMPARE(session.document()->artboard(0).rect, QRectF(39.5, 59.5, 101, 31));
        QCOMPARE(session.undoName(), QStringLiteral("Fit Artboard to Artwork"));
    }

    void switchArtboardOrientationSwapsWidthAndHeight()
    {
        EditorSession session;
        session.createDocument({400, 300});
        session.switchArtboardOrientation(0);
        QCOMPARE(session.document()->artboard(0).rect.size(), QSizeF(300, 400));
    }

    void showArtboardWrapsAndFitAllCoversEveryOne()
    {
        EditorSession session;
        session.createDocument({100, 100});
        session.addArtboard(QRectF(200, 0, 100, 100));
        QCOMPARE(session.activeArtboard(), 1);
        session.showArtboard(true);
        QCOMPARE(session.activeArtboard(), 0);
        session.showArtboard(false);
        QCOMPARE(session.activeArtboard(), 1);
        session.fitAllArtboards();
        QCOMPARE(session.viewport.zoom() > 0, true);
    }

    void collectAndRemoveFromExportAreUndoable()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid rect = rectangle(session, {10, 10, 20, 20});
        session.collectForExport({rect});
        QCOMPARE(session.document()->exportAssets, std::vector<QUuid>{rect});
        QCOMPARE(session.undoName(), QStringLiteral("Collect for Export"));
        session.removeFromExport({rect});
        QVERIFY(session.document()->exportAssets.empty());
    }

    void resizingArtboardOneKeepsTheViewportOriginStill()
    {
        EditorSession session;
        session.createDocument({200, 200});
        session.resizeView({800, 600}, 1);
        const QSizeF pan = session.viewport.pan;
        session.setArtboardSize({400, 200});
        // The pan compensates so the document's own origin stays put on screen.
        QVERIFY(session.viewport.pan != pan);
    }
};

QTEST_MAIN(ArtboardsTests)
#include "ArtboardsTests.moc"
