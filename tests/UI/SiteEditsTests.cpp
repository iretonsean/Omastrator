#include "Anywhere/AnywhereSettings.h"
#include "Anywhere/Desk.h"
#include "Anywhere/LiftDiff.h"
#include "Live/Browser.h"
#include "Live/EditSets.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/DesignController.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include "../Anywhere/FakeDesktop.h"
#include <QCheckBox>
#include <QDialog>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Change the real thing, widened (docs/ANYWHERE.md), in headless Chromium: a
// site that isn't registered takes real edits that are kept as edit sets,
// come back on a revisit, export as CSS and go to the Desk as Before and
// After, with Deploy hidden; Hand to Agent from a page, overlay art and a
// lifted app; lifted vectors of your own page write back to its code.
namespace {
// Stands in for `omarchy`: the default agent is sh; "the agent" records its task and edits the stylesheet where it runs.
constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT\"; pwd > \"$FAKE_OUT.cwd\";\n"
    "  printf '#title { color: var(--brand); }\\n' >> style.css; exit 0; fi\n"
    "exit 2\n";

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void executable(const QString &path, const QByteArray &body)
{
    write(path, body);
    QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QString git(const QString &folder, const QStringList &arguments)
{
    return WriteBack::git(folder, arguments);
}

// The plain fixture copied into `folder`, as a repository when `repository`.
void plainSite(const QString &folder, bool repository)
{
    QDirIterator files(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/plain"), QDir::Files);
    while (files.hasNext()) {
        const QString path = files.next();
        write(QDir(folder).filePath(QFileInfo(path).fileName()), read(path));
    }
    if (repository) {
        git(folder, {"init", "-q", "-b", "main"});
        git(folder, {"add", "-A"});
        git(folder, {"commit", "-q", "-m", "Plain site"});
    }
}

bool running(AgentBridge &bridge)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 60'000 && bridge.liveSession().state() == LiveSession::State::starting)
        QTest::qWait(50);
    return bridge.liveSession().state() == LiveSession::State::running;
}

QString computed(LiveSession &live, const char *selector, const char *property)
{
    return live.evaluate(QStringLiteral("getComputedStyle(document.querySelector('%1')).%2").arg(QLatin1String(selector), QLatin1String(property)))
        .toString();
}

QString textOf(LiveSession &live, const char *selector)
{
    return live.evaluate(QStringLiteral("document.querySelector('%1').textContent").arg(QLatin1String(selector))).toString();
}

bool hasText(const VectorDocument &document, const QUuid &under, const QString &text)
{
    for (const QUuid &id : document.descendants(under)) {
        const VectorObject *object = document.find(id);
        if (object && object->kind == ObjectKind::text && object->text.text.contains(text))
            return true;
    }
    return false;
}

// The request id the last prompt carried.
QString requestIn(const QString &prompt)
{
    return prompt.section(QLatin1String("(request "), 1).section(QLatin1Char(')'), 0, 0);
}
}

class SiteEditsTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_site;
    QString m_app;
    QString m_project;

    QString prompt() const { return QString::fromUtf8(read(m_directory.filePath(QStringLiteral("prompt")))); }

