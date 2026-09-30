#include "Agent/AgentProtocol.h"
#include "Agent/AgentTools.h"
#include "Agent/WorkspaceClaims.h"
#include "FakeHyprlandWorld.h"
#include "TemporaryConfig.h"
#include "UI/AgentBridge.h"
#include "UI/ProjectWorkspaceView.h"
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Hyprland to the app and back (docs/WORKSPACES.md, "Hyprland → app"): the event stream is a real local socket.
namespace {
struct Rig {
    Rig()
    {
        qputenv("OMASTRATOR_HYPRCTL", ctl.path().toUtf8());
        PageWorkspaces::forgetReachability();
        workspace.createDocument(QSizeF(100, 100));
        view = std::make_unique<ProjectWorkspaceView>(workspace);
        view->show();
        world.addEditor(*view);
        view->pageWorkspaces()->setFocusProbe([this] { return focus; });
        PageWorkspaces::setTurnedOn(true);
    }
    ~Rig() { QSettings().remove(QStringLiteral("view/pageWorkspaces")); }

    EditorSession &session() { return workspace.current().session; }
    PageWorkspaces &pages() { return *view->pageWorkspaces(); }
    QUuid page(int index) { return session().document()->allPages()[size_t(index)].id; }
    QString name(int index) { return pages().nameOf(workspace.selectedID(), page(index)); }
    QString editor() { return world.windows().front().address; }
    int count(const QString &pattern)
    {
        const QRegularExpression expression(pattern);
        int found = 0;
        for (const QString &line : world.history())
            found += expression.match(line).hasMatch() ? 1 : 0;
        return found;
    }

    // Whether an Omastrator window has focus: stand-ins are made, and the editor takes the user along, only then.
    bool focus = true;
    QTemporaryDir dir;
    FakeHyprctl ctl{dir.path()};
    FakeHyprlandWorld world{ctl};
    ProjectWorkspace workspace;
    std::unique_ptr<ProjectWorkspaceView> view;
};
}

class PageWorkspacesSyncTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void aWorkspaceEventMakesThePageCurrentAndSwaps();
    void aBurstOfThreeActsOnTheLast();
    void aNumberedWorkspaceDoesNothing();
    void ourOwnEchoDispatchesNothing();
    void theAgentsPageAddMovesSilently();
    void aClosedStandInStaysClosedUntilTheUserNavigatesThere();
    void theEditorIsFocusedOnlyWithFocusAndOnlyWhereTheyStillAre();
    void aBackgroundTabIsSelectedFromItsWorkspace();
    void otherWindowsEventsDoNotTriggerPlacement();
    void aDotClickFollowsTheUserInOneBatchWithoutFocus();
    void aDotClickOnTheCurrentPageMovesNothing();
    void aDotClickForAnUnknownWorkspaceIsRefused();
    void theCanvasPaintsTheNewPageBeforeTheEditorMovesIn();

private:
    // The stream's end that Hyprland writes to; waits for the app to connect.
    QLocalSocket *stream()
    {
        if (!m_server.hasPendingConnections())
            m_server.waitForNewConnection(2000);
        // The newest is this rig's; older ones belong to windows already gone.
        while (m_server.hasPendingConnections()) {
            delete m_stream;
            m_stream = m_server.nextPendingConnection();
        }
        return m_stream;
    }
    void heardLine(const QString &line)
    {
        QVERIFY(stream());
        m_stream->write((line + QLatin1Char('\n')).toUtf8());
        m_stream->flush();
    }
    void heard(Rig &rig, const QString &workspace)
    {
        QVERIFY(stream());
        m_stream->write(QStringLiteral("workspacev2>>-99,%1\n").arg(workspace).toUtf8());
        m_stream->flush();
        Q_UNUSED(rig)
    }
    QTemporaryDir m_runtime;
    QTemporaryDir m_sockets;
    QLocalServer m_server;
    QPointer<QLocalSocket> m_stream;
};

void PageWorkspacesSyncTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
    qputenv("OMASTRATOR_RUNTIME_DIR", m_runtime.path().toUtf8());
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    const QString path = m_sockets.filePath(QStringLiteral("events.sock"));
    QVERIFY(m_server.listen(path));
    qputenv("OMASTRATOR_HYPRLAND_EVENTS", path.toUtf8());
}

void PageWorkspacesSyncTests::init()
{
    QSettings().remove(QStringLiteral("view/pageWorkspaces"));
    WorkspaceClaims::write({});
}

void PageWorkspacesSyncTests::cleanup()
{
    delete m_stream;
}

void PageWorkspacesSyncTests::aWorkspaceEventMakesThePageCurrentAndSwaps()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    // The new page has the editor; the first page has a stand-in.
    QCOMPARE(rig.session().currentPage(), rig.page(1));
    const QString first = rig.name(0), second = rig.name(1);
    QCOMPARE(rig.world.workspaceOf(rig.editor()), second);
    QVERIFY(!rig.world.stand(first).isEmpty());

    // Super+Tab to the first page's workspace, from a window that isn't ours: the stand-in is what's there.
    rig.focus = false;
    rig.world.go(first);
    heard(rig, first);
    rig.world.clearHistory();
    rig.world.settle();
    QCOMPARE(rig.session().currentPage(), rig.page(0));
    QCOMPARE(rig.world.workspaceOf(rig.editor()), first);
    QVERIFY(!rig.world.stand(second).isEmpty());
    QCOMPARE(rig.world.active(), first);
    // The app didn't hold focus, so nothing was focused and the user was never moved.
    QCOMPARE(rig.count(QStringLiteral("focuswindow")), 0);
    QCOMPARE(rig.count(QStringLiteral("^dispatch workspace ")), 0);
    QCOMPARE(rig.count(QStringLiteral("^dispatch movetoworkspace name:")), 0);
}

void PageWorkspacesSyncTests::aBurstOfThreeActsOnTheLast()
{
    Rig rig;
    rig.session().addPage();
    rig.session().addPage();
    rig.world.settle();
    QCOMPARE(rig.session().currentPage(), rig.page(2));
    const QString a = rig.name(0), b = rig.name(1), c = rig.name(2);
    rig.world.clearHistory();
    rig.world.go(a);
    heard(rig, a);
    heard(rig, b);
    heard(rig, a);
    QTest::qWait(10);
    rig.world.go(b);
    heard(rig, b);
    rig.world.settle();
    QCOMPARE(rig.session().currentPage(), rig.page(1));
    QCOMPARE(rig.world.workspaceOf(rig.editor()), b);
    QVERIFY(!rig.world.stand(c).isEmpty());
    QVERIFY(!rig.world.stand(a).isEmpty());
    // One editor move for the whole run.
    QCOMPARE(rig.count(QStringLiteral("^dispatch movetoworkspace(silent)? name:.*,address:%1$").arg(rig.editor())), 1);
}

void PageWorkspacesSyncTests::aNumberedWorkspaceDoesNothing()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    rig.world.clearHistory();
    rig.world.go(QStringLiteral("3"));
    heard(rig, QStringLiteral("3"));
    rig.world.settle(3);
    QCOMPARE(rig.session().currentPage(), rig.page(1));
    QVERIFY(rig.world.history().isEmpty());
    QCOMPARE(rig.world.active(), QStringLiteral("3"));
}

