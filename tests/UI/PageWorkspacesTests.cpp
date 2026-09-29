#include "Agent/WorkspaceClaims.h"
#include "FakeHyprlandWorld.h"
#include "TemporaryConfig.h"
#include "UI/ProjectWorkspaceView.h"
#include <QDir>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Pages as Workspaces (docs/WORKSPACES.md): claims and give back across the lifecycle, against a fake Hyprland.
namespace {
const QString prefix = QStringLiteral("design:");

QString ws(const QString &document, const QString &page)
{
    return prefix + document + QStringLiteral(" · ") + page;
}

// A window on a fake desktop, with the pages feature switched on the way the View menu does.
struct Rig {
    explicit Rig(bool onHyprland = true)
    {
        if (onHyprland)
            qputenv("OMASTRATOR_HYPRCTL", ctl.path().toUtf8());
        else
            qunsetenv("OMASTRATOR_HYPRCTL");
        PageWorkspaces::forgetReachability();
        workspace.createDocument(QSizeF(100, 100));
        view = std::make_unique<ProjectWorkspaceView>(workspace);
        view->show();
        world.addEditor(*view);
        view->pageWorkspaces()->setFocusProbe([this] { return focus; });
    }
    ~Rig() { QSettings().remove(QStringLiteral("view/pageWorkspaces")); }

    EditorSession &session() { return workspace.current().session; }
    PageWorkspaces &pages() { return *view->pageWorkspaces(); }
    void toggle() { view->menus()->action(QStringLiteral("pageWorkspaces"))->trigger(); }
    QUuid page(int index) { return session().document()->allPages()[size_t(index)].id; }
    QString editor() { return world.windows().front().address; }
    QString editorWorkspace() { return world.workspaceOf(editor()); }

    // Whether an Omastrator window has focus: stand-ins are made, and the editor takes the user along, only then.
    bool focus = true;
    QTemporaryDir dir;
    FakeHyprctl ctl{dir.path()};
    FakeHyprlandWorld world{ctl};
    ProjectWorkspace workspace;
    std::unique_ptr<ProjectWorkspaceView> view;
};
}

class PageWorkspacesTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void namesAreCleanedAndCut();
    void offHyprlandTheCommandIsGreyed();
    void aRefusingHyprlandGreysItToo();
    void aOnePageDocumentClaimsNothing();
    void theSecondPageSpreadsTheDocumentOut();
    void aStandInMapsUnderItsFirstTitleAndIsLabelledOnceFound();
    void switchingPagesSwapsTheEditorAndAStandIn();
    void turningOffGivesEverythingBack();
    void downToOnePageGivesBackAndUndoClaimsAgain();
    void renamingAPageMovesItsWorkspace();
    void theUsersWindowsGoBackToTheReturnWorkspace();
    void aNumberedReturnWorkspaceComesBackByNumberEvenAfterItWasDeleted();
    void theEditorGoesBackToItsOwnWorkspaceAndTheUsersWindowsToTheFocusedOne();
    void saveAsRenamesAndSameNamesGetSuffixes();
    void aPercentSignInANameIsNotAPlaceholder();
    void longNamesThatClashGetSuffixesAndForeignOnesToo();
    void closingTheDocumentGivesItsWorkspacesBack();
    void hidingTheWindowGivesBackAndQuittingToo();
    void twentyFourWorkspacesAtMost();
    void theEditorFollowsTheUserOnlyWithFocus();
    void aSwitchAskedWithFocusButPlacedWithoutItLeavesTheUserAlone();
    void noStandInIsShownWhileNoWindowOfOursHasFocus();
    void everyWorkspaceKeepsItsIdAcrossTenSwaps();
    void aRefusedMoveStopsItAndSaysSoOnce();
    void hyprlangAndLuaDispatchStrings();

private:
    QTemporaryDir m_runtime;
};

void PageWorkspacesTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
    qputenv("OMASTRATOR_RUNTIME_DIR", m_runtime.path().toUtf8());
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    qunsetenv("OMASTRATOR_HYPRLAND_EVENTS");
}

void PageWorkspacesTests::init()
{
    QSettings().remove(QStringLiteral("view/pageWorkspaces"));
    WorkspaceClaims::write({});
}

void PageWorkspacesTests::namesAreCleanedAndCut()
{
    QCOMPARE(PageWorkspaces::label(QStringLiteral("a,b \"c\" \\d")), QStringLiteral("ab c d"));
    QCOMPARE(PageWorkspaces::label(QStringLiteral("  two   spaces\tand\nlines ")), QStringLiteral("two spaces and lines"));
    QCOMPARE(PageWorkspaces::label(QString(40, QLatin1Char('x'))), QString(31, QLatin1Char('x')) + QChar(0x2026));
    QCOMPARE(PageWorkspaces::label(QString(32, QLatin1Char('x'))), QString(32, QLatin1Char('x')));
    QCOMPARE(PageWorkspaces::name(QStringLiteral("Poster"), QStringLiteral("Front")), ws(QStringLiteral("Poster"), QStringLiteral("Front")));
    QCOMPARE(PageWorkspaces::name(QStringLiteral("Q3, \"final\""), QStringLiteral("Page\\1")), ws(QStringLiteral("Q3 final"), QStringLiteral("Page1")));
    QCOMPARE(PageWorkspaces::name(QString(), QString()), ws(QStringLiteral("Untitled"), QStringLiteral("Page")));
    // Unicode stays.
    QCOMPARE(PageWorkspaces::name(QStringLiteral("Café"), QStringLiteral("日本語")), ws(QStringLiteral("Café"), QStringLiteral("日本語")));
}

void PageWorkspacesTests::offHyprlandTheCommandIsGreyed()
{
    Rig rig(false);
    QAction *action = rig.view->menus()->action(QStringLiteral("pageWorkspaces"));
    QVERIFY(action);
    QVERIFY(action->isCheckable());
    QVERIFY(!action->isEnabled());
    QCOMPARE(action->toolTip(), QStringLiteral("Needs Hyprland"));
    // The setting can still be on: nothing happens, and the app carries on with its one window.
    PageWorkspaces::setTurnedOn(true);
    rig.session().addPage();
    QTest::qWait(200);
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QVERIFY(rig.ctl.log().isEmpty());
}

void PageWorkspacesTests::aRefusingHyprlandGreysItToo()
{
    Rig rig;
    rig.ctl.setFailing(true);
    PageWorkspaces::forgetReachability();
    rig.session().addPage();
    QVERIFY(!rig.view->menus()->action(QStringLiteral("pageWorkspaces"))->isEnabled());
    rig.ctl.setFailing(false);
    PageWorkspaces::forgetReachability();
    rig.session().addPage();
    QVERIFY(rig.view->menus()->action(QStringLiteral("pageWorkspaces"))->isEnabled());
}

void PageWorkspacesTests::aOnePageDocumentClaimsNothing()
{
    Rig rig;
    rig.toggle();
    QVERIFY(PageWorkspaces::isTurnedOn());
    rig.world.settle(3);
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QVERIFY(rig.world.history().isEmpty());
    QVERIFY(WorkspaceClaims::read().isEmpty());
}

