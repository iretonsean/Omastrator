#include "Document/EditorSession.h"
#include "Rendering/VectorRenderer.h"
#include <QTest>

// Pages (docs/PAGES.md): the model, then the session's operations.
class PagesTests : public QObject {
    Q_OBJECT

private:
    static VectorObject rectangleObject(QRectF rect, QColor fill)
    {
        VectorObject object;
        object.kind = ObjectKind::path;
        object.name = QStringLiteral("Rectangle");
        object.path = Shapes::rectangle(rect);
        object.fill = Paint::solid(fill);
        return object;
    }

    // Two pages: "Page 1" with a red square at 10,10 and "Page 2" with a blue one at 50,50 (its layer "Layer 2").
    struct Fixture {
        VectorDocument document;
        QUuid first, second, layerOne, layerTwo, red, blue;
    };
    static Fixture twoPages()
    {
        Fixture f;
        f.document = VectorDocument::blank({100, 100});
        f.layerOne = f.document.layers().front();
        f.red = rectangleObject({10, 10, 30, 30}, Qt::red).id;
        f.document.insert(rectangleObject({10, 10, 30, 30}, Qt::red), f.layerOne);
        f.document.objects.back().id = f.red;
        f.document.ensurePages();
        f.first = f.document.pages.front().id;
        f.second = QUuid::createUuid();
        f.document.pages.push_back({f.second, QStringLiteral("Page 2")});
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        layer.name = QStringLiteral("Layer 2");
        layer.page = f.second;
        f.layerTwo = layer.id;
        f.document.objects.push_back(layer);
        VectorObject square = rectangleObject({50, 50, 30, 30}, Qt::blue);
        f.blue = square.id;
        f.document.insert(square, f.layerTwo);
        f.document.ensurePages();
        return f;
    }

private slots:
    void noPagesIsOneImplicitPageWithAFixedId()
    {
        const VectorDocument document = VectorDocument::blank({100, 100});
        QCOMPARE(document.pageCount(), 1);
        const std::vector<Page> pages = document.allPages();
        QCOMPARE(pages.size(), size_t(1));
        QCOMPARE(pages.front().name, QStringLiteral("Page 1"));
        QCOMPARE(pages.front().id, VectorDocument::implicitPageId());
        QCOMPARE(document.currentPageId(), VectorDocument::implicitPageId());
        QCOMPARE(document.pageOf(document.layers().front()), VectorDocument::implicitPageId());
        QVERIFY(document.isOnCurrentPage(document.layers().front()));
        QVERIFY(document.pages.empty());
    }

    void ensurePagesStampsEveryLayerArtboardAndGuide()
    {
        VectorDocument document = VectorDocument::blank({100, 100});
        document.guides.push_back({Qt::Vertical, 10});
        document.ensurePages();
        QCOMPARE(document.pages.size(), size_t(1));
        const QUuid page = document.pages.front().id;
        QCOMPARE(document.pages.front().id, VectorDocument::implicitPageId());
        QCOMPARE(document.find(document.layers().front())->page, page);
        QCOMPARE(document.guides.front().page, page);
        QCOMPARE(document.artboards.size(), size_t(1));
        QCOMPARE(document.artboards.front().page, page);
        QCOMPARE(document.currentPage, page);
        // Idempotent, and the implicit artboard keeps its id.
        VectorDocument again = document;
        again.ensurePages();
        QCOMPARE(again, document);
        QCOMPARE(document.artboards.front().id, VectorDocument::blank({100, 100}).allArtboards().front().id);
    }

    void layersAndArtboardsAreTheCurrentPagesWhileAllLayersSpansEveryPage()
    {
        Fixture f = twoPages();
        QCOMPARE(f.document.currentPageId(), f.first);
        QCOMPARE(f.document.layers(), std::vector<QUuid>{f.layerOne});
        QCOMPARE(f.document.allLayers().size(), size_t(2));
        QCOMPARE(f.document.artboardCount(), 1);
        QCOMPARE(f.document.artboardsOn(f.second).size(), size_t(1));
        QCOMPARE(f.document.artboards.size(), size_t(2));
        f.document.currentPage = f.second;
        QCOMPARE(f.document.layers(), std::vector<QUuid>{f.layerTwo});
        QCOMPARE(f.document.allArtboards().front().page, f.second);
        // find() spans every page.
        QVERIFY(f.document.find(f.red));
        QVERIFY(!f.document.isOnCurrentPage(f.red));
        QVERIFY(f.document.isOnCurrentPage(f.blue));
        QCOMPARE(f.document.pageOf(f.red), f.first);
        QCOMPARE(f.document.pageIndex(f.second), 1);
    }