    // The app's desktop is fake; `browser` puts a window for Omastrator's browser at 0,0 with the headless viewport's size.
    FakeDesktop *fakeDesktop(AgentBridge &bridge, bool browser)
    {
        auto fake = std::make_unique<FakeDesktop>();
        FakeDesktop *desktop = fake.get();
        desktop->addWindow(QStringLiteral("foot"), QRect(100, 50, 800, 600), 4242);
        if (browser)
            desktop->addWindow(QStringLiteral("chromium"), QRect(0, 0, 1280, 800), bridge.liveSession().browser().processId());
        bridge.designMode().setSource(std::move(fake));
        bridge.designMode().start();
        return desktop;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (Browser::executable().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("Chromium and git are needed for editing sites in Omastrator's browser.");
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("GIT_CONFIG_GLOBAL", m_directory.filePath(QStringLiteral("gitconfig")).toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        write(m_directory.filePath(QStringLiteral("gitconfig")), "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_GH", "/bin/false");
        qputenv("OMASTRATOR_RCLONE", "/bin/false");
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qputenv("OMASTRATOR_LIVE_HEADLESS", "1");
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        executable(m_directory.filePath(QStringLiteral("hyprctl")), "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$0.log\"\n");
        qputenv("OMASTRATOR_HYPRCTL", m_directory.filePath(QStringLiteral("hyprctl")).toUtf8());
        executable(m_directory.filePath(QStringLiteral("wl-copy")), "#!/bin/sh\ncat > \"$0.out\"\n");
        qputenv("OMASTRATOR_WL_COPY", m_directory.filePath(QStringLiteral("wl-copy")).toUtf8());
        executable(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("prompt")).toUtf8());
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());

        // Someone else's site (served, never registered), your app's source, and your own registered site.
        m_site = m_directory.filePath(QStringLiteral("their-site"));
        plainSite(m_site, false);
        m_app = m_directory.filePath(QStringLiteral("my-app"));
        plainSite(m_app, true);
        m_project = m_directory.filePath(QStringLiteral("my-site"));
        plainSite(m_project, true);
    }

    void aSiteThatIsntYoursKeepsItsEditsOnThisMachine()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        StaticServer server;
        QVERIFY(server.serve(m_site).isEmpty());
        QVERIFY(bridge.startLive(server.url(), QString()).isEmpty());
        QVERIFY2(running(bridge), qPrintable(bridge.liveSession().message()));
        LiveSession &live = bridge.liveSession();
        QVERIFY(live.isMockup());
        QCOMPARE(live.status()["site"].toObject()["notice"].toString(), QStringLiteral("Not your site: changes stay on this machine."));
        // The page says so too.
        QVERIFY(live.evaluate(QStringLiteral("document.getElementById('omastrator-overlay').shadowRoot.querySelector('.site').style.display")).toString()
                == QLatin1String("flex"));

        // Real DOM and CSS edits, snapped to the page's own custom properties.
        const QString ink = computed(live, "#title", "color");
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e21e49")).isEmpty());
        QCOMPARE(computed(live, "#title", "color"), QStringLiteral("rgb(225, 29, 72)"));
        QCOMPARE(live.edits().front().token, QStringLiteral("--brand"));
        QVERIFY(live.edit(QStringLiteral(".lead"), QStringLiteral("text"), QStringLiteral("Edited here.")).isEmpty());
        QCOMPARE(textOf(live, ".lead"), QStringLiteral("Edited here."));
        QVERIFY(live.edit(QStringLiteral("main"), QStringLiteral("text"), QStringLiteral("Gone")).contains(QLatin1String("more than text")));
        QVERIFY(live.edit(QStringLiteral(".card"), QStringLiteral("border-radius"), QStringLiteral("12px")).isEmpty());
        QCOMPARE(computed(live, ".card", "borderTopLeftRadius"), QStringLiteral("12px"));

        // Deploy is hidden: there's nothing of yours to deploy.
        bridge.showLivePanel();
        QTest::qWait(50);
        QWidget *panel = window.findChild<QWidget *>(QStringLiteral("livePanel"));
        QVERIFY(panel);
        const auto deploys = panel->findChildren<QPushButton *>(QStringLiteral("liveDeploy"));
        QVERIFY(!deploys.isEmpty() && std::all_of(deploys.begin(), deploys.end(), [](QPushButton *each) { return each->isHidden(); }));
        QCOMPARE(panel->findChildren<QLabel *>(QStringLiteral("liveProject")).last()->text(), QStringLiteral("Not your site: changes stay on this machine."));
        QVERIFY(bridge.liveDeploy({}).contains(QLatin1String("mock-up")));