void PageWorkspacesTests::theSecondPageSpreadsTheDocumentOut()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1")), second = ws(QStringLiteral("Untitled"), QStringLiteral("Page 2"));
    QCOMPARE(rig.pages().claimedNames(), (QStringList{first, second}));
    // The new page is current: the editor is on its workspace, a stand-in holds the first page's.
    QCOMPARE(rig.editorWorkspace(), second);
    QCOMPARE(rig.world.on(first).size(), 1);
    QVERIFY(!rig.world.stand(first).isEmpty());
    // A stand-in for the first page and the spare, parked out of sight.
    QCOMPARE(rig.pages().standInCount(), 2);
    QCOMPARE(rig.world.on(QStringLiteral("special:omastrator-spare")).size(), 1);
    // An Omastrator window has focus, so the user is taken along with the editor.
    QCOMPARE(rig.world.active(), second);
    QVERIFY(rig.pages().standInAddresses().contains(rig.world.stand(first)));
    const WorkspaceClaims::State state = WorkspaceClaims::read();
    QCOMPARE(state.returnWorkspace, QStringLiteral("1"));
    QCOMPARE(state.pid, QCoreApplication::applicationPid());
    QCOMPARE(state.claims.size(), 2);
    QCOMPARE(state.claims[0].name, first);
    QCOMPARE(state.claims[0].windows, QStringList{rig.world.stand(first)});
    QCOMPARE(state.claims[1].windows, QStringList{rig.editor()});
    QCOMPARE(state.claims[1].pageId, rig.page(1).toString(QUuid::WithoutBraces));
}

void PageWorkspacesTests::aStandInMapsUnderItsFirstTitleAndIsLabelledOnceFound()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1"));
    const FakeHyprlandWorld::Window *standIn = rig.world.window(rig.world.stand(first));
    QVERIFY(standIn);
    // Mapped with the title the app finds it by, then labelled with its page.
    QVERIFY(standIn->mappedTitle.startsWith(QLatin1String("omastrator-standin-")));
    QCOMPARE(standIn->title, QStringLiteral("Untitled · Page 1 — Omastrator"));
}

void PageWorkspacesTests::switchingPagesSwapsTheEditorAndAStandIn()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1")), second = ws(QStringLiteral("Untitled"), QStringLiteral("Page 2"));
    rig.world.clearHistory();
    rig.session().setCurrentPage(rig.page(0));
    rig.world.settle();
    QCOMPARE(rig.editorWorkspace(), first);
    QCOMPARE(rig.world.on(second).size(), 1);
    QVERIFY(!rig.world.stand(second).isEmpty());
    QCOMPARE(rig.pages().standInCount(), 2);
    // The spare filled the old page's workspace, the editor took the new one, and the stand-in it replaced was parked.
    QCOMPARE(rig.world.history().size(), 3);
    QVERIFY(rig.world.history().at(0).contains(second));
    QVERIFY(rig.world.history().at(1).contains(first));
    QVERIFY(rig.world.history().at(2).contains(QStringLiteral("special:omastrator-spare")));
    QCOMPARE(rig.world.on(QStringLiteral("special:omastrator-spare")).size(), 1);
    // Settled: nothing more is dispatched.
    rig.world.clearHistory();
    rig.world.settle(3);
    QVERIFY(rig.world.history().isEmpty());
}

void PageWorkspacesTests::turningOffGivesEverythingBack()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    rig.toggle();
    QVERIFY(!PageWorkspaces::isTurnedOn());
    rig.world.settle();
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    QCOMPARE(rig.world.workspaces(), QStringList{QStringLiteral("1")});
    QVERIFY(WorkspaceClaims::read().isEmpty());
    // On again: it spreads out again.
    rig.toggle();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
}

void PageWorkspacesTests::downToOnePageGivesBackAndUndoClaimsAgain()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    const QUuid second = rig.page(1);
    QVERIFY(rig.session().deletePage(second));
    rig.world.settle();
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    QCOMPARE(rig.world.workspaces(), QStringList{QStringLiteral("1")});
    QVERIFY(WorkspaceClaims::read().isEmpty());
    rig.session().undo();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    QCOMPARE(rig.pages().standInCount(), 2);
    QVERIFY(rig.editorWorkspace().startsWith(prefix));
}

