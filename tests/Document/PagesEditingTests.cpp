#include "Document/DocumentHistory.h"
#include "Document/EditorSession.h"
#include <QSignalSpy>
#include <QTest>

// Pages (docs/PAGES.md): what an edit, a switch or a selection may do when a proposal, the lock or another page is in the way.
class PagesEditingTests : public QObject {
    Q_OBJECT

private:
    static VectorObject rectangleObject(QRectF rect)
    {
        VectorObject object;
        object.kind = ObjectKind::path;
        object.name = QStringLiteral("Rectangle");
        object.path = Shapes::rectangle(rect);
        object.fill = Paint::solid(Qt::red);
        return object;
    }

    // Two pages: "Page 1" with a red square at 10,10 and "Page 2" with a blue one at 50,50 (its layer "Layer 2").
    struct Fixture {
        EditorSession session;
        QUuid first, second, layerOne, layerTwo, red, blue;

        Fixture()
        {
            VectorDocument document = VectorDocument::blank({100, 100});
            layerOne = document.layers().front();
            VectorObject square = rectangleObject({10, 10, 30, 30});
            red = square.id;
            document.insert(square, layerOne);
            document.ensurePages();
            first = document.pages.front().id;
            second = QUuid::createUuid();
            document.pages.push_back({second, QStringLiteral("Page 2")});
            VectorObject layer;
            layer.kind = ObjectKind::layer;
            layer.name = QStringLiteral("Layer 2");
            layer.page = second;
            layerTwo = layer.id;
            document.objects.push_back(layer);
            VectorObject other = rectangleObject({50, 50, 30, 30});
            blue = other.id;
            document.insert(other, layerTwo);
            document.ensurePages();
            session.loadDocument(document);
        }
    };

private slots:
    void aProposalKeepsPageSwitchesPending()
    {
        Fixture f;
        f.session.beginInteraction(EditorSession::proposalPrefix() + QStringLiteral("Test"));
        QVERIFY(f.session.isProposalOpen());
        f.session.setCurrentPage(f.second);
        QCOMPARE(f.session.currentPage(), f.first);
        f.session.showPage(true);
        f.session.showPage(false);
        QCOMPARE(f.session.currentPage(), f.first);
        QVERIFY(f.session.isProposalOpen());
        QVERIFY(f.session.addPage().isNull());
        QVERIFY(f.session.duplicatePage(f.first).isNull());
        QVERIFY(!f.session.deletePage(f.second));
        QCOMPARE(f.session.document()->pageCount(), 2);
        f.session.cancelInteraction();
        QVERIFY(!f.session.isProposalOpen());
        f.session.setCurrentPage(f.second);
        QCOMPARE(f.session.currentPage(), f.second);
    }

    void aProposalKeepsPageOperationsToItself()
    {
        Fixture f;
        f.session.select({f.red});
        f.session.beginInteraction(EditorSession::proposalPrefix() + QStringLiteral("Test"));
        QSignalSpy moved(&f.session, &EditorSession::movedToPage);
        f.session.renamePage(f.first, QStringLiteral("Renamed"));
        f.session.movePage(f.second, 0);
        f.session.moveSelectionToPage(f.second);
        f.session.moveLayersToPage({f.layerOne}, f.second);
        QCOMPARE(f.session.document()->allPages().front().name, QStringLiteral("Page 1"));
        QCOMPARE(f.session.document()->pageIndex(f.second), 1);
        QCOMPARE(f.session.document()->pageOf(f.red), f.first);
        QCOMPARE(moved.count(), 0);
        f.session.cancelInteraction();
    }

