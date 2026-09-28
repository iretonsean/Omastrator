#include "Document/PathOperations.h"
#include "UI/LayersPanel.h"
#include "UI/NativeLayerList.h"
#include <QLineEdit>
#include <QStandardPaths>
#include <QtTest>

// The layer tree: rows, clicks, toggles, renames and drags.
namespace {
struct Fixture {
    EditorSession session;
    QUuid layer1, layer2, a, b, c;

    // Layer 1 holds a, b, c bottom-up; Layer 2 empty.
    Fixture()
    {
        session.createDocument(QSizeF(400, 300));
        layer1 = session.activeLayer().value();
        a = session.addPath(Shapes::rectangle(QRectF(10, 10, 20, 20)), QStringLiteral("A"));
        b = session.addPath(Shapes::rectangle(QRectF(40, 10, 20, 20)), QStringLiteral("B"));
        c = session.addPath(Shapes::rectangle(QRectF(70, 10, 20, 20)), QStringLiteral("C"));
        session.deselectAll();
        layer2 = session.addLayer();
        session.setActiveLayer(layer1);
    }
    std::vector<QUuid> children(const QUuid &parent) const { return session.document()->children(parent); }
};

std::vector<QUuid> ids(const NativeLayerList &list)
{
    std::vector<QUuid> result;
    for (const LayerCell *cell : list.cells())
        result.push_back(cell->objectID());
    return result;
}

LayerCell &cellFor(const NativeLayerList &list, const QUuid &id)
{
    for (LayerCell *cell : list.cells()) {
        if (cell->objectID() == id)
            return *cell;
    }
    throw std::runtime_error("no row");
}

// A list point at a fraction of the row's height.
QPoint at(const NativeLayerList &list, const QUuid &id, double fraction)
{
    LayerCell &cell = cellFor(list, id);
    return cell.mapTo(&list, QPoint(cell.width() / 2, int(cell.height() * fraction)));
}

std::unique_ptr<QMimeData> dragOf(const NativeLayerList &list, const QUuid &id)
{
    return list.dragData(cellFor(list, id));
}
}

class NativeLayerListTests : public QObject {
    Q_OBJECT
private slots:
    void nameWithAIAsksTheAgent();
    void initTestCase();
    void rowsListLayersTopDownAndFoldClosedGroups();
    void selectionKeepsTheRowsThemselves();
    void clicksSelectToggleAndRange();
    void aLayerRowTargetsTheLayer();
    void eyeAndLockToggle();
    void renamingTakesReturnAndEscape();
    void dropsPlaceAboveBelowAndInto();
    void aDropIntoItselfIsRefused();
    void layersDragAmongLayers();
    void panelButtonsAddAndDelete();
};

void NativeLayerListTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void NativeLayerListTests::rowsListLayersTopDownAndFoldClosedGroups()
{
    Fixture f;
    NativeLayerList list(f.session);
    QCOMPARE(ids(list), (std::vector<QUuid>{f.layer2, f.layer1, f.c, f.b, f.a}));
    const std::vector<NativeLayerList::Row> rows = NativeLayerList::rows(*f.session.document());
    QCOMPARE(rows.at(2).depth, 1);
    QCOMPARE(rows.at(0).depth, 0);
    // A group's children sit one deeper, hidden when closed.
    f.session.select({f.a, f.b});
    f.session.groupSelection();
    const QUuid group = f.session.selection().front();
    QCOMPARE(ids(list), (std::vector<QUuid>{f.layer2, f.layer1, f.c, group, f.b, f.a}));
    QCOMPARE(NativeLayerList::rows(*f.session.document()).at(4).depth, 2);
    f.session.setExpanded(group, false);
    QCOMPARE(ids(list), (std::vector<QUuid>{f.layer2, f.layer1, f.c, group}));
    f.session.setExpanded(f.layer1, false);
    QCOMPARE(ids(list), (std::vector<QUuid>{f.layer2, f.layer1}));
}

