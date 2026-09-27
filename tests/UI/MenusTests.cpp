#include "Document/PathOperations.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/LayersPanel.h"
#include "UI/ProjectWorkspaceView.h"
#include <QMenu>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// The menu bar: keys, gates, names, toggles and fields.
namespace {
void clearShortcuts()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

QUuid box(EditorSession &session, double x)
{
    return session.addPath(Shapes::rectangle(QRectF(x, 10, 40, 40)), QStringLiteral("Box"));
}
}

class MenusTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void everyMenuKeyHasOneDefinition();
    void entriesNeedADocument();
    void undoAndRedoNameTheirSteps();
    void groupingFollowsTheSession();
    void viewTogglesAreChecked();
    void windowTogglesThePanels();
    void remappedKeysReachTheEntries();
    void aFocusedFieldKeepsUndo();
};

void MenusTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    clearShortcuts();
    QSettings().remove(ContentView::layersKey);
}

void MenusTests::cleanup()
{
    clearShortcuts();
    QSettings().remove(ContentView::layersKey);
}

void MenusTests::everyMenuKeyHasOneDefinition()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    QHash<QString, int> used;
    int keyed = 0;
    for (QAction *entry : window.menuBar()->findChildren<QAction *>()) {
        const QKeySequence original = entry->property("originalShortcut").value<QKeySequence>();
        if (original.isEmpty())
            continue;
        const ShortcutChord chord(original[0]);
        // Function keys are no chord: F7 stays unremappable.
        if (chord.key.size() != 1) {
            QCOMPARE(entry->objectName(), QString("showLayers"));
            continue;
        }
        keyed += 1;
        const auto definition = std::find_if(ShortcutDefinition::all().begin(), ShortcutDefinition::all().end(),
                                             [&](const ShortcutDefinition &each) { return each.isMenu() && each.original == chord; });
        QVERIFY2(definition != ShortcutDefinition::all().end(), qPrintable(entry->objectName()));
        used[definition->id()] += 1;
    }
    QCOMPARE(keyed, 40);
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.isMenu())
            QVERIFY2(used.value(definition.id()) == 1, qPrintable(definition.id()));
    }
    QCOMPARE(window.menus()->action("group")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_G));
}

void MenusTests::entriesNeedADocument()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    const QStringList documentOnly{"save", "saveAs", "place", "exportPNG", "exportSVG", "selectAll", "zoomIn", "artboardSize", "unlockAll"};
    for (const QString &name : documentOnly)
        QVERIFY2(!menus.action(name)->isEnabled(), qPrintable(name));
    QVERIFY(menus.action("newDocument")->isEnabled());
    QVERIFY(menus.action("open")->isEnabled());
    workspace.createDocument(QSizeF(200, 200));
    for (const QString &name : documentOnly)
        QVERIFY2(menus.action(name)->isEnabled(), qPrintable(name));
    // Selection entries wait for a selection.
    QVERIFY(!menus.action("copy")->isEnabled());
    QVERIFY(!menus.action("bringToFront")->isEnabled());
    box(workspace.current().session, 10);
    QVERIFY(menus.action("copy")->isEnabled());
    QVERIFY(menus.action("bringToFront")->isEnabled());
    QVERIFY(!menus.action("createOutlines")->isEnabled());
    menus.action("deselect")->trigger();
    QVERIFY(!workspace.current().session.hasSelection());
    QVERIFY(!menus.action("deselect")->isEnabled());
}

void MenusTests::undoAndRedoNameTheirSteps()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QCOMPARE(menus.action("undo")->text(), QString("Undo"));
    QVERIFY(!menus.action("undo")->isEnabled());
    workspace.createDocument(QSizeF(200, 200));
    box(workspace.current().session, 10);
    QCOMPARE(menus.action("undo")->text(), QString("Undo Draw Box"));
    menus.action("undo")->trigger();
    QVERIFY(workspace.current().session.document().value().children(workspace.current().session.activeLayer().value()).empty());
    QCOMPARE(menus.action("undo")->text(), QString("Undo"));
    QCOMPARE(menus.action("redo")->text(), QString("Redo Draw Box"));
    menus.action("redo")->trigger();
    QCOMPARE(int(workspace.current().session.document().value().children(workspace.current().session.activeLayer().value()).size()), 1);
}