void PageWorkspacesTests::renamingAPageMovesItsWorkspace()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1")), second = ws(QStringLiteral("Untitled"), QStringLiteral("Page 2"));
    const QString foreign = rig.world.addForeign(first);
    // The editor's page.
    rig.session().renamePage(rig.page(1), QStringLiteral("Cover"));
    rig.world.settle();
    const QString cover = ws(QStringLiteral("Untitled"), QStringLiteral("Cover"));
    QCOMPARE(rig.editorWorkspace(), cover);
    QVERIFY(!rig.world.exists(second));
    // A stand-in's page, with the user's window on its workspace.
    rig.session().renamePage(rig.page(0), QStringLiteral("Back"));
    rig.world.settle();
    const QString back = ws(QStringLiteral("Untitled"), QStringLiteral("Back"));
    QVERIFY(!rig.world.exists(first));
    QVERIFY(!rig.world.stand(back).isEmpty());
    QCOMPARE(rig.world.workspaceOf(foreign), back);
    QCOMPARE(rig.pages().claimedNames(), (QStringList{back, cover}));
}

void PageWorkspacesTests::theUsersWindowsGoBackToTheReturnWorkspace()
{
    Rig rig;
    rig.world.addForeign(QStringLiteral("2"), QStringLiteral("Terminal"));
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1"));
    const QString dragged = rig.world.addForeign(first);
    rig.world.settle(2);
    // Hyprland leaves it there while the page is claimed.
    QCOMPARE(rig.world.workspaceOf(dragged), first);
    rig.toggle();
    rig.world.settle();
    QCOMPARE(rig.world.workspaceOf(dragged), QStringLiteral("1"));
    QCOMPARE(rig.world.workspaces().count(QStringLiteral("2")), 1);
    QVERIFY(!rig.world.exists(first));
}

void PageWorkspacesTests::aNumberedReturnWorkspaceComesBackByNumberEvenAfterItWasDeleted()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString first = ws(QStringLiteral("Untitled"), QStringLiteral("Page 1"));
    const QString dragged = rig.world.addForeign(first);
    // The user is on the editor's workspace now (it took them along), so workspace 1 is empty and gone.
    QCOMPARE(WorkspaceClaims::read().returnId, 1);
    QVERIFY(!rig.world.exists(QStringLiteral("1")));
    QCOMPARE(WorkspaceClaims::read().returnId, 1);
    rig.toggle();
    rig.world.settle();
    // Given back by number, so it's workspace 1 again and not a new one named "1".
    QCOMPARE(rig.world.idOf(QStringLiteral("1")), 1);
    QCOMPARE(rig.world.workspaceOf(dragged), QStringLiteral("1"));
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    QVERIFY(rig.world.rejected().isEmpty());
}

void PageWorkspacesTests::theEditorGoesBackToItsOwnWorkspaceAndTheUsersWindowsToTheFocusedOne()
{
    Rig rig;
    // The editor is on 1 while the user is looking at 2 (say, the window was opened from the launcher there).
    rig.world.go(QStringLiteral("2"));
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    const QString dragged = rig.world.addForeign(ws(QStringLiteral("Untitled"), QStringLiteral("Page 1")));
    rig.toggle();
    rig.world.settle();
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    QCOMPARE(rig.world.workspaceOf(dragged), QStringLiteral("2"));
}

void PageWorkspacesTests::saveAsRenamesAndSameNamesGetSuffixes()
{
    Rig rig;
    QTemporaryDir one, two;
    rig.session().addPage();
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().front(), one.filePath(QStringLiteral("poster.omai"))));
    rig.workspace.createDocument(QSizeF(50, 50));
    QVERIFY(rig.workspace.tabs().size() == 2);
    rig.session().addPage();
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().back(), two.filePath(QStringLiteral("poster.omai"))));
    rig.toggle();
    rig.world.settle();
    const QUuid firstTab = rig.workspace.tabs().front()->id, secondTab = rig.workspace.tabs().back()->id;
    const QUuid firstPage = rig.workspace.tabs().front()->session.document()->allPages().front().id;
    QCOMPARE(rig.pages().nameOf(firstTab, firstPage), ws(QStringLiteral("poster"), QStringLiteral("Page 1")));
    const QUuid otherPage = rig.workspace.tabs().back()->session.document()->allPages().front().id;
    QCOMPARE(rig.pages().nameOf(secondTab, otherPage), ws(QStringLiteral("poster (2)"), QStringLiteral("Page 1")));
    QCOMPARE(rig.pages().claimedNames().size(), 4);
    // Save As renames: the editor's document is the second one.
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().back(), two.filePath(QStringLiteral("flyer.omai"))));
    rig.world.settle();
    QCOMPARE(rig.pages().nameOf(secondTab, otherPage), ws(QStringLiteral("flyer"), QStringLiteral("Page 1")));
    QVERIFY(!rig.world.exists(ws(QStringLiteral("poster (2)"), QStringLiteral("Page 1"))));
    QVERIFY(rig.editorWorkspace().startsWith(ws(QStringLiteral("flyer"), QString())));
}

