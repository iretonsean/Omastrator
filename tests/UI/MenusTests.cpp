#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "Document/PathOperations.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/LayersPanel.h"
#include "UI/MotionTimeline.h"
#include "UI/NumberField.h"
#include "UI/ProjectTabs.h"
#include "UI/ProjectWorkspaceView.h"
#include "TemporaryConfig.h"
#include <QImage>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QStandardPaths>
#include <QtTest>

// The menu bar: keys, gates, names, toggles and fields.
namespace {
void clearShortcuts()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

class PageHost : public BrowserViewHost {
public:
    ElementState elementState(const QUuid &) const override { return state; }
    EditBoxes editBoxes(const QUuid &) const override { return boxes; }
    ElementState state;
    EditBoxes boxes;
    QImage picture(const QUuid &) const override { return {}; }
    QString message(const QUuid &) const override { return {}; }
    QString beginEditPage(const QUuid &) override { return {}; }
    bool canUndoPageEdit(const QUuid &) const override { return undoable; }
    bool canRedoPageEdit(const QUuid &) const override { return false; }
    void undoPageEdit(const QUuid &) override { ++undone; }
    void act(const QUuid &frame, Action action) override { acted.push_back({frame, action}); }
    bool undoable = true;
    int undone = 0;
    QList<QPair<QUuid, Action>> acted;
};

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
    void undoInEditPageIsThePagesOwn();
    void enterInAnElementFieldLeavesUndoToThePage();
    void browserViewItemsFollowTheFrameEvenInEditPage();
    void pageEntriesFollowTheDocument();
    void groupingFollowsTheSession();
    void viewTogglesAreChecked();
    void windowTogglesThePanels();
    void windowHasTheTimelineAndMotionEntries();
    void exportArtboardIsACheckedToggleOnTheActiveArtboard();
    void remappedKeysReachTheEntries();
    void aFocusedFieldKeepsUndo();
    void closingWithTheShortcutsPanelOpen();
    void typeKeysStyleSelectedType();
    void altArrowsDuplicateAnythingElse();
    void typeKeysKernAtACaretWhileTyping();
    void typeMenuConvertsPointAndArea();
    void figmaKeysWorkFromAnyPanel();
    void figmaKeysSitBesideIllustratorsOnTheirEntries();
    void lockDocumentTogglesAndClosesTheEditingEntries();
    void aLockedDocumentShowsItsLockAndSaysWhyOnARefusedEdit();
};

void MenusTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
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
        // Function keys are no chord: F7 stays unremappable, and so do the artboard page keys.
        if (chord.key.size() != 1) {
            QVERIFY2(entry->objectName() == QString("showLayers") || entry->objectName() == QString("nextArtboard")
                         || entry->objectName() == QString("previousArtboard") || entry->objectName() == QString("nextPage")
                         || entry->objectName() == QString("previousPage"),
                     qPrintable(entry->objectName()));
            continue;
        }
        keyed += 1;
        const auto definition = std::find_if(ShortcutDefinition::all().begin(), ShortcutDefinition::all().end(),
                                             [&](const ShortcutDefinition &each) { return each.isMenu() && each.original == chord; });
        QVERIFY2(definition != ShortcutDefinition::all().end(), qPrintable(entry->objectName()));
        used[definition->id()] += 1;
    }
    QCOMPARE(keyed, 80);
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

