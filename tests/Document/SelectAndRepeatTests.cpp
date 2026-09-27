#include "Document/EditorSession.h"
#include "Document/PathOperations.h"
#include <QStandardPaths>
#include <QTest>

// The Select menu, Paste in Front and Back, Transform Again, the Layers panel's
// Others commands and Zoom to Selection: each change one named undo step.
namespace {
struct Fixture {
    EditorSession session;
    QUuid layer, a, b, c;

    // a, b, c bottom-up in one layer, spread left to right.
    Fixture()
    {
        session.createDocument({400, 300});
        layer = session.activeLayer().value();
        a = box(10);
        b = box(60);
        c = box(110);
        session.deselectAll();
    }

    QUuid box(double x, QString name = QStringLiteral("Box"))
    {
        return session.addPath(Shapes::rectangle(QRectF(x, 10, 40, 40)), name);
    }
    std::vector<QUuid> children(const QUuid &parent) const { return session.document()->children(parent); }
    QRectF bounds(const QUuid &id) const { return session.document()->bounds(id); }
    VectorObject object(const QUuid &id) const { return *session.document()->find(id); }
};

bool near(QRectF a, QRectF b)
{
    return std::abs(a.left() - b.left()) < 1e-6 && std::abs(a.top() - b.top()) < 1e-6 && std::abs(a.width() - b.width()) < 1e-6
        && std::abs(a.height() - b.height()) < 1e-6;
}
}

class SelectAndRepeatTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void hitTestAllListsTheStackTopmostFirst()
    {
        Fixture f;
        const QUuid over = f.session.addPath(Shapes::rectangle(QRectF(20, 20, 60, 20)), QStringLiteral("Over"));
        QCOMPARE(f.session.document()->hitTestAll(QPointF(30, 30), 1), (std::vector<QUuid>{over, f.a}));
        QCOMPARE(f.session.document()->hitTestAll(QPointF(30, 30), 1, 1), std::vector<QUuid>{over});
        QCOMPARE(f.session.document()->hitTest(QPointF(30, 30), 1), std::optional(over));
        // Hidden and locked objects are out of reach.
        f.session.setLocked(over, true);
        QCOMPARE(f.session.document()->hitTestAll(QPointF(30, 30), 1), std::vector<QUuid>{f.a});
        QVERIFY(f.session.document()->hitTestAll(QPointF(300, 200), 1).empty());
    }

    void inverseSelectsEverythingElse()
    {
        Fixture f;
        f.session.select({f.b});
        f.session.selectInverse();
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{f.a, f.c}));
        // Selection isn't a document change.
        QCOMPARE(f.session.undoName(), QStringLiteral("Draw Box"));
    }

    void nextObjectAboveAndBelowWalkTheStack()
    {
        Fixture f;
        f.session.select({f.b});
        f.session.selectAdjacent(true);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.c});
        f.session.selectAdjacent(true);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.c});
        f.session.selectAdjacent(false);
        f.session.selectAdjacent(false);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.a});
        // Hidden objects are stepped over.
        f.session.setVisible(f.b, false);
        f.session.select({f.a});
        f.session.selectAdjacent(true);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.c});
    }

    void sameFillFindsMatchesAndReselectRepeats()
    {
        Fixture f;
        f.session.select({f.a, f.c});
        f.session.setFillOfSelection(Paint::solid(Qt::red));
        f.session.select({f.a});
        f.session.selectSame(SameAttribute::fillColor);
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{f.a, f.c}));
        QVERIFY(f.session.canReselect());
        // Reselect runs the same command on what's there now.
        f.session.select({f.b});
        f.session.reselect();
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.b});
        f.session.select({f.b});
        f.session.selectSame(SameAttribute::opacity);
        QCOMPARE(f.session.selection().size(), size_t(3));
        f.session.select({f.a});
        f.session.setOpacityOfSelection(0.5);
        f.session.selectSame(SameAttribute::opacity);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.a});
    }

    void sameFontOnlyMatchesText()
    {
        Fixture f;
        const QUuid one = f.session.addText({10, 100}, QStringLiteral("One"));
        const QUuid two = f.session.addText({10, 150}, QStringLiteral("Two"));
        f.session.select({one});
        f.session.selectSame(SameAttribute::fontFamily);
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{one, two}));
        f.session.select({f.a});
        f.session.selectSame(SameAttribute::fontFamily);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.a});
    }

    void objectFiltersFindKinds()
    {
        Fixture f;
        const QUuid text = f.session.addText({10, 100}, QStringLiteral("Words"));
        VectorPath open;
        open.contours.push_back(Contour{{PathNode(QPointF(0, 0)), PathNode(QPointF(50, 50))}, false});
        const QUuid line = f.session.addPath(open, QStringLiteral("Line"));
        VectorPath stray;
        stray.contours.push_back(Contour{{PathNode(QPointF(5, 5))}, false});
        const QUuid point = f.session.addPath(stray, QStringLiteral("Point"));
        f.session.selectObjects(ObjectFilter::textObjects);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{text});
        f.session.selectObjects(ObjectFilter::openPaths);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{line});
        f.session.selectObjects(ObjectFilter::strayPoints);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{point});
        f.session.select({f.a, f.b});
        f.session.makeClippingMask();
        f.session.selectObjects(ObjectFilter::clippingMasks);
        QCOMPARE(f.session.selection(), std::vector<QUuid>{f.b});
    }

    void allOnSameLayersStaysInTheLayer()
    {
        Fixture f;
        const QUuid other = f.session.addLayer();
        f.session.setActiveLayer(other);
        f.box(200);
        f.session.select({f.a});
        f.session.selectAllOnSameLayers();
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{f.a, f.b, f.c}));
    }

    void pasteInFrontAndBackStackAgainstTheSelection()
    {
        Fixture f;
        f.session.select({f.c});
        f.session.copy();
        f.session.select({f.a});
        f.session.paste(PastePosition::front);
        QCOMPARE(f.session.undoName(), QStringLiteral("Paste in Front"));
        const QUuid front = f.session.selection().front();
        // Right above a, where c was copied from.
        QCOMPARE(f.children(f.layer), (std::vector<QUuid>{f.a, front, f.b, f.c}));
        QCOMPARE(f.bounds(front), f.bounds(f.c));
        f.session.select({f.b});
        f.session.paste(PastePosition::back);
        QCOMPARE(f.session.undoName(), QStringLiteral("Paste in Back"));
        const QUuid back = f.session.selection().front();
        QCOMPARE(f.children(f.layer), (std::vector<QUuid>{f.a, front, back, f.b, f.c}));
        // With nothing selected, back is the bottom of the layer and front the top.
        f.session.deselectAll();
        f.session.paste(PastePosition::back);
        QCOMPARE(f.children(f.layer).front(), f.session.selection().front());
        f.session.deselectAll();
        f.session.paste(PastePosition::front);
        QCOMPARE(f.children(f.layer).back(), f.session.selection().front());
        f.session.undo();
        QCOMPARE(f.children(f.layer).size(), size_t(6));
    }

    void transformAgainRepeatsAMove()
    {
        Fixture f;
        QVERIFY(!f.session.canTransformAgain());
        f.session.select({f.a});
        f.session.moveSelection({5, 7});
        QVERIFY(f.session.canTransformAgain());
        f.session.transformAgain();
        QCOMPARE(f.session.undoName(), QStringLiteral("Transform Again"));
        QCOMPARE(f.bounds(f.a), QRectF(20, 24, 40, 40));
        // It applies to whatever is selected now.
        f.session.select({f.c});
        f.session.transformAgain();
        QCOMPARE(f.bounds(f.c), QRectF(115, 17, 40, 40));
    }

    void transformAgainPivotsOnTheNewSelection()
    {
        Fixture f;
        f.session.select({f.a});
        f.session.scaleSelection(0.5, 0.5);
        QVERIFY(near(f.bounds(f.a), QRectF(20, 20, 20, 20)));
        f.session.select({f.c});
        f.session.transformAgain();
        // Scaled about c's own centre, not a's.
        QVERIFY(near(f.bounds(f.c), QRectF(120, 20, 20, 20)));
        f.session.select({f.b});
        f.session.rotateSelection(90);
        f.session.transformAgain();
        QVERIFY(near(f.bounds(f.b), QRectF(60, 10, 40, 40)));
    }

    void stepAndRepeatAfterADuplicate()
    {
        Fixture f;
        f.session.select({f.a});
        f.session.duplicateSelection({0, 50});
        f.session.transformAgain();
        f.session.transformAgain();
        // Three copies, 50 pt apart, each its own step.
        QCOMPARE(f.children(f.layer).size(), size_t(6));
        QCOMPARE(f.bounds(f.session.selection().front()), QRectF(10, 160, 40, 40));
        f.session.undo();
        QCOMPARE(f.children(f.layer).size(), size_t(5));
    }

    void anAltDragDuplicateRepeatsAsCopies()
    {
        Fixture f;
        f.session.select({f.a});
        f.session.beginInteraction(QStringLiteral("Duplicate"));
        f.session.previewDuplicateSelection();
        f.session.previewTransform(QTransform::fromTranslate(60, 0));
        f.session.commitInteraction();
        QCOMPARE(f.children(f.layer).size(), size_t(4));
        f.session.transformAgain();
        QCOMPARE(f.children(f.layer).size(), size_t(5));
        QCOMPARE(f.bounds(f.session.selection().front()), QRectF(130, 10, 40, 40));
        // The original stayed put.
        QCOMPARE(f.bounds(f.a), QRectF(10, 10, 40, 40));
    }

    void othersHideAndLockAsOneStep()
    {
        Fixture f;
        QVERIFY(f.session.anyOtherVisible(f.b));
        f.session.select({f.a});
        f.session.setOthersVisible(f.b, false);
        QCOMPARE(f.session.undoName(), QStringLiteral("Hide Others"));
        QVERIFY(!f.object(f.a).isVisible && f.object(f.b).isVisible && !f.object(f.c).isVisible);
        QVERIFY(f.session.selection().empty());
        QVERIFY(!f.session.anyOtherVisible(f.b));
        f.session.setOthersVisible(f.b, true);
        QVERIFY(f.object(f.a).isVisible && f.object(f.c).isVisible);
        f.session.setOthersLocked(f.b, true);
        QCOMPARE(f.session.undoName(), QStringLiteral("Lock Others"));
        QVERIFY(f.object(f.a).isLocked && !f.object(f.b).isLocked && f.object(f.c).isLocked);
        QVERIFY(!f.session.anyOtherUnlocked(f.b));
        f.session.undo();
        QVERIFY(!f.object(f.a).isLocked);
    }

    void layersDuplicateAndTakeAColor()
    {
        Fixture f;
        f.session.duplicateLayer(f.layer);
        QCOMPARE(f.session.undoName(), QStringLiteral("Duplicate Layer"));
        const std::vector<QUuid> layers = f.session.document()->layers();
        QCOMPARE(layers.size(), size_t(2));
        const QUuid copy = f.session.activeLayer().value();
        QVERIFY(copy != f.layer);
        QCOMPARE(f.object(copy).name, f.object(f.layer).name + QStringLiteral(" copy"));
        QCOMPARE(f.children(copy).size(), size_t(3));
        f.session.setLayerColor(f.layer, Qt::green);
        QCOMPARE(f.object(f.layer).layerColor, QColor(Qt::green));
        QCOMPARE(f.session.undoName(), QStringLiteral("Layer Color"));
    }

    void zoomToSelectionFramesIt()
    {
        Fixture f;
        f.session.resizeView({800, 600}, 1);
        f.session.actualSize();
        f.session.select({f.c});
        f.session.zoomToSelection();
        const QPointF centre = f.session.viewport.viewPoint(f.bounds(f.c).center(), f.session.document()->size);
        QVERIFY(QLineF(centre, QPointF(400, 300)).length() < 1);
        // 41 pt with the stroke fills the 504 pt left by the margin.
        QVERIFY(std::abs(f.session.viewport.zoom() - 504.0 / 41) < 0.01);
        QVERIFY(!f.session.canUndo() || f.session.undoName() == QStringLiteral("Draw Box"));
    }
};

QTEST_MAIN(SelectAndRepeatTests)
#include "SelectAndRepeatTests.moc"