void PageWorkspacesSyncTests::otherWindowsEventsDoNotTriggerPlacement()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    const QString stand = rig.world.stand(rig.name(0)).mid(2);
    auto lookedAtClients = [&](const QString &line) {
        rig.ctl.clearLog();
        heardLine(line);
        QTest::qWait(300);
        return rig.ctl.log().contains(QStringLiteral("-j clients"));
    };
    // A browser opening, moving and closing, on a workspace we claim and off it.
    QVERIFY(!lookedAtClients(QStringLiteral("openwindow>>beef1,3,firefox,Firefox")));
    QVERIFY(!lookedAtClients(QStringLiteral("movewindowv2>>beef1,4,4")));
    QVERIFY(!lookedAtClients(QStringLiteral("closewindow>>beef1")));
    // Ours, and a window claiming to be a stand-in, are looked at; so is something moved onto one of our workspaces.
    QVERIFY(lookedAtClients(QStringLiteral("closewindow>>") + stand));
    QVERIFY(lookedAtClients(QStringLiteral("openwindow>>beef2,3,io.github.iretonsean.Omastrator,omastrator-standin-9")));
    QVERIFY(lookedAtClients(QStringLiteral("movewindowv2>>beef3,-1337,") + rig.name(0)));
}

void PageWorkspacesSyncTests::ourOwnEchoDispatchesNothing()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    // Focused, the app takes the user to the page it just showed; Hyprland reports that arrival back.
    rig.focus = true;
    rig.session().setCurrentPage(rig.page(0));
    rig.world.settle();
    QCOMPARE(rig.world.active(), rig.name(0));
    rig.world.clearHistory();
    heard(rig, rig.name(0));
    rig.world.settle(3);
    QVERIFY(rig.world.history().isEmpty());
    QCOMPARE(rig.session().currentPage(), rig.page(0));
}

void PageWorkspacesSyncTests::theAgentsPageAddMovesSilently()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    // The user is in another window while the agent works.
    rig.focus = false;
    rig.world.go(QStringLiteral("3"));
    const QString before = rig.world.active();
    rig.world.clearHistory();
    rig.view->agent()->tools().call(QStringLiteral("page"), {{"action", "add"}});
    rig.world.settle();
    QCOMPARE(rig.session().document()->allPages().size(), size_t(3));
    QCOMPARE(rig.world.workspaceOf(rig.editor()), rig.name(2));
    QCOMPARE(rig.world.active(), before);
    QCOMPARE(rig.count(QStringLiteral("^dispatch movetoworkspace name:")), 0);
    QCOMPARE(rig.count(QStringLiteral("^dispatch workspace ")), 0);
    QCOMPARE(rig.count(QStringLiteral("focuswindow")), 0);
}

void PageWorkspacesSyncTests::aClosedStandInStaysClosedUntilTheUserNavigatesThere()
{
    Rig rig;
    rig.session().addPage();
    rig.session().addPage();
    rig.world.settle();
    const QString a = rig.name(0), b = rig.name(1), c = rig.name(2);
    // Super+W on the first page's stand-in.
    QVERIFY(!rig.world.stand(a).isEmpty());
    auto *standIn = qobject_cast<PageStandIn *>(rig.world.window(rig.world.stand(a))->owner);
    QVERIFY(standIn);
    standIn->close();
    rig.world.settle(4);
    QVERIFY(rig.world.stand(a).isEmpty());
    QVERIFY(!rig.pages().claimedNames().contains(a));
    // b's stand-in and the spare.
    QCOMPARE(rig.pages().standInCount(), 2);
    // Nothing reopens it while the user works elsewhere.
    rig.session().setCurrentPage(rig.page(1));
    rig.world.settle(4);
    QVERIFY(rig.world.stand(a).isEmpty());
    QVERIFY(!rig.world.stand(c).isEmpty());
    QCOMPARE(rig.world.workspaceOf(rig.editor()), b);
    // Going to the page again from the Pages list claims it back.
    rig.session().setCurrentPage(rig.page(0));
    rig.world.settle(4);
    QCOMPARE(rig.world.workspaceOf(rig.editor()), a);
    QVERIFY(rig.pages().claimedNames().contains(a));
    QVERIFY(!rig.world.stand(b).isEmpty());
}