void PageWorkspacesTests::aPercentSignInANameIsNotAPlaceholder()
{
    Rig rig;
    QTemporaryDir one, two;
    rig.session().addPage();
    rig.session().renamePage(rig.page(0), QStringLiteral("A%2"));
    // The comma is dropped from a name, so these two clash without the session renaming either.
    rig.session().renamePage(rig.page(1), QStringLiteral("A%2,"));
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().front(), one.filePath(QStringLiteral("sale%2.omai"))));
    rig.workspace.createDocument(QSizeF(50, 50));
    rig.session().addPage();
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().back(), two.filePath(QStringLiteral("sale%2.omai"))));
    rig.toggle();
    rig.world.settle();
    const QUuid firstTab = rig.workspace.tabs().front()->id, secondTab = rig.workspace.tabs().back()->id;
    const auto &pages = rig.workspace.tabs().front()->session.document()->allPages();
    QCOMPARE(rig.pages().nameOf(firstTab, pages[0].id), ws(QStringLiteral("sale%2"), QStringLiteral("A%2")));
    QCOMPARE(rig.pages().nameOf(firstTab, pages[1].id), ws(QStringLiteral("sale%2"), QStringLiteral("A%2 (2)")));
    QCOMPARE(rig.pages().nameOf(secondTab, rig.workspace.tabs().back()->session.document()->allPages().front().id), ws(QStringLiteral("sale%2 (2)"), QStringLiteral("Page 1")));
}

void PageWorkspacesTests::longNamesThatClashGetSuffixesAndForeignOnesToo()
{
    Rig rig;
    rig.session().addPage();
    const QString stem(40, QLatin1Char('x'));
    rig.session().renamePage(rig.page(0), stem + QStringLiteral("A"));
    rig.session().renamePage(rig.page(1), stem + QStringLiteral("B"));
    // Someone's window is already on the workspace the first page would take.
    const QString cut = PageWorkspaces::label(stem + QStringLiteral("A"));
    rig.world.addForeign(ws(QStringLiteral("Untitled"), cut));
    rig.toggle();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames(), (QStringList{ws(QStringLiteral("Untitled"), cut + QStringLiteral(" (2)")), ws(QStringLiteral("Untitled"), cut + QStringLiteral(" (3)"))}));
}

void PageWorkspacesTests::closingTheDocumentGivesItsWorkspacesBack()
{
    Rig rig;
    QTemporaryDir directory;
    rig.session().addPage();
    QVERIFY(rig.workspace.saveTo(*rig.workspace.tabs().front(), directory.filePath(QStringLiteral("poster.omai"))));
    rig.workspace.createDocument(QSizeF(50, 50));
    rig.toggle();
    rig.world.settle();
    // The front document has one page, so it claims nothing; the other one does, with stand-ins for both pages.
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    QCOMPARE(rig.pages().standInCount(), 3);
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    rig.workspace.select(rig.workspace.tabs().front()->id);
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    QCOMPARE(rig.pages().standInCount(), 2);
    rig.workspace.close(rig.workspace.tabs().front()->id);
    QTRY_COMPARE(rig.workspace.tabs().size(), size_t(1));
    rig.world.settle();
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
    QVERIFY(WorkspaceClaims::read().isEmpty());
}