void NativeLayerListTests::selectionKeepsTheRowsThemselves()
{
    Fixture f;
    NativeLayerList list(f.session);
    const std::vector<LayerCell *> before = list.cells();
    f.session.select({f.b});
    QCOMPARE(list.cells(), before);
    QVERIFY(list.isHighlighted(f.b));
    QVERIFY(!list.isHighlighted(f.a));
    // Nothing selected: the active layer shows as the target.
    f.session.deselectAll();
    QVERIFY(list.isHighlighted(f.layer1));
    QVERIFY(!list.isHighlighted(f.layer2));
}

void NativeLayerListTests::clicksSelectToggleAndRange()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    QTest::mouseClick(&cellFor(list, f.c), Qt::LeftButton, Qt::NoModifier, QPoint(150, 15));
    QCOMPARE(f.session.selection(), std::vector<QUuid>{f.c});
    // Ctrl adds, then takes away.
    QTest::mouseClick(&cellFor(list, f.a), Qt::LeftButton, Qt::ControlModifier, QPoint(150, 15));
    QCOMPARE(f.session.selection().size(), size_t(2));
    QVERIFY(f.session.isSelected(f.a) && f.session.isSelected(f.c));
    QTest::mouseClick(&cellFor(list, f.a), Qt::LeftButton, Qt::ControlModifier, QPoint(150, 15));
    QCOMPARE(f.session.selection(), std::vector<QUuid>{f.c});
    // Shift runs from the anchor, layers left out.
    QTest::mouseClick(&cellFor(list, f.c), Qt::LeftButton, Qt::NoModifier, QPoint(150, 15));
    QTest::mouseClick(&cellFor(list, f.a), Qt::LeftButton, Qt::ShiftModifier, QPoint(150, 15));
    QCOMPARE(f.session.selection().size(), size_t(3));
    QVERIFY(f.session.isSelected(f.a) && f.session.isSelected(f.b) && f.session.isSelected(f.c));
    // Below every row lets go.
    QTest::mouseClick(list.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 390));
    QVERIFY(!f.session.hasSelection());
}

void NativeLayerListTests::aLayerRowTargetsTheLayer()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    f.session.select({f.a});
    QTest::mouseClick(&cellFor(list, f.layer2), Qt::LeftButton, Qt::NoModifier, QPoint(150, 15));
    QCOMPARE(f.session.activeLayer().value(), f.layer2);
    QVERIFY(!f.session.hasSelection());
    // New art now goes into the targeted layer.
    const QUuid d = f.session.addPath(Shapes::ellipse(QRectF(0, 0, 10, 10)), QStringLiteral("D"));
    QCOMPARE(f.session.document()->find(d)->parentID.value(), f.layer2);
}

void NativeLayerListTests::eyeAndLockToggle()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    cellFor(list, f.b).findChild<QToolButton *>("layerEye")->click();
    QVERIFY(!f.session.document()->find(f.b)->isVisible);
    QCOMPARE(f.session.undoName(), QString("Hide"));
    cellFor(list, f.b).findChild<QToolButton *>("layerEye")->click();
    QVERIFY(f.session.document()->find(f.b)->isVisible);
    cellFor(list, f.layer1).findChild<QToolButton *>("layerLock")->click();
    QVERIFY(f.session.document()->find(f.layer1)->isLocked);
    cellFor(list, f.layer1).findChild<QToolButton *>("layerLock")->click();
    QVERIFY(!f.session.document()->find(f.layer1)->isLocked);
    // The disclosure folds the layer without an undo step.
    const QString undo = f.session.undoName();
    cellFor(list, f.layer1).findChild<QToolButton *>("layerDisclosure")->click();
    QVERIFY(!f.session.document()->find(f.layer1)->isExpanded);
    QCOMPARE(list.cells().size(), size_t(2));
    QCOMPARE(f.session.undoName(), undo);
}