void MenusTests::pageEntriesFollowTheDocument()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QVERIFY(!menus.action("pagesMenu")->isEnabled());
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    for (const char *name : {"pagesMenu", "newPage", "duplicatePage", "renamePage"})
        QVERIFY2(menus.action(QString::fromLatin1(name))->isEnabled(), name);
    // One page: nothing to delete, walk or move to.
    for (const char *name : {"deletePage", "nextPage", "previousPage", "moveToPageMenu"})
        QVERIFY2(!menus.action(QString::fromLatin1(name))->isEnabled(), name);
    QCOMPARE(menus.action("nextPage")->shortcut(), QKeySequence(Qt::ALT | Qt::Key_PageDown));
    QCOMPARE(menus.action("previousPage")->shortcut(), QKeySequence(Qt::ALT | Qt::Key_PageUp));
    const QUuid first = session.currentPage();
    menus.action("newPage")->trigger();
    QCOMPARE(session.document()->pageCount(), 2);
    QCOMPARE(session.undoName(), QString("New Page"));
    QVERIFY(menus.action("deletePage")->isEnabled() && menus.action("nextPage")->isEnabled());
    // Move to Page waits for a selection.
    QVERIFY(!menus.action("moveToPageMenu")->isEnabled());
    box(session, 10);
    QVERIFY(menus.action("moveToPageMenu")->isEnabled());
    QMenu *move = menus.action("moveToPageMenu")->menu();
    QMetaObject::invokeMethod(move, "aboutToShow");
    QCOMPARE(move->actions().size(), 1);
    QCOMPARE(move->actions().first()->text(), QString("Page 1"));
    menus.action("nextPage")->trigger();
    QCOMPARE(session.currentPage(), first);
    menus.action("previousPage")->trigger();
    QCOMPARE(session.document()->pageIndex(session.currentPage()), 1);
    menus.action("deletePage")->trigger();
    QCOMPARE(session.document()->pageCount(), 1);
    QCOMPARE(session.undoName(), QString("Delete Page"));
    QVERIFY(!menus.action("deletePage")->isEnabled());
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

void MenusTests::exportArtboardIsACheckedToggleOnTheActiveArtboard()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    session.addArtboard(QRectF(300, 0, 200, 200));
    QAction *toggle = menus.action("artboardExported");
    QVERIFY(toggle->isCheckable() && toggle->isChecked() && toggle->isEnabled());
    toggle->trigger();
    QVERIFY(!session.document()->artboard(1).exported);
    QVERIFY(session.document()->artboard(0).exported);
    QVERIFY(!toggle->isChecked());
    session.setActiveArtboard(0);
    QVERIFY(toggle->isChecked());
    session.setActiveArtboard(1);
    toggle->trigger();
    QVERIFY(session.document()->artboard(1).exported);
    QVERIFY(toggle->isChecked());
}

void MenusTests::windowHasTheTimelineAndMotionEntries()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QVERIFY(menus.action("showTimeline")->isCheckable());
    QVERIFY(menus.action("showMotion")->isCheckable());
    workspace.createDocument(QSizeF(400, 300));
    QVERIFY(menus.action("showTimeline")->isEnabled());
    MotionTimeline *timeline = MotionTimeline::of(workspace.current().session);
    QVERIFY(timeline);
    QVERIFY(!timeline->isOpen());
    // With no Browser View there is nothing to open, and it says so instead of failing quietly.
    QSignalSpy notice(timeline, &MotionTimeline::notice);
    menus.action("showTimeline")->trigger();
    QCOMPARE(notice.size(), 1);
    QVERIFY(notice.first().first().toString().contains(QStringLiteral("no Browser View")));
    QVERIFY(!timeline->isOpen());
    QVERIFY(!menus.action("showTimeline")->isChecked());
    // Motion is the inspector's own panel.
    QVERIFY(!menus.action("showMotion")->isChecked());
    menus.action("showMotion")->trigger();
    QVERIFY(menus.action("showMotion")->isChecked());
    menus.action("showMotion")->trigger();
    QVERIFY(!menus.action("showMotion")->isChecked());
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

void MenusTests::undoInEditPageIsThePagesOwn()
{
    // The host outlives the window, which asks it about undo while it goes.
    PageHost host;
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(400, 400));
    EditorSession &session = workspace.current().session;
    box(session, 10);
    EditorCanvas &canvas = window.content()->canvas();
    canvas.setBrowserViewHost(&host);
    const QUuid frame = session.addBrowserView({100, 100, 200, 200}, QUrl(QStringLiteral("https://example.com/")));
    QVERIFY(canvas.enterEditPage(frame));
    // Edit Page has its own history: the document's steps aren't offered, and Undo never makes one.
    QCOMPARE(menus.action("undo")->text(), QString("Undo Page Edit"));
    QVERIFY(menus.action("undo")->isEnabled());
    const QString before = session.undoName();
    menus.action("undo")->trigger();
    QCOMPARE(host.undone, 1);
    QCOMPARE(session.undoName(), before);
    host.undoable = false;
    canvas.noteEditPageHostChanged();
    QTRY_VERIFY(!menus.action("undo")->isEnabled());
    // Outside Edit Page, Ctrl+Z is the document's again.
    canvas.leaveEditPage();
    QTRY_COMPARE(menus.action("undo")->text(), QString("Undo ") + before);
    menus.action("undo")->trigger();
    QVERIFY(session.undoName() != before);
    QCOMPARE(host.undone, 1);
    canvas.setBrowserViewHost(nullptr);
}