    void aPageThatNamesNoPageIsOnTheFirst()
    {
        Fixture f = twoPages();
        f.document.find(f.layerTwo)->page = QUuid::createUuid();
        QCOMPARE(f.document.pageOf(f.blue), f.first);
        QCOMPARE(f.document.layers().size(), size_t(2));
        f.document.currentPage = QUuid::createUuid();
        QCOMPARE(f.document.currentPageId(), f.first);
    }

    void hitTestMatchingAndSelectAllStayOnTheCurrentPage()
    {
        Fixture f = twoPages();
        // Both squares are hit at their own spots, but only the current page's answers.
        QCOMPARE(f.document.hitTest({20, 20}, 1), std::optional(f.red));
        QVERIFY(!f.document.hitTest({60, 60}, 1));
        QCOMPARE(f.document.hitTestAll({60, 60}, 1).size(), size_t(0));
        f.document.currentPage = f.second;
        QVERIFY(!f.document.hitTest({20, 20}, 1));
        QCOMPARE(f.document.hitTest({60, 60}, 1), std::optional(f.blue));
        // Select ▸ Same finds nothing across the page boundary.
        f.document.find(f.blue)->fill = Paint::solid(Qt::red);
        QCOMPARE(f.document.matching(f.blue, SameAttribute::fillColor), std::vector<QUuid>{f.blue});
        QCOMPARE(f.document.matching(ObjectFilter::openPaths).size(), size_t(0));

        EditorSession session;
        session.loadDocument(f.document);
        session.selectAll();
        QCOMPARE(session.selection(), std::vector<QUuid>{f.blue});
        QCOMPARE(session.objectsIn(QRectF(0, 0, 100, 100), false), std::vector<QUuid>{f.blue});
    }

    void aTopLevelLayerInsertedWithNoPageTakesTheCurrentOne()
    {
        Fixture f = twoPages();
        f.document.currentPage = f.second;
        VectorObject layer;
        layer.kind = ObjectKind::layer;
        f.document.insert(layer, QUuid());
        QCOMPARE(f.document.find(layer.id)->page, f.second);
        VectorObject appended;
        appended.kind = ObjectKind::layer;
        f.document.appendLayer(appended);
        QCOMPARE(f.document.find(appended.id)->page, f.second);
        // Without pages the tag stays null.
        VectorDocument plain = VectorDocument::blank({10, 10});
        VectorObject other;
        other.kind = ObjectKind::layer;
        plain.appendLayer(other);
        QVERIFY(plain.find(other.id)->page.isNull());
    }

    void movingALayerCountsOnlyItsPagesLayers()
    {
        Fixture f = twoPages();
        VectorObject extra;
        extra.kind = ObjectKind::layer;
        extra.page = f.first;
        f.document.objects.push_back(extra);
        // Order in objects: layerOne, layerTwo, extra. On page 1 that's [layerOne, extra].
        QCOMPARE(f.document.layers(), (std::vector<QUuid>{f.layerOne, extra.id}));
        QVERIFY(f.document.moveLayer(extra.id, 0));
        QCOMPARE(f.document.layers(), (std::vector<QUuid>{extra.id, f.layerOne}));
        QVERIFY(f.document.moveLayer(extra.id, 5));
        QCOMPARE(f.document.layers(), (std::vector<QUuid>{f.layerOne, extra.id}));
        QCOMPARE(f.document.layersOn(f.second), std::vector<QUuid>{f.layerTwo});
    }

    void artboardHelpersAndSetArtboardsWorkOnTheCurrentPage()
    {
        Fixture f = twoPages();
        f.document.currentPage = f.second;
        std::vector<Artboard> boards = f.document.allArtboards();
        boards.front().rect = QRectF(0, 0, 200, 150);
        boards.push_back({QUuid::createUuid(), QStringLiteral("Extra"), QRectF(300, 0, 50, 50), Qt::white});
        f.document.setArtboards(boards);
        QCOMPARE(f.document.artboardCount(), 2);
        QCOMPARE(f.document.artboard(1).page, f.second);
        // The other page is untouched, and `size` still follows the raw first artboard (page 1's).
        f.document.currentPage = f.first;
        QCOMPARE(f.document.artboardCount(), 1);
        QCOMPARE(f.document.size, QSizeF(100, 100));
        QCOMPARE(f.document.artboards.size(), size_t(3));
        QCOMPARE(f.document.artboardAt({20, 20}), 0);
        QCOMPARE(f.document.artboardBounds(), QRectF(0, 0, 100, 100));
        f.document.currentPage = f.second;
        QCOMPARE(f.document.artboardBounds(), QRectF(0, 0, 350, 150));
        // objectsOn: the second page's own art, two artboards, only what meets each.
        QCOMPARE(f.document.objectsOn(0), std::vector<QUuid>{f.blue});
        QCOMPARE(f.document.objectsOn(1).size(), size_t(0));
    }