void NativeLayerListTests::renamingTakesReturnAndEscape()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    QTest::mouseDClick(&cellFor(list, f.b), Qt::LeftButton, Qt::NoModifier, QPoint(150, 15));
    auto *editor = cellFor(list, f.b).findChild<QLineEdit *>("layerNameEditor");
    QVERIFY(cellFor(list, f.b).isRenaming() && editor->isVisible());
    QCOMPARE(editor->text(), QString("B"));
    editor->setText(QStringLiteral("Roof"));
    QTest::keyClick(editor, Qt::Key_Return);
    QCOMPARE(f.session.document()->find(f.b)->name, QString("Roof"));
    QCOMPARE(cellFor(list, f.b).findChild<QLabel *>("layerName")->text(), QString("Roof"));
    QCOMPARE(f.session.undoName(), QString("Rename"));
    // Escape keeps the old name.
    QTest::mouseDClick(&cellFor(list, f.b), Qt::LeftButton, Qt::NoModifier, QPoint(150, 15));
    editor->setText(QStringLiteral("Wall"));
    QTest::keyClick(editor, Qt::Key_Escape);
    QCOMPARE(f.session.document()->find(f.b)->name, QString("Roof"));
    QVERIFY(!cellFor(list, f.b).isRenaming());
}

void NativeLayerListTests::dropsPlaceAboveBelowAndInto()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    // A above C: the top of Layer 1.
    QVERIFY(list.acceptDrop(*dragOf(list, f.a), at(list, f.c, 0.2)));
    QCOMPARE(f.children(f.layer1), (std::vector<QUuid>{f.b, f.c, f.a}));
    QCOMPARE(f.session.undoName(), QString("Move Object"));
    QCOMPARE(f.session.selection(), std::vector<QUuid>{f.a});
    // One undo step puts it back.
    f.session.undo();
    QCOMPARE(f.children(f.layer1), (std::vector<QUuid>{f.a, f.b, f.c}));
    // C below A: the bottom.
    QVERIFY(list.acceptDrop(*dragOf(list, f.c), at(list, f.a, 0.8)));
    QCOMPARE(f.children(f.layer1), (std::vector<QUuid>{f.c, f.a, f.b}));
    // Onto another layer's row: on its top.
    QVERIFY(list.acceptDrop(*dragOf(list, f.b), at(list, f.layer2, 0.5)));
    QCOMPARE(f.children(f.layer2), std::vector<QUuid>{f.b});
    QCOMPARE(f.children(f.layer1), (std::vector<QUuid>{f.c, f.a}));
    // A selection moves together, keeping its order.
    f.session.select({f.c, f.a});
    QVERIFY(list.acceptDrop(*dragOf(list, f.a), at(list, f.b, 0.2)));
    QCOMPARE(f.children(f.layer2), (std::vector<QUuid>{f.b, f.c, f.a}));
    QCOMPARE(f.session.undoName(), QString("Move Objects"));
}

void NativeLayerListTests::aDropIntoItselfIsRefused()
{
    Fixture f;
    f.session.select({f.a, f.b});
    f.session.groupSelection();
    const QUuid group = f.session.selection().front();
    f.session.deselectAll();
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    // C into the group's middle lands on its top.
    QVERIFY(list.acceptDrop(*dragOf(list, f.c), at(list, group, 0.5)));
    QCOMPARE(f.children(group), (std::vector<QUuid>{f.a, f.b, f.c}));
    // The group into its own child is refused.
    QVERIFY(!list.dropTarget(*dragOf(list, group), at(list, f.b, 0.2)));
    QVERIFY(!list.acceptDrop(*dragOf(list, group), at(list, f.b, 0.2)));
    // Layers land only among layers; strangers' data nowhere.
    QVERIFY(!list.dropTarget(*dragOf(list, f.layer2), at(list, f.c, 0.2)));
    NativeLayerList other(f.session);
    QVERIFY(!list.dropTarget(*other.dragData(*other.cells().at(3)), at(list, f.c, 0.2)));
    QCOMPARE(f.children(group), (std::vector<QUuid>{f.a, f.b, f.c}));
}