void MenusTests::enterInAnElementFieldLeavesUndoToThePage()
{
    PageHost host;
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(400, 400));
    EditorSession &session = workspace.current().session;
    EditorCanvas &canvas = window.content()->canvas();
    canvas.setBrowserViewHost(&host);
    const QUuid frame = session.addBrowserView({100, 100, 200, 200}, QUrl(QStringLiteral("https://example.com/")));
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QVERIFY(canvas.enterEditPage(frame));
    const QJsonObject picked{{"selector", "#a"}, {"tag", "div"}, {"text", ""}, {"textOnly", false},
                             {"rect", QJsonObject{{"x", 10}, {"y", 10}, {"width", 80}, {"height", 40}}},
                             {"styles", QJsonObject{{"padding-left", "8px"}, {"padding-right", "8px"}, {"padding-top", "8px"}, {"padding-bottom", "8px"}}}};
    host.state.selection = QJsonArray{picked};
    host.boxes.selection.push_back({QRectF(10, 10, 80, 40), "div"});
    canvas.noteEditPageHostChanged();
    auto *bar = canvas.findChild<ElementBar *>();
    QVERIFY(bar);
    QTRY_VERIFY(bar->findChild<NumberField *>("elementPaddingX"));
    NumberField *padding = bar->findChild<NumberField *>("elementPaddingX");
    padding->field->setFocus(Qt::MouseFocusReason);
    QTRY_VERIFY(padding->field->hasFocus());
    // While the field has the keyboard, Undo is its own.
    QCOMPARE(menus.action("undo")->text(), QString("Undo"));
    QTest::keyClick(padding->field, Qt::Key_Return);
    QTRY_VERIFY(canvas.hasFocus());
    QTRY_COMPARE(menus.action("undo")->text(), QString("Undo Page Edit"));
    menus.action("undo")->trigger();
    QCOMPARE(host.undone, 1);
    canvas.setBrowserViewHost(nullptr);
}

void MenusTests::browserViewItemsFollowTheFrameEvenInEditPage()
{
    PageHost host;
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    workspace.createDocument(QSizeF(400, 400));
    EditorSession &session = workspace.current().session;
    EditorCanvas &canvas = window.content()->canvas();
    canvas.setBrowserViewHost(&host);
    const QStringList project = {"browserViewDeploy", "browserViewSave", "browserViewReviewChanges", "browserViewHistory", "browserViewBuildIt",
                                 "browserViewBuildItWithNote", "browserViewStopBuild", "browserViewStopLive", "browserViewKeepEdits",
                                 "browserViewShowOriginal", "browserViewExportCss", "browserViewThisIsMySite"};
    // No Browser View: nothing of them is on offer, so Ctrl+K doesn't list them as usable.
    session.deselectAll();
    QTRY_VERIFY(!menus.action("browserViewDeploy")->isEnabled());
    for (const QString &name : project)
        QVERIFY2(!menus.action(name)->isEnabled(), qPrintable(name));
    const QUuid frame = session.addBrowserView({100, 100, 200, 200}, QUrl(QStringLiteral("https://example.com/")));
    session.select({frame});
    QTRY_VERIFY(menus.action("browserViewDeploy")->isEnabled());
    QVERIFY(menus.action("browserViewBuildIt")->isEnabled());
    QVERIFY(menus.action("browserViewThisIsMySite")->isEnabled());
    // Nothing is building and Live isn't running on the frame: there is nothing to stop.
    QVERIFY(!menus.action("browserViewStopBuild")->isEnabled());
    QVERIFY(!menus.action("browserViewStopLive")->isEnabled());
    QVERIFY(!menus.action("browserViewKeepEdits")->isEnabled());
    // Edit Page deselects the document; the items act on its frame all the same.
    QVERIFY(canvas.enterEditPage(frame));
    QVERIFY(!session.selectedBrowserView().has_value());
    QTRY_VERIFY(menus.action("browserViewDeploy")->isEnabled());
    menus.action("browserViewDeploy")->trigger();
    menus.action("browserViewBuildIt")->trigger();
    QCOMPARE(host.acted.size(), 2);
    QCOMPARE(host.acted[0].first, frame);
    QCOMPARE(host.acted[0].second, BrowserViewHost::Action::deploy);
    QCOMPARE(host.acted[1].second, BrowserViewHost::Action::buildIt);
    canvas.setBrowserViewHost(nullptr);
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

void MenusTests::closingWithTheShortcutsPanelOpen()
{
    // The panel hides as the window goes, which moves focus while the menus are being destroyed.
    auto workspace = std::make_unique<ProjectWorkspace>();
    auto window = std::make_unique<ProjectWorkspaceView>(*workspace);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    window->menus()->action(QStringLiteral("keyboardShortcuts"))->trigger();
    QTRY_VERIFY(QApplication::activeWindow() && QApplication::activeWindow() != window.get());
    window.reset();
    workspace.reset();
}


namespace {
struct TypeWindow {
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    EditorSession &session() { return workspace.current().session; }
    EditorCanvas &canvas() { return window.content()->canvas(); }
    TypeWindow()
    {
        workspace.createDocument(QSizeF(400, 300));
        window.resize(1200, 800);
        window.show();
        if (!QTest::qWaitForWindowActive(&window))
            qWarning("the window never became active");
        canvas().setFocus();
    }
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::keyClick(&canvas(), key, modifiers); }
    size_t count()
    {
        size_t objects = 0;
        for (const VectorObject &object : session().document()->objects)
            objects += object.kind == ObjectKind::layer ? 0 : 1;
        return objects;
    }
};
}