void PageWorkspacesSyncTests::theEditorIsFocusedOnlyWithFocusAndOnlyWhereTheyStillAre()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    const QString first = rig.name(0);
    rig.focus = false;
    rig.world.go(first);
    rig.focus = true;
    heard(rig, first);
    rig.world.clearHistory();
    rig.world.settle(3);
    QCOMPARE(rig.session().currentPage(), rig.page(0));
    QCOMPARE(rig.count(QStringLiteral("^dispatch focuswindow address:%1$").arg(rig.editor())), 1);
    // The user stays where they are: no `workspace` dispatch takes them anywhere.
    QCOMPARE(rig.world.active(), first);
    QCOMPARE(rig.count(QStringLiteral("^dispatch workspace ")), 0);

    // They have already left by the time the burst settles: the page follows the last landing, not the keyboard.
    rig.world.clearHistory();
    const QString second = rig.name(1);
    rig.world.go(second);
    heard(rig, second);
    rig.world.go(QStringLiteral("4"));
    rig.world.settle(3);
    QCOMPARE(rig.count(QStringLiteral("focuswindow")), 0);
    QCOMPARE(rig.world.active(), QStringLiteral("4"));
}

void PageWorkspacesSyncTests::aBackgroundTabIsSelectedFromItsWorkspace()
{
    Rig rig;
    rig.session().addPage();
    const QUuid first = rig.workspace.selectedID();
    rig.workspace.addTab(false, QStringLiteral("Second"));
    rig.session().addPage();
    rig.world.settle();
    QVERIFY(rig.workspace.selectedID() != first);
    const std::shared_ptr<ProjectTab> firstTab = rig.workspace.tab(first);
    const QString there = rig.pages().nameOf(first, firstTab->session.document()->allPages().front().id);
    QVERIFY(!there.isEmpty());
    QVERIFY(!rig.world.stand(there).isEmpty());

    rig.world.go(there);
    heard(rig, there);
    rig.world.settle();
    QCOMPARE(rig.workspace.selectedID(), first);
    QCOMPARE(firstTab->session.currentPage(), firstTab->session.document()->allPages().front().id);
    QCOMPARE(rig.world.workspaceOf(rig.editor()), there);
}

// The bar's page dots go through the app (`go_to_page`): the editor and the user arrive together, as with Alt+PageDown.
void PageWorkspacesSyncTests::aDotClickFollowsTheUserInOneBatchWithoutFocus()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    const QString first = rig.name(0), second = rig.name(1);
    QCOMPARE(rig.world.workspaceOf(rig.editor()), second);
    // The user is in a browser: no Omastrator window has focus.
    rig.focus = false;
    rig.world.go(QStringLiteral("3"));
    rig.world.clearHistory();
    rig.ctl.clearLog();
    rig.view->agent()->tools().call(QStringLiteral("go_to_page"), {{"workspace", first}});
    // Straight away, not on a later turn: one call to Hyprland holds every move.
    QCOMPARE(rig.ctl.dispatches().size(), 1);
    rig.world.settle();
    QCOMPARE(rig.world.batches().size(), 1);
    QCOMPARE(rig.session().currentPage(), rig.page(0));
    QCOMPARE(rig.world.workspaceOf(rig.editor()), first);
    QVERIFY(!rig.world.stand(second).isEmpty());
    // The user went with the editor, and no stand-in was ever what they saw: the editor's move is the one that follows.
    QCOMPARE(rig.world.active(), first);
    QCOMPARE(rig.count(QStringLiteral("^dispatch movetoworkspace name:.*,address:%1$").arg(rig.editor())), 1);
    QCOMPARE(rig.count(QStringLiteral("^dispatch workspace ")), 0);
    // Hyprland's report of that arrival changes nothing.
    rig.world.clearHistory();
    heard(rig, first);
    rig.world.settle(3);
    QVERIFY(rig.world.history().isEmpty());
    QCOMPARE(rig.session().currentPage(), rig.page(0));
}