void NativeLayerListTests::layersDragAmongLayers()
{
    Fixture f;
    NativeLayerList list(f.session);
    list.resize(300, 400);
    list.show();
    QVERIFY(QTest::qWaitForWindowExposed(&list));
    // Rows top down: Layer 2, Layer 1, C, B, A.
    QCOMPARE(f.session.document()->layers(), (std::vector<QUuid>{f.layer1, f.layer2}));
    const std::optional<LayerDropTarget> above = list.dropTarget(*dragOf(list, f.layer1), at(list, f.layer2, 0.2));
    QVERIFY(above);
    QVERIFY(above->parent.isNull());
    QVERIFY(!above->fills);
    QVERIFY(list.acceptDrop(*dragOf(list, f.layer1), at(list, f.layer2, 0.2)));
    QCOMPARE(f.session.document()->layers(), (std::vector<QUuid>{f.layer2, f.layer1}));
    QCOMPARE(f.children(f.layer1), (std::vector<QUuid>{f.a, f.b, f.c}));
    QCOMPARE(f.session.undoName(), QString("Move Layer"));
    f.session.undo();
    QCOMPARE(f.session.document()->layers(), (std::vector<QUuid>{f.layer1, f.layer2}));
    // Below a layer's bottom half: under it and all its rows.
    list.update();
    QVERIFY(list.acceptDrop(*dragOf(list, f.layer2), at(list, f.layer1, 0.8)));
    QCOMPARE(f.session.document()->layers(), (std::vector<QUuid>{f.layer2, f.layer1}));
    // Past the last row: the bottom.
    f.session.undo();
    list.update();
    const LayerCell &last = *list.cells().back();
    QVERIFY(list.acceptDrop(*dragOf(list, f.layer2), last.mapTo(&list, QPoint(last.width() / 2, last.height() + 20))));
    QCOMPARE(f.session.document()->layers(), (std::vector<QUuid>{f.layer2, f.layer1}));
    // Onto itself, onto art or into a group: refused.
    list.update();
    QVERIFY(!list.dropTarget(*dragOf(list, f.layer2), at(list, f.layer2, 0.2)));
    QVERIFY(!list.dropTarget(*dragOf(list, f.layer2), at(list, f.b, 0.5)));
    // Art still never lands at the top level: onto a layer row it goes into the layer.
    const std::optional<LayerDropTarget> art = list.dropTarget(*dragOf(list, f.a), at(list, f.layer2, 0.2));
    QVERIFY(art);
    QCOMPARE(art->parent, f.layer2);
}

void NativeLayerListTests::panelButtonsAddAndDelete()
{
    Fixture f;
    LayersPanel panel(f.session);
    auto *count = panel.findChild<QLabel *>("layerCount");
    QCOMPARE(count->text(), QString("2"));
    panel.findChild<QToolButton *>("newLayer")->click();
    QCOMPARE(f.session.document()->layers().size(), size_t(3));
    QCOMPARE(count->text(), QString("3"));
    const QUuid added = f.session.activeLayer().value();
    QCOMPARE(f.session.document()->find(added)->name, QString("Layer 3"));
    // With a selection, Delete takes the selection.
    auto *remove = panel.findChild<QToolButton *>("deleteLayer");
    f.session.select({f.b});
    QCOMPARE(remove->toolTip(), QString("Delete selection"));
    remove->click();
    QVERIFY(!f.session.document()->find(f.b));
    QCOMPARE(f.session.document()->layers().size(), size_t(3));
    // Without one, the active layer goes.
    QCOMPARE(remove->toolTip(), QString("Delete layer"));
    f.session.setActiveLayer(f.layer1);
    remove->click();
    QVERIFY(!f.session.document()->find(f.layer1));
    QVERIFY(!f.session.document()->find(f.a));
    QCOMPARE(count->text(), QString("2"));
}

// Name with AI: hidden without an agent; a click asks for names for what things are, no convention.
void NativeLayerListTests::nameWithAIAsksTheAgent()
{
    Fixture f;
    LayersPanel panel(f.session);
    panel.show();
    auto *button = panel.findChild<QToolButton *>("nameLayersWithAI");
    QVERIFY(button && !button->isVisible());
    QStringList asked;
    panel.setNamer([&asked](const QString &convention) {
        asked << convention;
        return QString();
    });
    QVERIFY(button->isVisible() && button->isEnabled());
    button->click();
    QCOMPARE(asked, QStringList{QString()});
}

QTEST_MAIN(NativeLayerListTests)
#include "NativeLayerListTests.moc"