    void artboardDocumentAndCroppedToGiveSinglePageDocuments()
    {
        Fixture f = twoPages();
        f.document.guides.push_back({Qt::Vertical, 5, f.first});
        f.document.guides.push_back({Qt::Vertical, 6, f.second});
        f.document.currentPage = f.second;
        const VectorDocument board = f.document.artboardDocument(0);
        QVERIFY(board.pages.empty());
        QVERIFY(board.currentPage.isNull());
        QVERIFY(board.artboards.empty());
        QVERIFY(!board.find(f.red));
        QVERIFY(board.find(f.blue));
        QCOMPARE(board.allLayers().size(), size_t(1));
        QVERIFY(board.find(f.layerTwo)->page.isNull());
        QCOMPARE(board.guides.size(), size_t(1));
        QCOMPARE(board.guides.front().position, 6.0);
        QVERIFY(board.guides.front().page.isNull());

        const VectorDocument cropped = f.document.croppedTo({f.blue});
        QVERIFY(cropped.pages.empty());
        QVERIFY(!cropped.find(f.red));
        QVERIFY(cropped.size.width() >= 30 && cropped.size.width() < 40); // the stroke adds a margin
        QVERIFY(cropped.find(f.layerTwo)->page.isNull());
        QCOMPARE(cropped.guides.size(), size_t(1));
    }

    void sizeFollowsTheFirstPagesFirstArtboard()
    {
        Fixture f = twoPages();
        f.document.setArtboards({{f.document.allArtboards().front().id, QStringLiteral("A"), QRectF(0, 0, 640, 480), Qt::white}});
        QCOMPARE(f.document.size, QSizeF(640, 480));
        f.document.currentPage = f.second;
        std::vector<Artboard> boards = f.document.allArtboards();
        boards.front().rect = QRectF(0, 0, 10, 10);
        f.document.setArtboards(boards);
        QCOMPARE(f.document.size, QSizeF(640, 480));
        QCOMPARE(f.document.artboards.front().rect.size(), QSizeF(640, 480));
    }

    void theRendererDrawsOnlyTheCurrentPage()
    {
        Fixture f = twoPages();
        QImage one = VectorRenderer::render(f.document, 1, false);
        QCOMPARE(one.size(), QSize(100, 100));
        // Page 1's red square, but not page 2's blue one.
        QCOMPARE(QColor(one.pixel(20, 20)), QColor(Qt::red));
        QCOMPARE(QColor(one.pixel(60, 60)), QColor(Qt::white));
        f.document.currentPage = f.second;
        QImage two = VectorRenderer::render(f.document, 1, false);
        QCOMPARE(QColor(two.pixel(20, 20)), QColor(Qt::white));
        QCOMPARE(QColor(two.pixel(60, 60)), QColor(Qt::blue));
    }

    void rulerGuidesBelongToTheirPage()
    {
        Fixture f = twoPages();
        f.document.guides.push_back({Qt::Vertical, 5, f.first});
        f.document.guides.push_back({Qt::Horizontal, 6, f.second});
        f.document.currentPage = f.first;
        EditorSession session;
        session.loadDocument(f.document);
        QCOMPARE(session.document()->guidesOnCurrentPage().size(), size_t(1));
        session.addGuide({Qt::Vertical, 30});
        QCOMPARE(session.document()->guides.back().page, f.first);
        QCOMPARE(session.document()->guidesOnCurrentPage().size(), size_t(2));
        session.clearGuides();
        QCOMPARE(session.document()->guides.size(), size_t(1));
        QCOMPARE(session.document()->guides.front().page, f.second);
        QVERIFY(session.document()->guidesOnCurrentPage().empty());
        // Nothing left on this page to clear: no undo step.
        session.clearGuides();
        session.undo();
        QCOMPARE(session.document()->guidesOnCurrentPage().size(), size_t(2));
    }