void PageWorkspacesTests::hidingTheWindowGivesBackAndQuittingToo()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    // The daemon's close only hides the window.
    rig.view->hide();
    rig.world.settle(3);
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QVERIFY(WorkspaceClaims::read().isEmpty());
    rig.view->show();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    // Quitting hands everything back before the windows go.
    const QString dragged = rig.world.addForeign(ws(QStringLiteral("Untitled"), QStringLiteral("Page 1")));
    rig.world.settle(2);
    rig.view.reset();
    rig.world.step();
    QCOMPARE(rig.world.workspaceOf(dragged), QStringLiteral("1"));
    QVERIFY(WorkspaceClaims::read().isEmpty());
}

void PageWorkspacesTests::twentyFourWorkspacesAtMost()
{
    Rig rig;
    for (int i = 0; i < 29; ++i)
        rig.session().addPage();
    rig.toggle();
    rig.world.settle(12);
    QCOMPARE(rig.pages().claimedNames().size(), PageWorkspaces::maxClaims);
    // Every claim but the editor's has a stand-in, and the spare makes one more.
    QCOMPARE(rig.pages().standInCount(), PageWorkspaces::maxClaims);
    QVERIFY(rig.workspace.cloudStatusText().contains(QStringLiteral("24")));
    // The current page (the last) has the editor though it comes last: it holds its place under the cap.
    QVERIFY(rig.pages().nameOf(rig.workspace.current().id, rig.session().currentPage()) == rig.editorWorkspace());
    QVERIFY(rig.editorWorkspace().startsWith(prefix));
}

void PageWorkspacesTests::theEditorFollowsTheUserOnlyWithFocus()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    // The user goes to a workspace of their own and an Omastrator window isn't in front.
    rig.world.go(QStringLiteral("1"));
    rig.focus = false;
    rig.session().setCurrentPage(rig.page(0));
    rig.world.settle();
    QCOMPARE(rig.world.active(), QStringLiteral("1"));
    QVERIFY(rig.editorWorkspace().startsWith(prefix));
    // Focused: the editor takes the user along.
    rig.focus = true;
    rig.session().setCurrentPage(rig.page(1));
    rig.world.settle();
    QCOMPARE(rig.world.active(), rig.editorWorkspace());
    QVERIFY(rig.editorWorkspace().startsWith(prefix));
    // Turning it off from the window brings them back with it.
    rig.toggle();
    rig.world.settle();
    QCOMPARE(rig.world.active(), QStringLiteral("1"));
    QCOMPARE(rig.editorWorkspace(), QStringLiteral("1"));
}

void PageWorkspacesTests::aSwitchAskedWithFocusButPlacedWithoutItLeavesTheUserAlone()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    rig.world.go(QStringLiteral("1"));
    // Asked while focused, placed after the user has already gone elsewhere.
    rig.session().setCurrentPage(rig.page(0));
    rig.focus = false;
    rig.world.settle();
    QCOMPARE(rig.world.active(), QStringLiteral("1"));
    // The stale request was dropped: a later placement doesn't drag the user in.
    rig.session().renamePage(rig.page(1), QStringLiteral("Back"));
    rig.world.settle();
    QCOMPARE(rig.world.active(), QStringLiteral("1"));
}

void PageWorkspacesTests::noStandInIsShownWhileNoWindowOfOursHasFocus()
{
    Rig rig;
    rig.focus = false;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    for (QWidget *widget : QApplication::topLevelWidgets())
        QVERIFY(!qobject_cast<PageStandIn *>(widget) || !widget->isVisible());
    QCOMPARE(rig.pages().standInCount(), 0);
    // The editor still took its page's workspace, silently.
    QVERIFY(rig.editorWorkspace().startsWith(prefix));
    QCOMPARE(rig.world.active(), QStringLiteral("1"));
    // Focus comes back: the stand-ins appear.
    rig.focus = true;
    emit qGuiApp->focusWindowChanged(nullptr);
    rig.world.settle();
    QCOMPARE(rig.pages().standInCount(), 2);
    QVERIFY(!rig.world.stand(ws(QStringLiteral("Untitled"), QStringLiteral("Page 1"))).isEmpty());
}

