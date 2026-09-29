#include "Canvas/EditorCanvas.h"
#include "ContentView.h"
#include "Document/PathOperations.h"
#include "UI/ContextMenus.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/Menus.h"
#include "UI/NativeLayerList.h"
#include "UI/ObjectDialogs.h"
#include "UI/ProjectWorkspaceView.h"
#include <QContextMenuEvent>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QtTest>

// Right-click menus on the canvas and the Layers rows, the Select menu, and the new Edit and View keys.
namespace {
QUuid box(EditorSession &session, double x, const QString &name = QStringLiteral("Box"))
{
    return session.addPath(Shapes::rectangle(QRectF(x, 10, 40, 40)), name);
}

// The names of a menu's entries in order, separators as "-", without leading or doubled separators.
QStringList names(const QMenu &menu)
{
    QStringList result;
    for (QAction *entry : menu.actions()) {
        if (entry->isSeparator()) {
            if (!result.isEmpty() && result.last() != QLatin1String("-"))
                result << QStringLiteral("-");
        } else {
            result << entry->objectName();
        }
    }
    if (!result.isEmpty() && result.last() == QLatin1String("-"))
        result.removeLast();
    return result;
}

QMenu *submenu(const QMenu &menu, const QString &name)
{
    for (QAction *entry : menu.actions()) {
        if (entry->objectName() == name)
            return entry->menu();
    }
    return nullptr;
}

struct Window {
    ProjectWorkspace workspace;
    ProjectWorkspaceView view{workspace};

    Window()
    {
        workspace.createDocument(QSizeF(400, 300));
        view.resize(1200, 800);
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            qWarning("window never exposed");
    }
    EditorSession &session() { return workspace.current().session; }
    Menus &menus() { return *view.menus(); }
    EditorCanvas &canvas() { return view.content()->canvas(); }
    std::unique_ptr<QMenu> canvasMenu(const QList<QUuid> &hits = {})
    {
        return std::unique_ptr<QMenu>(ContextMenus::forCanvas(menus(), session(), canvas(), hits, nullptr));
    }
};
}

class ContextMenusTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
        ShortcutSettings::shared().reload();
    }

    void artboardMenusOfferExportArtboard()
    {
        Window w;
        const std::unique_ptr<QMenu> menu = w.canvasMenu();
        QMenu *artboards = submenu(*menu, QStringLiteral("contextArtboards"));
        QVERIFY(artboards);
        QVERIFY(names(*artboards).contains("artboardExported"));
        w.session().selectTool(Tool::artboard);
        QVERIFY(names(*w.canvasMenu()).contains("artboardExported"));
    }

    void emptyCanvasOffersPasteViewAndGenerate()
    {
        Window w;
        const std::unique_ptr<QMenu> menu = w.canvasMenu();
        const QStringList entries = names(*menu);
        QCOMPARE(entries.first(), QStringLiteral("generate"));
        QVERIFY(entries.contains("selectAll") && entries.contains("fitArtboard") && entries.contains("showGrid"));
        // Nothing to paste yet, and nothing that needs a selection.
        QVERIFY(!entries.contains("paste") && !entries.contains("copy") && !entries.contains("askAI"));
    }

    void anObjectsMenuLeadsWithAskAIAndListsOnlyWhatApplies()
    {
        Window w;
        box(w.session(), 10);
        const std::unique_ptr<QMenu> menu = w.canvasMenu();
        const QStringList entries = names(*menu);
        QCOMPARE(entries.first(), QStringLiteral("askAI"));
        QVERIFY(entries.contains("cut") && entries.contains("copy") && entries.contains("duplicate") && entries.contains("delete"));
        QVERIFY(entries.contains("contextArrange") && entries.contains("contextTransform") && entries.contains("contextPath"));
        // One path: no grouping, masks, alignment, Pathfinder, outlines or tracing.
        for (const char *absent : {"group", "ungroup", "makeClippingMask", "contextAlign", "contextPathfinder", "createOutlines",
                                   "imageTraceMake", "isolateGroup", "selectLayerMenu", "paste"})
            QVERIFY2(!entries.contains(QString::fromLatin1(absent)), absent);
        // Transform ▸ starts empty-handed: nothing to repeat yet.
        QVERIFY(!names(*submenu(*menu, "contextTransform")).contains("transformAgain"));
        QVERIFY(names(*submenu(*menu, "contextTransform")).contains("flipHorizontal"));
    }

    void askAIOpensTheInstructionScopedToTheClickedObject()
    {
        Window w;
        box(w.session(), 10, QStringLiteral("Logo"));
        const std::unique_ptr<QMenu> menu = w.canvasMenu();
        QAction *ask = menu->findChild<QAction *>(QStringLiteral("askAI"));
        QVERIFY(ask);
        ask->trigger();
        QDialog *sheet = nullptr;
        QTRY_VERIFY((sheet = w.view.findChild<QDialog *>(QStringLiteral("editInstructionSheet"))));
        QCOMPARE(sheet->findChild<QLabel *>(QStringLiteral("instructionScope"))->text(), QStringLiteral("Applies to “Logo”."));
        sheet->close();
    }

    void severalObjectsGetGroupingAlignAndPathfinder()
    {
        Window w;
        const QUuid a = box(w.session(), 10);
        const QUuid b = box(w.session(), 30);
        w.session().select({a, b});
        w.session().copy();
        const std::unique_ptr<QMenu> menu = w.canvasMenu({b, a});
        const QStringList entries = names(*menu);
        QCOMPARE(entries.first(), QStringLiteral("askAI"));
        // The layers under the pointer come right after, topmost first.
        QCOMPARE(entries.at(1), QStringLiteral("selectLayerMenu"));
        QMenu *picker = submenu(*menu, "selectLayerMenu");
        QCOMPARE(picker->title(), QStringLiteral("Select"));
        QCOMPARE(picker->actions().size(), 2);
        QVERIFY(entries.contains("group") && entries.contains("makeClippingMask") && entries.contains("contextAlign")
                && entries.contains("contextPathfinder") && entries.contains("paste") && entries.contains("pasteInFront"));
        // Picking a layer selects just it.
        picker->actions().at(1)->trigger();
        QCOMPARE(w.session().selection(), std::vector<QUuid>{a});
    }

    void groupsAndTextGetTheirOwnEntries()
    {
        Window w;
        const QUuid a = box(w.session(), 10);
        const QUuid b = box(w.session(), 60);
        w.session().select({a, b});
        w.session().groupSelection();
        std::unique_ptr<QMenu> menu = w.canvasMenu();
        QStringList entries = names(*menu);
        QVERIFY(entries.contains("ungroup") && entries.contains("isolateGroup") && !entries.contains("group"));
        menu->findChild<QAction *>(QStringLiteral("isolateGroup"))->trigger();
        QVERIFY(w.canvas().isolatedGroup());
        w.session().select({a});
        menu = w.canvasMenu();
        QVERIFY(names(*menu).contains("exitIsolation"));
        w.canvas().exitIsolation();
        w.session().addText({10, 200}, QStringLiteral("Words"));
        menu = w.canvasMenu();
        entries = names(*menu);
        QVERIFY(entries.contains("createOutlines") && !entries.contains("contextPath"));
        QVERIFY(names(*submenu(*menu, "contextSelectSame")).contains("selectSameFontFamily"));
    }

    void aRightClickOnTheCanvasOpensTheMenu()
    {
        Window w;
        const QUuid a = box(w.session(), 10);
        w.session().deselectAll();
        EditorCanvas &canvas = w.canvas();
        const QPoint at = w.session().viewport.viewPoint(QPointF(30, 30), w.session().document()->size).toPoint();
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, canvas.mapToGlobal(at));
        QApplication::sendEvent(&canvas, &event);
        QCOMPARE(w.session().selection(), std::vector<QUuid>{a});
        QMenu *menu = canvas.findChild<QMenu *>(QStringLiteral("canvasContextMenu"));
        QVERIFY(menu);
        QCOMPARE(names(*menu).first(), QStringLiteral("askAI"));
        menu->close();
    }

    void aLayerRowsMenuActsOnThatRow()
    {
        Window w;
        EditorSession &session = w.session();
        const QUuid layer = session.activeLayer().value();
        const QUuid a = box(session, 10);
        const QUuid b = box(session, 60);
        NativeLayerList &list = *w.view.findChild<NativeLayerList *>();
        std::unique_ptr<QMenu> menu(ContextMenus::forLayerRow(&w.menus(), session, list, layer, nullptr));
        QStringList entries = names(*menu);
        QVERIFY(entries.contains("layerDuplicate") && entries.contains("layerColorMenu") && entries.contains("layerSelectChildren"));
        QVERIFY(!entries.contains("group"));
        menu->findChild<QAction *>(QStringLiteral("layerSelectChildren"))->trigger();
        QCOMPARE(session.selection(), (std::vector<QUuid>{a, b}));
        session.select({a});
        menu.reset(ContextMenus::forLayerRow(&w.menus(), session, list, a, nullptr));
        entries = names(*menu);
        QVERIFY(entries.contains("duplicate") && !entries.contains("layerDuplicate") && !entries.contains("layerColorMenu"));
        QCOMPARE(menu->findChild<QAction *>(QStringLiteral("layerHideOthers"))->text(), QStringLiteral("Hide Others"));
        menu->findChild<QAction *>(QStringLiteral("layerHideOthers"))->trigger();
        QVERIFY(!session.document()->find(b)->isVisible);
        QCOMPARE(session.undoName(), QStringLiteral("Hide Others"));
        menu.reset(ContextMenus::forLayerRow(&w.menus(), session, list, a, nullptr));
        QCOMPARE(menu->findChild<QAction *>(QStringLiteral("layerHideOthers"))->text(), QStringLiteral("Show Others"));
        // Without the menu bar the row still has its own entries.
        menu.reset(ContextMenus::forLayerRow(nullptr, session, list, a, nullptr));
        QVERIFY(!names(*menu).contains("duplicate") && names(*menu).contains("layerRename"));
    }

    void altClickingAnEyeHidesTheOthers()
    {
        Window w;
        EditorSession &session = w.session();
        const QUuid a = box(session, 10);
        const QUuid b = box(session, 60);
        NativeLayerList &list = *w.view.findChild<NativeLayerList *>();
        LayerCell *cell = nullptr;
        for (LayerCell *each : list.cells()) {
            if (each->objectID() == a)
                cell = each;
        }
        QVERIFY(cell);
        auto *eye = cell->findChild<QToolButton *>(QStringLiteral("layerEye"));
        QTest::mouseClick(eye, Qt::LeftButton, Qt::AltModifier);
        QVERIFY(session.document()->find(a)->isVisible);
        QVERIFY(!session.document()->find(b)->isVisible);
        // A plain click afterwards is a plain click.
        for (LayerCell *each : list.cells()) {
            if (each->objectID() == a)
                cell = each;
        }
        cell->findChild<QToolButton *>(QStringLiteral("layerEye"))->click();
        QVERIFY(!session.document()->find(a)->isVisible);
        QVERIFY(!session.document()->find(b)->isVisible);
    }

    void theSelectMenuSitsBeforeTypeAndFollowsTheSelection()
    {
        Window w;
        QStringList titles;
        for (QAction *entry : w.view.menuBar()->actions())
            titles << entry->text();
        QVERIFY(titles.indexOf("&Select") == titles.indexOf("&Object") + 1);
        Menus &menus = w.menus();
        QVERIFY(!menus.action("nextObjectAbove")->isEnabled());
        QVERIFY(!menus.action("reselect")->isEnabled());
        const QUuid a = box(w.session(), 10);
        const QUuid b = box(w.session(), 60);
        w.session().select({a});
        QVERIFY(menus.action("nextObjectAbove")->isEnabled());
        QVERIFY(!menus.action("selectSameFontFamily")->isEnabled());
        menus.action("selectInverse")->trigger();
        QCOMPARE(w.session().selection(), std::vector<QUuid>{b});
        QVERIFY(menus.action("reselect")->isEnabled());
        menus.action("nextObjectBelow")->trigger();
        QCOMPARE(w.session().selection(), std::vector<QUuid>{a});
    }

    void editAndViewKeys()
    {
        Window w;
        Menus &menus = w.menus();
        QCOMPARE(menus.action("transformAgain")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_D));
        QCOMPARE(menus.action("duplicate")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_D));
        QCOMPARE(menus.action("pasteInFront")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F));
        QCOMPARE(menus.action("pasteInBack")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_B));
        // Figma's Shift+1 and Shift+2 ride along with the remappable keys.
        QCOMPARE(menus.action("fitArtboard")->shortcuts(), (QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::SHIFT | Qt::Key_1)}));
        QCOMPARE(menus.action("zoomToSelection")->shortcuts().at(1), QKeySequence(Qt::SHIFT | Qt::Key_2));
        QVERIFY(!menus.action("transformAgain")->isEnabled());
        QVERIFY(!menus.action("zoomToSelection")->isEnabled());
        box(w.session(), 10);
        QVERIFY(menus.action("zoomToSelection")->isEnabled());
        w.session().moveSelection({10, 0});
        QVERIFY(menus.action("transformAgain")->isEnabled());
        menus.action("transformAgain")->trigger();
        QCOMPARE(w.session().selectionBounds().left(), 30.0);
        // A remap keeps the alias.
        QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Menus:Zoom to Selection"), ShortcutChord("9", 3)}}));
        QCOMPARE(menus.action("zoomToSelection")->shortcuts(), (QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_9), QKeySequence(Qt::SHIFT | Qt::Key_2)}));
        QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
        ShortcutSettings::shared().reload();
    }

    void preferencesSetTheKeyboardIncrement()
    {
        Window w;
        QVERIFY(w.menus().action("preferences")->isEnabled());
        QDialog *dialog = ObjectDialogs::preferences(&w.view);
        auto *increment = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("keyboardIncrement"));
        QCOMPARE(increment->value(), 1.0);
        increment->setValue(2.5);
        dialog->findChild<QPushButton *>(QStringLiteral("dialogOK"))->click();
        QCOMPARE(EditorCanvas::keyboardIncrement(), 2.5);
        const QUuid a = box(w.session(), 10);
        w.canvas().setFocus();
        QTest::keyClick(&w.canvas(), Qt::Key_Left);
        QCOMPARE(w.session().document()->bounds(a).left(), 7.5);
        EditorCanvas::setKeyboardIncrement(1);
    }
};

QTEST_MAIN(ContextMenusTests)
#include "ContextMenusTests.moc"