        // Kept as a named set; the edits stay on the page.
        QJsonObject result;
        QVERIFY(bridge.live(QStringLiteral("keepEdits"), {{"name", "Brand title"}}, result).isEmpty());
        QCOMPARE(result["name"].toString(), QStringLiteral("Brand title"));
        QVERIFY(live.edits().empty());
        std::vector<EditSets::Set> sets = EditSets::read(live.origin());
        QCOMPARE(sets.size(), size_t(1));
        QCOMPARE(sets.front().edits.size(), size_t(3));
        QCOMPARE(textOf(live, ".lead"), QStringLiteral("Edited here."));
        QTRY_VERIFY(!panel->findChildren<QCheckBox *>(QStringLiteral("liveEditSet")).isEmpty());

        // Exported: CSS in the page's own tokens, and a userstyle.
        const QString css = m_directory.filePath(QStringLiteral("export/their-site.css"));
        QVERIFY(bridge.live(QStringLiteral("exportEdits"), {{"path", css}}, result).isEmpty());
        const QString sheet = QString::fromUtf8(read(css));
        QVERIFY2(sheet.contains(QLatin1String("#title {\n  color: var(--brand) !important;\n}")), qPrintable(sheet));
        QVERIFY(sheet.contains(QLatin1String("border-radius: var(--radius-card) !important;")));
        QVERIFY(sheet.contains(QLatin1String("\"Edited here.\"")));
        QVERIFY(!sheet.contains(QLatin1String("==UserStyle==")));
        const QString userstyle = m_directory.filePath(QStringLiteral("export/their-site.user.css"));
        QVERIFY(bridge.live(QStringLiteral("exportEdits"), {{"path", userstyle}}, result).isEmpty());
        QVERIFY(QString::fromUtf8(read(userstyle)).contains(QStringLiteral("@-moz-document url-prefix(\"%1/\")").arg(live.origin())));

        // Toggled off, the page is as the site made it; on, the set is back.
        QVERIFY(bridge.live(QStringLiteral("toggleEdits"), {{"name", "Brand title"}, {"on", false}}, result).isEmpty());
        QCOMPARE(computed(live, "#title", "color"), ink);
        QCOMPARE(textOf(live, ".lead"), QStringLiteral("Edit me live."));
        QVERIFY(!EditSets::read(live.origin()).front().enabled);
        QVERIFY(bridge.live(QStringLiteral("toggleEdits"), {{"name", "Brand title"}, {"on", true}}, result).isEmpty());
        QCOMPARE(computed(live, "#title", "color"), QStringLiteral("rgb(225, 29, 72)"));