void PageWorkspacesTests::everyWorkspaceKeepsItsIdAcrossTenSwaps()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.session().addPage();
    rig.world.settle();
    const QStringList names = rig.pages().claimedNames();
    QCOMPARE(names.size(), 3);
    QHash<QString, int> ids;
    for (const QString &name : names)
        ids[name] = rig.world.idOf(name);
    for (int id : ids)
        QVERIFY(id != 0);
    rig.world.clearDeleted();
    for (int i = 0; i < 10; ++i) {
        rig.session().setCurrentPage(rig.page(i % 3));
        rig.world.settle(4);
    }
    for (const QString &name : names)
        QCOMPARE(rig.world.idOf(name), ids.value(name));
    for (const QString &name : rig.world.deleted())
        QVERIFY2(!name.startsWith(prefix), qPrintable(name));
    QCOMPARE(rig.world.on(QStringLiteral("special:omastrator-spare")).size(), 1);
}

void PageWorkspacesTests::aRefusedMoveStopsItAndSaysSoOnce()
{
    Rig rig;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    QCOMPARE(rig.pages().claimedNames().size(), 2);
    rig.ctl.setDispatchFailing(true);
    rig.session().addPage();
    rig.world.settle();
    QVERIFY(rig.pages().claimedNames().isEmpty());
    QCOMPARE(rig.pages().standInCount(), 0);
    QVERIFY(WorkspaceClaims::read().isEmpty());
    const QString notice = QStringLiteral("Pages as Workspaces stopped: Hyprland refused a move.");
    QCOMPARE(rig.workspace.cloudStatusText().count(notice), 1);
    // It stays stopped: no more moves are tried, even when Hyprland is willing again.
    rig.ctl.setDispatchFailing(false);
    rig.ctl.clearLog();
    rig.session().addPage();
    rig.world.settle();
    QVERIFY(rig.ctl.dispatches().isEmpty());
    QVERIFY(rig.pages().claimedNames().isEmpty());
}

void PageWorkspacesTests::hyprlangAndLuaDispatchStrings()
{
    Rig rig;
    // Without focus there are no stand-ins, so the editor's move is the first thing dispatched.
    rig.focus = false;
    rig.toggle();
    rig.session().addPage();
    rig.world.settle();
    // hyprlang: hyprctl dispatch, silent by default.
    QVERIFY(rig.world.history().first().startsWith(QStringLiteral("dispatch movetoworkspacesilent name:design:Untitled · Page ")));
    QVERIFY(rig.world.history().first().contains(QStringLiteral(",address:0x")));
    // Lua: hyprctl eval.
    const QString hypr = QDir(qEnvironmentVariable("XDG_CONFIG_HOME")).filePath(QStringLiteral("hypr"));
    QVERIFY(QDir().mkpath(hypr));
    QFile lua(hypr + QStringLiteral("/hyprland.lua"));
    QVERIFY(lua.open(QIODevice::WriteOnly));
    lua.close();
    rig.ctl.clearLog();
    rig.session().setCurrentPage(rig.page(0));
    QTRY_VERIFY(!rig.ctl.dispatches().isEmpty());
    for (const QString &line : rig.ctl.dispatches()) {
        QVERIFY2(line.startsWith(QStringLiteral("eval hl.dispatch(hl.dsp.window.move({ workspace = \"name:design:Untitled · Page ")), qPrintable(line));
        QVERIFY(line.contains(QStringLiteral("window = \"address:0x")));
    }
    QVERIFY(lua.remove());
}

QTEST_MAIN(PageWorkspacesTests)
#include "PageWorkspacesTests.moc"