void MenusTests::groupingFollowsTheSession()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    const QUuid first = box(session, 10), second = box(session, 80);
    session.select({first, second});
    QVERIFY(menus.action("group")->isEnabled());
    QVERIFY(!menus.action("ungroup")->isEnabled());
    QVERIFY(menus.action("makeCompoundPath")->isEnabled());
    menus.action("group")->trigger();
    QCOMPARE(int(session.selection().size()), 1);
    QCOMPARE(session.document().value().find(session.selection().front())->kind, ObjectKind::group);
    QVERIFY(menus.action("ungroup")->isEnabled());
    QCOMPARE(menus.action("ungroup")->isEnabled(), session.canUngroup());
    menus.action("ungroup")->trigger();
    QCOMPARE(int(session.selection().size()), 2);
    QVERIFY(!menus.action("ungroup")->isEnabled());
}

void MenusTests::viewTogglesAreChecked()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    for (const QString &name : {QStringLiteral("outline"), QStringLiteral("showGrid"), QStringLiteral("snapToGrid")})
        QVERIFY(!menus.action(name)->isChecked());
    menus.action("outline")->trigger();
    menus.action("showGrid")->trigger();
    menus.action("snapToGrid")->trigger();
    QVERIFY(session.showsOutline && session.showsGrid && session.snapsToGrid);
    for (const QString &name : {QStringLiteral("outline"), QStringLiteral("showGrid"), QStringLiteral("snapToGrid")})
        QVERIFY(menus.action(name)->isChecked());
    menus.action("outline")->trigger();
    QVERIFY(!session.showsOutline);
    QVERIFY(!menus.action("outline")->isChecked());
}

void MenusTests::windowTogglesThePanels()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QVERIFY(menus.action("showLayers")->isChecked());
    QVERIFY(!window.content()->layersPanel().isHidden());
    menus.action("showLayers")->trigger();
    QVERIFY(window.content()->layersPanel().isHidden());
    QVERIFY(!menus.action("showLayers")->isChecked());
    QCOMPARE(QSettings().value(ContentView::layersKey).toBool(), false);
    // A new editor remembers the choice.
    workspace.createDocument(QSizeF(10, 10));
    workspace.createDocument(QSizeF(10, 10));
    QVERIFY(window.content()->layersPanel().isHidden());
    menus.action("showLayers")->trigger();
    QVERIFY(!window.content()->layersPanel().isHidden());
}

void MenusTests::remappedKeysReachTheEntries()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    const QString group = QStringLiteral("Menus:Group");
    QVERIFY(ShortcutSettings::shared().save({{group, ShortcutChord(QStringLiteral("h"), 3)}}));
    QCOMPARE(menus.action("group")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    QCOMPARE(menus.action("ungroup")->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    clearShortcuts();
    QCOMPARE(menus.action("group")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_G));
}

void MenusTests::aFocusedFieldKeepsUndo()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(200, 200));
    box(workspace.current().session, 10);
    auto *field = new QLineEdit(&window);
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    field->setFocus();
    QTRY_VERIFY(field->hasFocus());
    field->insert(QStringLiteral("abc"));
    // Bare words: the field's undo is no named step.
    QCOMPARE(menus.action("undo")->text(), QString("Undo"));
    menus.action("undo")->trigger();
    QCOMPARE(field->text(), QString());
    QVERIFY(workspace.current().session.canUndo());
    QCOMPARE(workspace.current().session.undoName(), QString("Draw Box"));
    field->clearFocus();
    window.content()->canvas().setFocus();
    QTRY_COMPARE(menus.action("undo")->text(), QString("Undo Draw Box"));
}

QTEST_MAIN(MenusTests)
#include "MenusTests.moc"