    void releasingGuidesLeavesTheOtherPagesAlone()
    {
        Fixture f = twoPages();
        f.document.guides.push_back({Qt::Vertical, 5, f.first});
        f.document.guides.push_back({Qt::Horizontal, 6, f.second});
        EditorSession session;
        session.loadDocument(f.document);
        session.releaseGuides();
        QCOMPARE(session.document()->guides.size(), size_t(1));
        QCOMPARE(session.document()->guides.front().page, f.second);
    }

    void aPageAlwaysKeepsOneLayerWhenItsArtIsDeleted()
    {
        Fixture f = twoPages();
        f.document.currentPage = f.second;
        EditorSession session;
        session.loadDocument(f.document);
        session.deleteObjects({f.layerTwo});
        // The page got a fresh layer, on itself; page 1 is untouched.
        QCOMPARE(session.document()->layers().size(), size_t(1));
        QCOMPARE(session.document()->layersOn(f.first), std::vector<QUuid>{f.layerOne});
        QVERIFY(session.document()->find(f.red));
        QVERIFY(!session.document()->find(f.blue));
    }

    void newLayersAndComponentLayersLandOnTheCurrentPage()
    {
        Fixture f = twoPages();
        f.document.currentPage = f.second;
        EditorSession session;
        session.loadDocument(f.document);
        const QUuid layer = session.addLayer();
        QCOMPARE(session.document()->pageOf(layer), f.second);
        QCOMPARE(session.document()->layers().size(), size_t(2));
        QCOMPARE(session.document()->layersOn(f.first).size(), size_t(1));
        QCOMPARE(session.activeLayer(), std::optional(layer));
    }

    void unlockAllAndShowAllStayOnTheCurrentPage()
    {
        Fixture f = twoPages();
        f.document.find(f.red)->isLocked = true;
        f.document.find(f.blue)->isLocked = true;
        f.document.find(f.red)->isVisible = false;
        f.document.find(f.blue)->isVisible = false;
        f.document.currentPage = f.second;
        EditorSession session;
        session.loadDocument(f.document);
        session.unlockAll();
        QVERIFY(!session.document()->find(f.blue)->isLocked);
        QVERIFY(session.document()->find(f.red)->isLocked);
        session.showAll();
        QVERIFY(session.document()->find(f.blue)->isVisible);
        QVERIFY(!session.document()->find(f.red)->isVisible);
    }

    void pastingLandsOnTheCurrentPage()
    {
        Fixture f = twoPages();
        f.document.currentPage = f.second;
        EditorSession source;
        source.loadDocument(f.document);
        source.select({f.blue});
        source.copy();
        f.document.currentPage = f.first;
        EditorSession target;
        target.loadDocument(f.document);
        target.paste(true);
        QCOMPARE(target.selection().size(), size_t(1));
        QCOMPARE(target.document()->pageOf(target.selection().front()), f.first);
        QCOMPARE(target.document()->layersOn(f.second), std::vector<QUuid>{f.layerTwo});
        QCOMPARE(target.document()->children(f.layerTwo).size(), size_t(1));
    }

    void theViewportSizeIsTheCurrentPagesFirstArtboard()
    {
        Fixture f = twoPages();
        QCOMPARE(f.document.viewSize(), QSizeF(100, 100));
        f.document.currentPage = f.second;
        QCOMPARE(f.document.viewSize(), QSizeF(100, 100));
        std::vector<Artboard> boards = f.document.allArtboards();
        boards.front().rect = QRectF(0, 0, 640, 480);
        f.document.setArtboards(boards);
        QCOMPARE(f.document.viewSize(), QSizeF(640, 480));
        f.document.currentPage = f.first;
        QCOMPARE(f.document.viewSize(), QSizeF(100, 100));
    }

    void uniquePageNamesNumberFromTwo()
    {
        Fixture f = twoPages();
        QCOMPARE(f.document.uniquePageName(QStringLiteral("Home")), QStringLiteral("Home"));
        QCOMPARE(f.document.uniquePageName(QStringLiteral("Page 2")), QStringLiteral("Page 2 2"));
        QCOMPARE(f.document.uniquePageName(QStringLiteral("Page 1")), QStringLiteral("Page 1 2"));
    }
};

QTEST_MAIN(PagesTests)
#include "PagesTests.moc"