        // An edit not kept yet survives a reload of the page.
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("font-size"), QStringLiteral("40px")).isEmpty());
        live.evaluate(QStringLiteral("location.reload()"));
        QTRY_COMPARE_WITH_TIMEOUT(computed(live, "#title", "fontSize"), QStringLiteral("40px"), 15'000);
        QTRY_COMPARE_WITH_TIMEOUT(textOf(live, ".lead"), QStringLiteral("Edited here."), 15'000);

        // Revisited in a new session, the kept set comes back by itself.
        live.stop();
        QVERIFY(bridge.startLive(server.url(), QString()).isEmpty());
        QVERIFY(running(bridge));
        QTRY_COMPARE_WITH_TIMEOUT(textOf(live, ".lead"), QStringLiteral("Edited here."), 15'000);
        QCOMPARE(computed(live, "#title", "color"), QStringLiteral("rgb(225, 29, 72)"));
        QCOMPARE(computed(live, ".card", "borderTopLeftRadius"), QStringLiteral("12px"));
        QVERIFY(bridge.live(QStringLiteral("removeEdits"), {{"name", "Brand title"}}, result).isEmpty());
        QCOMPARE(textOf(live, ".lead"), QStringLiteral("Edit me live."));
        QVERIFY(EditSets::read(live.origin()).empty());
        live.stop();
    }

    void beforeAndAfterLandOnTheDeskAsOneStep()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        fakeDesktop(bridge, false);
        StaticServer server;
        QVERIFY(server.serve(m_site).isEmpty());
        QVERIFY(bridge.startLive(server.url(), QString()).isEmpty());
        QVERIFY(running(bridge));
        LiveSession &live = bridge.liveSession();
        QJsonObject result;
        QVERIFY(bridge.live(QStringLiteral("beforeAfter"), {}, result).contains(QLatin1String("no edits")));
        QVERIFY(live.edit(QStringLiteral(".lead"), QStringLiteral("text"), QStringLiteral("Edited for the client.")).isEmpty());

        QVERIFY(bridge.live(QStringLiteral("beforeAfter"), {}, result).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.designMode().liftJob(), 60'000);
        ProjectTab *desk = bridge.designMode().deskTab();
        QVERIFY(desk);
        const auto frames = Desk::frames(*desk->session.document());
        QVERIFY2(frames.size() == 2, qPrintable(bridge.designMode().message()));
        QCOMPARE(desk->session.undoName(), QStringLiteral("Before and After to Desk"));
        QVERIFY(frames[0].second.contains(QLatin1String(", before")));
        QVERIFY(frames[1].second.contains(QLatin1String(", after")));
        QVERIFY(hasText(*desk->session.document(), frames[0].first, QStringLiteral("Edit me live.")));
        QVERIFY(hasText(*desk->session.document(), frames[1].first, QStringLiteral("Edited for the client.")));
        // The page keeps its edits afterwards.
        QCOMPARE(textOf(live, ".lead"), QStringLiteral("Edited for the client."));
        // One undo takes both frames away.
        desk->session.undo();
        QVERIFY(Desk::frames(*desk->session.document()).empty());
        live.stop();
    }

    void handToAgentFromAPageThatIsntYours()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        fakeDesktop(bridge, false);
        StaticServer server;
        QVERIFY(server.serve(m_site).isEmpty());
        QVERIFY(bridge.startLive(server.url(), QString()).isEmpty());
        QVERIFY(running(bridge));
        LiveSession &live = bridge.liveSession();
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e11d48")).isEmpty());

        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        QJsonObject result;
        QVERIFY(bridge.live(QStringLiteral("handoff"), {{"page", true}, {"folder", m_app}, {"prompt", "Make my header match"}}, result).isEmpty());
        QTRY_VERIFY(!prompt().isEmpty());
        const QString text = prompt();
        QVERIFY(text.contains(QLatin1String("hand-off")) && text.contains(QLatin1String("Make my header match")));
        QVERIFY(text.contains(QLatin1String("The page before the user's edits: ")));
        const QString css = text.section(QLatin1String("Those edits as CSS: "), 1).section(QLatin1Char('\n'), 0, 0);
        QVERIFY2(QString::fromUtf8(read(css)).contains(QLatin1String("color: var(--brand) !important;")), qPrintable(css));
        QVERIFY(text.contains(QLatin1String("#title { color: ")));
        const QString png = text.section(QLatin1String("The mockup: "), 1).section(QLatin1Char(' '), 0, 0);
        QVERIFY2(!QImage(png).isNull(), qPrintable(png));
        // The agent's change is a review, found behind Review changes; nothing opens by itself.
        QVERIFY(bridge.liveAgentDone(requestIn(text), QStringLiteral("Header matches the mockup")).isEmpty());
        QCOMPARE(bridge.liveReviews().back().folder, QFileInfo(m_app).canonicalFilePath());
        QVERIFY(bridge.liveReviews().back().diff().contains(QLatin1String("+#title { color: var(--brand); }")));
        QVERIFY(!bridge.reviewPanel().isVisible());
        QVERIFY(bridge.unsavedFiles() > 0);
        QVERIFY(bridge.discardReview(bridge.liveReviews().back().id).isEmpty());

        // Without a folder, the sheet asks, offering the one used for this site last time.
        QVERIFY(bridge.live(QStringLiteral("handoff"), {{"page", true}}, result).isEmpty());
        QVERIFY(result["sheet"].toBool());
        QDialog *sheet = nullptr;
        QTRY_VERIFY((sheet = window.findChild<QDialog *>(QStringLiteral("handoffSheet"))));
        QCOMPARE(sheet->findChild<QLineEdit *>(QStringLiteral("handoffFolder"))->text(), QFileInfo(m_app).canonicalFilePath());
        sheet->reject();
        live.stop();
    }

    void handToAgentFromArtAndALiftedApp()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        FakeDesktop *desktop = fakeDesktop(bridge, false);
        DesignController &design = bridge.designMode();
        QJsonObject result;
        design.run(QStringLiteral("onboarding"), {{"finish", false}}, result);
        design.run(QStringLiteral("on"), {}, result);
        design.run(QStringLiteral("tool"), {{"tool", "inspect"}}, result);
        // A settings window lifted from its accessibility tree, then a note drawn beside it.
        desktop->trees.insert(4242, QJsonObject{{"root", QJsonObject{{"role", "frame"}, {"name", "Settings"}, {"rect", QJsonArray{0, 0, 800, 600}},
                                                                     {"children", QJsonArray{QJsonObject{{"role", "push button"}, {"name", "Save"},
                                                                                                         {"rect", QJsonArray{20, 20, 120, 32}},
                                                                                                         {"text", "Save"}, {"index", 0}}}}}}});
        desktop->pointer = QPoint(300, 300);
        design.mode().poll();
        const QJsonValue target = design.status()["bar"].toObject()["target"];
        QVERIFY(design.run(QStringLiteral("lift"), {{"target", target}}, result).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!design.liftJob(), 20'000);
        QVERIFY(!design.overlays().art(QStringLiteral("window:foot")).empty());
        QVERIFY(design.run(QStringLiteral("draw"), {{"tool", "rectangle"}, {"points", QJsonArray{QJsonArray{150, 100}, QJsonArray{260, 140}}}}, result).isEmpty());

        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        QVERIFY(design.run(QStringLiteral("handoff"), {{"surface", "window:foot"}, {"folder", m_app}}, result).isEmpty());
        QVERIFY(!result["requestId"].toString().isEmpty());
        QTRY_VERIFY(!prompt().isEmpty());
        const QString text = prompt();
        QVERIFY(text.contains(QLatin1String("It came from: foot")));
        const QString svg = text.section(QLatin1String("the same as SVG: "), 1).section(QLatin1Char('\n'), 0, 0);
        QVERIFY2(QFileInfo::exists(svg), qPrintable(svg));
        QVERIFY(QFileInfo::exists(text.section(QLatin1String("The screen as it is now: "), 1).section(QLatin1Char('\n'), 0, 0)));
        const QString selectors = text.section(QLatin1String("with where it sits: "), 1).section(QLatin1Char('\n'), 0, 0);
        const QJsonArray mapped = QJsonDocument::fromJson(read(selectors)).array();
        QVERIFY2(!mapped.isEmpty(), qPrintable(selectors));
        QVERIFY(QString::fromUtf8(read(selectors)).contains(QLatin1String("push button")));
        QVERIFY(bridge.liveAgentDone(result["requestId"].toString(), QStringLiteral("Styled the Save button")).isEmpty());
        QCOMPARE(bridge.liveReviews().back().folder, QFileInfo(m_app).canonicalFilePath());
        QVERIFY(bridge.discardReview(bridge.liveReviews().back().id).isEmpty());
        // The folder is remembered for that window.
        QCOMPARE(AnywhereSettings::handoffFolder(QStringLiteral("window:foot")), QFileInfo(m_app).canonicalFilePath());
    }

    void liftedVectorEditsWriteBackToYourOwnSite()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        QVERIFY(bridge.startLive({}, m_project).isEmpty());
        QVERIFY2(running(bridge), qPrintable(bridge.liveSession().message()));
        LiveSession &live = bridge.liveSession();
        QVERIFY(!live.isMockup());
        FakeDesktop *desktop = fakeDesktop(bridge, true);
        DesignController &design = bridge.designMode();
        QJsonObject result;
        design.run(QStringLiteral("onboarding"), {{"finish", false}}, result);
        design.run(QStringLiteral("on"), {}, result);
        design.run(QStringLiteral("tool"), {{"tool", "inspect"}}, result);

        // The heading, lifted onto the overlay from the page.
        desktop->pointer = QPoint(40, 60);
        design.mode().poll();
        const QJsonObject bar = design.status()["bar"].toObject();
        QCOMPARE(bar["kind"].toString(), QStringLiteral("web"));
        const QString key = bar["surface"].toString();
        QVERIFY(design.run(QStringLiteral("lift"), {{"target", bar["target"]}}, result).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!design.liftJob(), 30'000);
        EditorSession &overlay = design.overlays().session();
        const VectorObject *title = nullptr;
        for (const VectorObject &object : overlay.document()->objects)
            if (object.kind == ObjectKind::text && object.liftedFrom == QLatin1String("#title"))
                title = &object;
        QVERIFY2(title, qPrintable(design.message()));
        QVERIFY(title->text.text.contains(QLatin1String("Hello from a plain site")));
        QVERIFY(!LiftDiff::readBaselines(design.liftedPath()).isEmpty());

        // New words and the brand colour on the lifted text: one undo step on the overlay.
        const QUuid id = title->id;
        overlay.beginInteraction(QStringLiteral("Edit Lifted Heading"));
        VectorDocument next = *overlay.document();
        next.find(id)->text.text = QStringLiteral("Hello from the overlay");
        next.find(id)->fill = Paint::solid(QColor(0xe1, 0x1d, 0x48));
        overlay.previewDocument(next, {id});
        overlay.commitInteraction();

        // Apply to Source: the text is certain and written directly; the colour goes to the agent.
        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        QVERIFY(design.run(QStringLiteral("send"), {{"destination", "source"}, {"surface", key}}, result).isEmpty());
        const QStringList applied = [&] {
            QStringList list;
            for (const QJsonValue &each : result["applied"].toArray())
                list << each.toString();
            return list;
        }();
        QVERIFY2(applied.join('\n').contains(QLatin1String("#title: text")), qPrintable(applied.join('\n')));
        QVERIFY(applied.join('\n').contains(QLatin1String("#title: color")));
        QCOMPARE(textOf(live, "#title"), QStringLiteral("Hello from the overlay"));
        QVERIFY(read(m_project + QStringLiteral("/index.html")).contains("<h1 id=\"title\">Hello from the overlay</h1>"));
        QCOMPARE(bridge.liveReviews().front().title, QStringLiteral("Live edits"));
        QVERIFY(!result["requestId"].toString().isEmpty());
        QTRY_VERIFY(prompt().contains(QLatin1String("#title: color")));
        QVERIFY(bridge.liveAgentDone(result["requestId"].toString(), QStringLiteral("Brand colour on the heading")).isEmpty());
        QCOMPARE(bridge.liveReviews().size(), size_t(2));
        // Applied again with nothing new, the lifted edits aren't sent twice.
        std::vector<QUuid> roots = design.overlays().art(key);
        QVERIFY(LiftDiff::changes(*overlay.document(), roots, LiftDiff::readBaselines(design.liftedPath()), nullptr).empty());
        live.stop();
    }
};

QTEST_MAIN(SiteEditsTests)
#include "SiteEditsTests.moc"