    void selectGoesToTheOnePageItsIdsAreOn()
    {
        Fixture f;
        QCOMPARE(f.session.currentPage(), f.first);
        f.session.select({f.blue});
        QCOMPARE(f.session.currentPage(), f.second);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.blue});
        // Mixed pages: only the current page's ids stay, and the view stays put.
        f.session.select({f.blue, f.red});
        QCOMPARE(f.session.currentPage(), f.second);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.blue});
    }

    void selectDropsOtherPagesIdsWhenTheSwitchIsRefused()
    {
        Fixture f;
        f.session.beginInteraction(EditorSession::proposalPrefix() + QStringLiteral("Test"));
        f.session.select({f.blue});
        QCOMPARE(f.session.currentPage(), f.first);
        QVERIFY(f.session.selection().empty());
        f.session.cancelInteraction();
    }

    void selectingMainComponentsAcrossPagesLandsOnTheirPage()
    {
        Fixture f;
        f.session.select({f.red});
        f.session.select({f.blue});
        QCOMPARE(f.session.currentPage(), f.second);
        QVERIFY(f.session.isSelected(f.blue));
        QVERIFY(!f.session.isSelected(f.red));
    }

    void aboutToChangePageComesBeforeEveryPageChange()
    {
        Fixture f;
        QSignalSpy about(&f.session, &EditorSession::aboutToChangePage);
        f.session.setCurrentPage(f.second);
        QCOMPARE(about.count(), 1);
        f.session.setCurrentPage(f.second);
        QCOMPARE(about.count(), 1);
        const QUuid added = f.session.addPage();
        QCOMPARE(about.count(), 2);
        f.session.duplicatePage(added);
        QCOMPARE(about.count(), 3);
        f.session.deletePage(f.session.currentPage());
        QCOMPARE(about.count(), 4);
        // Deleting a page that isn't shown leaves the view alone.
        f.session.deletePage(f.second);
        QCOMPARE(about.count(), 4);
    }

    void aHistoryStepDoesNotAbsorbAnEditFromAnotherPage()
    {
        VectorDocument one = VectorDocument::blank({100, 100});
        one.ensurePages();
        VectorDocument other = one;
        other.pages.push_back({QUuid::createUuid(), QStringLiteral("Page 2")});
        other.currentPage = other.pages.back().id;
        DocumentHistory history;
        VectorDocument edited = one;
        edited.size = QSizeF(120, 100);
        history.begin(QStringLiteral("Type"), one, {});
        history.end(edited, {});
        QVERIFY(history.canUndo());
        VectorDocument again = edited;
        again.size = QSizeF(130, 100);
        QVERIFY(history.amend(QStringLiteral("Type"), again, {}));
        other.size = QSizeF(140, 100);
        QVERIFY(!history.amend(QStringLiteral("Type"), other, {}));
    }

    void duplicatePageCopiesTheArtboardsThePageShows()
    {
        EditorSession session;
        session.createDocument({200, 100});
        const QUuid copy = session.duplicatePage(session.currentPage());
        const VectorDocument &document = *session.document();
        QCOMPARE(document.pageCount(), 2);
        QCOMPARE(document.artboardsOn(copy).size(), size_t(1));
        QCOMPARE(document.artboardsOn(copy).front().rect.size(), QSizeF(200, 100));
        QCOMPARE(document.artboardsOn(document.pages.front().id).front().rect.size(), QSizeF(200, 100));
    }

    void moveSelectionToPageSkipsALockedLayerWithTheSameName()
    {
        Fixture f;
        f.session.rename(f.layerTwo, QStringLiteral("Layer 1"));
        // The target page's same-named layer is locked, and an open one sits above it.
        f.session.setLocked(f.layerTwo, true);
        f.session.select({f.red});
        f.session.moveSelectionToPage(f.second);
        const std::optional<QUuid> landed = f.session.document()->layerOf(f.red);
        QVERIFY(landed);
        QVERIFY(*landed != f.layerTwo);
        QVERIFY(!f.session.document()->find(*landed)->isLocked);
        QCOMPARE(f.session.document()->pageOf(f.red), f.second);
    }

    void moveToPageSaysNothingWhenNothingMoved()
    {
        Fixture f;
        f.session.select({f.red});
        QSignalSpy moved(&f.session, &EditorSession::movedToPage);
        f.session.setDocumentLocked(true);
        f.session.moveSelectionToPage(f.second);
        QCOMPARE(moved.count(), 0);
        QCOMPARE(f.session.document()->pageOf(f.red), f.first);
        f.session.setDocumentLocked(false);
        f.session.moveSelectionToPage(f.second);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.first().first().toString(), QStringLiteral("Page 2"));
    }

    void aLockedDocumentTurnsPageOperationsAwayHonestly()
    {
        Fixture f;
        f.session.setDocumentLocked(true);
        QSignalSpy refused(&f.session, &EditorSession::editRefused);
        const int steps = f.session.undoNames().size();
        QVERIFY(f.session.addPage().isNull());
        QVERIFY(f.session.duplicatePage(f.first).isNull());
        f.session.renamePage(f.first, QStringLiteral("Renamed"));
        f.session.movePage(f.second, 0);
        QVERIFY(!f.session.deletePage(f.second));
        f.session.moveLayersToPage({f.layerOne}, f.second);
        QCOMPARE(refused.count(), 6);
        QCOMPARE(int(f.session.undoNames().size()), steps);
        QCOMPARE(f.session.document()->pageCount(), 2);
        // Looking at another page is no edit.
        f.session.setCurrentPage(f.second);
        QCOMPARE(f.session.currentPage(), f.second);
    }

    void moveLayersToPageIsOneStepAndLeavesNoPageEmpty()
    {
        Fixture f;
        QSignalSpy moved(&f.session, &EditorSession::movedToPage);
        const size_t steps = f.session.undoNames().size();
        f.session.moveLayersToPage({f.layerOne}, f.second);
        QCOMPARE(f.session.undoNames().size(), steps + 1);
        QCOMPARE(f.session.undoName(), QStringLiteral("Move to Page"));
        QCOMPARE(moved.count(), 1);
        const VectorDocument &document = *f.session.document();
        QCOMPARE(document.pageOf(f.red), f.second);
        QCOMPARE(document.layersOn(f.second).size(), size_t(2));
        // The first page lost its only layer and got a fresh one.
        QCOMPARE(document.layersOn(f.first).size(), size_t(1));
        QVERIFY(document.layersOn(f.first).front() != f.layerOne);
        QVERIFY(f.session.selection().empty());
        f.session.undo();
        QCOMPARE(f.session.document()->pageOf(f.red), f.first);
        QCOMPARE(f.session.document()->layersOn(f.first), std::vector<QUuid>{f.layerOne});
        // A layer already there, an object and an unknown id do nothing.
        moved.clear();
        f.session.moveLayersToPage({f.layerTwo, f.blue, QUuid::createUuid()}, f.second);
        QCOMPARE(moved.count(), 0);
    }

    void aPageWithNoLayerGetsOneWhenTheDocumentIsRepaired()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.ensurePages();
        const QUuid page = QUuid::createUuid();
        document.pages.push_back({page, QStringLiteral("Empty")});
        document.ensurePages();
        QCOMPARE(document.layersOn(page).size(), size_t(1));
        QCOMPARE(document.artboardsOn(page).size(), size_t(1));
    }

    void repairPageNamesFillsBlanksAndNumbersRepeats()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.pages = {{QUuid::createUuid(), QStringLiteral("A")}, {QUuid::createUuid(), QStringLiteral(" ")},
                          {QUuid::createUuid(), QStringLiteral("A")}, {QUuid::createUuid(), QStringLiteral("A")}};
        document.repairPageNames();
        QCOMPARE(document.pages[0].name, QStringLiteral("A"));
        QCOMPARE(document.pages[1].name, QStringLiteral("Page 2"));
        QCOMPARE(document.pages[2].name, QStringLiteral("A 2"));
        QCOMPARE(document.pages[3].name, QStringLiteral("A 3"));
    }

    void zoomAndPlaceUseTheActiveArtboardOfThePage()
    {
        Fixture f;
        f.session.setCurrentPage(f.second);
        f.session.addArtboard(QRectF(500, 0, 300, 200));
        f.session.setActiveArtboard(1);
        QImage image(20, 10, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        const QUuid placed = f.session.placeImage(image, QStringLiteral("Dot"));
        QCOMPARE(f.session.document()->bounds(placed).center(), QPointF(650, 100));
        QCOMPARE(f.session.document()->viewSize(), QSizeF(100, 100));
    }
};

QTEST_MAIN(PagesEditingTests)
#include "PagesEditingTests.moc"