void MenusTests::typeKeysStyleSelectedType()
{
    TypeWindow w;
    const QUuid id = w.session().addText(QPointF(40, 100), QStringLiteral("Tracked"));
    const auto text = [&] { return w.session().document()->find(id)->text; };
    QVERIFY(w.window.menus()->action("loosenTracking")->isEnabled());
    const QString before = w.session().undoName();
    for (int repeat = 0; repeat < 3; ++repeat)
        w.press(Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(text().tracking, 60.0);
    QCOMPARE(w.count(), size_t(1));
    QCOMPARE(w.session().undoName(), QString("Tracking"));
    w.session().undo();
    QCOMPARE(text().tracking, 0.0);
    QCOMPARE(w.session().undoName(), before);
    w.press(Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(text().tracking, -100.0);
    // Alt+Up tightens leading from Auto's value; Alt+Shift+Up raises the baseline.
    const double automatic = text().effectiveLeading();
    w.press(Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(text().leading, std::optional<double>(automatic - 2));
    w.press(Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(text().baselineShift, 2.0);
    const double size = text().size;
    w.press(Qt::Key_Greater, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(text().size, size + 2);
    w.press(Qt::Key_Less, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(text().size, size);
    w.press(Qt::Key_Q, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(text().tracking, 0.0);
    QCOMPARE(w.count(), size_t(1));
}

void MenusTests::altArrowsDuplicateAnythingElse()
{
    TypeWindow w;
    const QUuid text = w.session().addText(QPointF(40, 200), QStringLiteral("Words"));
    const QUuid shape = box(w.session(), 10);
    w.session().select({shape});
    QVERIFY(!w.window.menus()->action("loosenTracking")->isEnabled());
    w.press(Qt::Key_Right, Qt::AltModifier);
    // A copy, nudged; the type is untouched.
    QCOMPARE(w.count(), size_t(3));
    QCOMPARE(w.session().document()->find(text)->text.tracking, 0.0);
    // Type and a shape together: not all type, so it's a copy too.
    w.session().select({text, shape});
    w.press(Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(w.count(), size_t(5));
}

void MenusTests::typeKeysKernAtACaretWhileTyping()
{
    TypeWindow w;
    const QUuid id = w.session().addText(QPointF(100, 100), QStringLiteral("AVA"));
    w.session().selectTool(Tool::text);
    const QRectF bounds = w.session().document()->bounds(id);
    const QPoint at = w.session().viewport.viewPoint(QPointF(bounds.left() + 0.5, bounds.center().y()), w.session().document()->size).toPoint();
    QTest::mouseClick(&w.canvas(), Qt::LeftButton, Qt::NoModifier, at);
    QVERIFY(w.canvas().isEditingText());
    QVERIFY(w.window.menus()->action("loosenTracking")->isEnabled());
    // The caret between A and V: Alt+Left kerns that pair.
    w.press(Qt::Key_Right);
    w.press(Qt::Key_Left, Qt::AltModifier);
    w.press(Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(w.session().document()->find(id)->text.kerns.at(1), -40.0);
    QCOMPARE(w.session().document()->find(id)->text.tracking, 0.0);
    QCOMPARE(w.session().undoName(), QString("Kerning"));
    QVERIFY(w.canvas().isEditingText());
    // With a range selected, it's tracking, on the range alone.
    w.press(Qt::Key_Right, Qt::ShiftModifier);
    w.press(Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(w.session().document()->find(id)->text.formatAt(1).tracking, 20.0);
    QCOMPARE(w.session().document()->find(id)->text.tracking, 0.0);
    // Typing carries on with the kern in place.
    w.press(Qt::Key_End);
    QTest::keyClicks(&w.canvas(), QStringLiteral("!"));
    w.canvas().finishTextEditing();
    QCOMPARE(w.session().document()->find(id)->text.text, QString("AVA!"));
    QCOMPARE(w.session().document()->find(id)->text.kerns.at(1), -40.0);
}

void MenusTests::typeMenuConvertsPointAndArea()
{
    TypeWindow w;
    const QUuid id = w.session().addText(QPointF(40, 100), QStringLiteral("Point to area"));
    Menus &menus = *w.window.menus();
    QVERIFY(menus.action("convertToAreaType")->isEnabled());
    QVERIFY(!menus.action("convertToPointType")->isEnabled());
    menus.action("convertToAreaType")->trigger();
    QVERIFY(w.session().document()->find(id)->text.area.has_value());
    QVERIFY(!menus.action("convertToAreaType")->isEnabled());
    menus.action("convertToPointType")->trigger();
    QVERIFY(!w.session().document()->find(id)->text.area.has_value());
    QCOMPARE(w.session().document()->find(id)->text.text, QString("Point to area"));
}

// Shift+A was a canvas key: it did nothing once a panel held focus, and its entry showed no key.
void MenusTests::figmaKeysWorkFromAnyPanel()
{
    TypeWindow w;
    const QUuid one = box(w.session(), 10), two = box(w.session(), 100);
    w.session().select({one, two});
    Menus &menus = *w.window.menus();
    QCOMPARE(menus.action("addAutoLayout")->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_A));
    // A button in a panel holds focus, as after a click on a panel's control.
    auto *button = new QPushButton(QStringLiteral("Focus"), &w.window.content()->layersPanel());
    button->show();
    button->setFocus();
    QVERIFY(QTest::qWaitFor([&] { return QApplication::focusWidget() == button; }));
    // A text field keeps its capital A.
    auto *field = new QLineEdit(&w.window.content()->layersPanel());
    field->show();
    field->setFocus();
    QTest::keyClick(field, 'A', Qt::ShiftModifier);
    QCOMPARE(field->text(), QString("A"));
    QCOMPARE(w.count(), size_t(2));
    button->setFocus();
    QTest::keyClick(button, Qt::Key_A, Qt::ShiftModifier);
    QCOMPARE(w.count(), size_t(3));
    QVERIFY(w.session().canRemoveAutoLayout());
    QTest::keyClick(button, Qt::Key_A, Qt::AltModifier | Qt::ShiftModifier);
    QVERIFY(!w.session().canRemoveAutoLayout());
    // Tool letters reach the canvas from the button too, and not from the field.
    QTest::keyClick(button, Qt::Key_P);
    QCOMPARE(w.session().tool(), Tool::pen);
    field->setFocus();
    QTest::keyClick(field, Qt::Key_V);
    QCOMPARE(w.session().tool(), Tool::pen);
    QCOMPARE(field->text(), QString("Av"));
    button->setFocus();
    QTest::keyClick(button, Qt::Key_V);
    QCOMPARE(w.session().tool(), Tool::select);
    // So do the arrows, from a plain button.
    const QRectF before = w.session().selectionBounds();
    QTest::keyClick(button, Qt::Key_Right);
    QCOMPARE(w.session().selectionBounds().left(), before.left() + 1);
}

void MenusTests::figmaKeysSitBesideIllustratorsOnTheirEntries()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QCOMPARE(menus.action("flipHorizontal")->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_H));
    QCOMPARE(menus.action("removeAutoLayout")->shortcut(), QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_A));
    QCOMPARE(menus.action("lockSelection")->shortcuts(), (QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_2), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)}));
    QCOMPARE(menus.action("actualSize")->shortcuts().last(), QKeySequence(Qt::SHIFT | Qt::Key_0));
    // The sheet is under Help now, with Figma's key.
    QCOMPARE(menus.action("keyboardShortcuts")->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Question));
    // Remapping the entry moves its Figma key with it, and the fixed second key stays.
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Menus:Lock Selection"), ShortcutChord("l", 3)}}));
    QCOMPARE(menus.action("lockSelection")->shortcuts().first(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_L));
    QCOMPARE(menus.action("lockSelection")->shortcuts().last(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
}

void MenusTests::lockDocumentTogglesAndClosesTheEditingEntries()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    Menus &menus = *window.menus();
    QVERIFY(!menus.action("lockDocument")->isEnabled());
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    box(session, 10);
    // Ctrl+K is the command palette; Figma's lock key with Alt is this one.
    QCOMPARE(menus.action("lockDocument")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_L));
    QCOMPARE(menus.action("lockDocument")->text(), QString("Lock Document"));
    QVERIFY(menus.action("lockDocument")->isEnabled());
    QVERIFY(menus.action("group")->isEnabled() || menus.action("bringToFront")->isEnabled());
    menus.action("lockDocument")->trigger();
    QVERIFY(session.isDocumentLocked());
    QCOMPARE(menus.action("lockDocument")->text(), QString("Unlock Document"));
    for (const QString name : {"cut", "paste", "pasteInPlace", "pasteInFront", "pasteInBack", "place", "bringToFront", "undo", "redo"})
        QVERIFY2(!menus.action(name)->isEnabled(), qPrintable(name));
    // Looking, keeping and sending it out stay open, and so does the way back.
    for (const QString name : {"copy", "save", "saveAs", "exportPNG", "exportSVG", "exportPDF", "selectAll", "zoomIn", "lockDocument"})
        QVERIFY2(menus.action(name)->isEnabled(), qPrintable(name));
    menus.action("lockDocument")->trigger();
    QVERIFY(!session.isDocumentLocked());
    QVERIFY(menus.action("bringToFront")->isEnabled());
    QCOMPARE(menus.action("lockDocument")->text(), QString("Lock Document"));
}

void MenusTests::aLockedDocumentShowsItsLockAndSaysWhyOnARefusedEdit()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    workspace.createDocument(QSizeF(200, 200));
    EditorSession &session = workspace.current().session;
    const QUuid id = box(session, 10);
    ProjectTabStrip *strip = window.findChild<ProjectTabStrip *>();
    QVERIFY(strip);
    QVERIFY(strip->buttons().first()->findChild<QToolButton *>()->icon().isNull());
    session.setDocumentLocked(true);
    QToolButton *select = strip->buttons().first()->findChild<QToolButton *>();
    QVERIFY(!select->icon().isNull());
    QVERIFY(select->toolTip().contains(QStringLiteral("Unlock Document")));
    session.rename(id, QStringLiteral("Nope"));
    QCOMPARE(session.document()->find(id)->name, QStringLiteral("Box"));
    QVERIFY(workspace.cloudStatusText().contains(QStringLiteral("locked")));
    // Placing a file would edit the tab, so it says no instead of reporting success.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString picture = directory.filePath(QStringLiteral("locked.png"));
    QImage image(20, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(picture));
    const auto objects = session.document()->objects.size();
    const QString undoName = session.undoName();
    QVERIFY(!workspace.placeFile(picture));
    QCOMPARE(session.document()->objects.size(), objects);
    QCOMPARE(session.undoName(), undoName);
    session.setDocumentLocked(false);
    QVERIFY(strip->buttons().first()->findChild<QToolButton *>()->icon().isNull());
    QVERIFY(workspace.placeFile(picture));
    QCOMPARE(session.document()->objects.size(), objects + 1);
}

QTEST_MAIN(MenusTests)
#include "MenusTests.moc"