void PageWorkspacesSyncTests::aDotClickOnTheCurrentPageMovesNothing()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    const QString current = rig.name(1);
    // Standing on it already: nothing at all.
    rig.world.go(current);
    rig.world.clearHistory();
    rig.view->agent()->tools().call(QStringLiteral("go_to_page"), {{"workspace", current}});
    rig.world.settle(3);
    QVERIFY(rig.world.batches().isEmpty());
    // Elsewhere: only the user goes, and no window moves.
    rig.world.go(QStringLiteral("3"));
    rig.world.clearHistory();
    rig.view->agent()->tools().call(QStringLiteral("go_to_page"), {{"workspace", current}});
    rig.world.settle(3);
    QCOMPARE(rig.world.active(), current);
    QCOMPARE(rig.count(QStringLiteral("movetoworkspace")), 0);
    QCOMPARE(rig.session().currentPage(), rig.page(1));
}

void PageWorkspacesSyncTests::aDotClickForAnUnknownWorkspaceIsRefused()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    rig.world.clearHistory();
    auto refused = [&](const QString &workspace) {
        try {
            rig.view->agent()->tools().call(QStringLiteral("go_to_page"), {{"workspace", workspace}});
        } catch (const AgentProtocol::Error &) {
            return true;
        }
        return false;
    };
    QVERIFY(refused(QStringLiteral("3")));
    QVERIFY(refused(QStringLiteral("design:Nothing · Here")));
    QVERIFY(rig.world.batches().isEmpty());
    // With the feature off there is nothing to go to: the dot's fallback focuses the workspace itself.
    const QString name = rig.name(0);
    PageWorkspaces::setTurnedOn(false);
    rig.world.settle(3);
    QVERIFY(refused(name));
}

// Step 3 of the flicker: after a Super+Tab the editor was still painted with the old page when it arrived.
void PageWorkspacesSyncTests::theCanvasPaintsTheNewPageBeforeTheEditorMovesIn()
{
    Rig rig;
    rig.session().addPage();
    rig.world.settle();
    const QString first = rig.name(0);
    // Every paint of the window is noted in the fake hyprctl's log, in order with the moves, with the page it drew.
    struct Watcher : QObject {
        std::function<bool(QObject *, QEvent *)> filter;
        bool eventFilter(QObject *watched, QEvent *event) override { return filter(watched, event); }
    } watcher;
    watcher.filter = [&](QObject *watched, QEvent *event) {
        if (event->type() == QEvent::Paint && watched->isWidgetType() && static_cast<QWidget *>(watched)->window() == rig.view.get())
            rig.ctl.note(QStringLiteral("paint ") + rig.session().currentPage().toString());
        return false;
    };
    qApp->installEventFilter(&watcher);
    rig.focus = false;
    rig.world.go(first);
    rig.ctl.clearLog();
    heard(rig, first);
    // The world clears the log each time it steps, so read it before that.
    QTest::qWait(200);
    const QStringList log = rig.ctl.log();
    qApp->removeEventFilter(&watcher);
    rig.world.settle();
    QCOMPARE(rig.session().currentPage(), rig.page(0));
    const QString drawn = QStringLiteral("# paint ") + rig.page(0).toString();
    const QRegularExpression editorMove(QStringLiteral("movetoworkspace(silent)? name:.*,address:%1").arg(rig.editor()));
    const qsizetype paint = log.indexOf(drawn);
    qsizetype move = -1;
    for (qsizetype i = 0; i < log.size() && move < 0; ++i) {
        if (editorMove.match(log[i]).hasMatch())
            move = i;
    }
    QVERIFY2(move >= 0, qPrintable(log.join(QLatin1Char('\n'))));
    QVERIFY2(paint >= 0 && paint < move, qPrintable(log.join(QLatin1Char('\n'))));
}

QTEST_MAIN(PageWorkspacesSyncTests)
#include "PageWorkspacesSyncTests.moc"
