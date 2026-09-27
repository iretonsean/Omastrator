#include "Live/Browser.h"
#include "Live/LiveSession.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include <QElapsedTimer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Phase 5 of docs/OS-SUITE.md in headless Chromium: open the fixture sites,
// select by clicking, edit live, and snap to the page's tokens.
namespace {
void copyFolder(const QString &from, const QString &to)
{
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        const QString target = QDir(to).filePath(QDir(from).relativeFilePath(path));
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::copy(path, target);
    }
}
}

class LiveBrowserTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_fixtures = QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures");

    LiveSession::Target target(const QString &folder, const QUrl &url = {})
    {
        LiveSession::Target target;
        target.folder = folder;
        target.url = url;
        target.headless = true;
        target.profile = m_directory.filePath(QStringLiteral("profile"));
        return target;
    }

    bool waitRunning(LiveSession &live, int timeoutMs = 60'000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < timeoutMs) {
            if (live.state() == LiveSession::State::running || live.state() == LiveSession::State::failed)
                break;
            QTest::qWait(50);
        }
        return live.state() == LiveSession::State::running;
    }

    // A real click in the page, as the user's mouse would make it.
    void click(LiveSession &live, const QString &selector, bool shift = false)
    {
        const QJsonObject rect = live.evaluate(QStringLiteral("window.__oma.info(%1).rect").arg(QStringLiteral("'") + selector + QStringLiteral("'"))).toObject();
        const double x = rect["x"].toDouble() + rect["width"].toDouble() / 2;
        const double y = rect["y"].toDouble() + rect["height"].toDouble() / 2;
        QString error;
        CdpConnection &cdp = live.browser().cdp();
        const QString session = live.pageSession();
        for (const char *type : {"mouseMoved", "mousePressed", "mouseReleased"}) {
            cdp.callAndWait(QStringLiteral("Input.dispatchMouseEvent"),
                            {{"type", type}, {"x", x}, {"y", y}, {"button", "left"}, {"clickCount", 1}, {"modifiers", shift ? 8 : 0}}, session, &error);
            QVERIFY2(error.isEmpty(), qPrintable(error));
        }
    }

    QString computed(LiveSession &live, const QString &selector, const QString &property)
    {
        return live.evaluate(QStringLiteral("getComputedStyle(document.querySelector('%1')).getPropertyValue('%2')").arg(selector, property)).toString();
    }

private slots:
    void initTestCase()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed, so the Live pipeline can't be tested here.");
        QVERIFY(m_directory.isValid());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
    }

    void plainSiteSelectsAndEditsLive()
    {
        const QString site = m_directory.filePath(QStringLiteral("plain"));
        copyFolder(m_fixtures + QStringLiteral("/plain"), site);
        LiveSession live;
        QVERIFY(live.start(target(site)).isEmpty());
        QCOMPARE(live.state(), LiveSession::State::starting);
        QVERIFY2(waitRunning(live), qPrintable(live.message()));
        QVERIFY(!live.isMockup());
        QCOMPARE(live.devServer().command().kind, DevCommand::Kind::staticSite);
        QVERIFY(live.url().toString().startsWith(QLatin1String("http://127.0.0.1:")));
        // The page's own custom properties are its tokens.
        QVERIFY(std::any_of(live.tokens().tokens().begin(), live.tokens().tokens().end(), [](const Token &token) { return token.name == QLatin1String("--brand"); }));

        // Click selects; Shift-click adds; a link doesn't navigate.
        click(live, QStringLiteral("#title"));
        QTRY_COMPARE(live.selection().size(), 1);
        QCOMPARE(live.selection()[0].toObject()["selector"].toString(), QStringLiteral("#title"));
        click(live, QStringLiteral(".lead"), true);
        QTRY_COMPARE(live.selection().size(), 2);
        QCOMPARE(live.selection()[1].toObject()["selector"].toString(), QStringLiteral("body > main > p"));
        const QString before = live.evaluate(QStringLiteral("location.href")).toString();
        click(live, QStringLiteral(".card > a"));
        QTest::qWait(300);
        QCOMPARE(live.evaluate(QStringLiteral("location.href")).toString(), before);
        QTRY_COMPARE(live.selection()[0].toObject()["tag"].toString(), QStringLiteral("a"));

        // A colour near the brand's becomes the brand; spacing near --gap becomes --gap.
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty());
        QCOMPARE(computed(live, QStringLiteral("#title"), QStringLiteral("color")), QStringLiteral("rgb(225, 29, 72)"));
        QCOMPARE(live.edits().size(), size_t(1));
        QCOMPARE(live.edits().front().token, QStringLiteral("--brand"));
        QCOMPARE(live.edits().front().before, QStringLiteral("#0f172a"));
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("padding-top"), QStringLiteral("22px")).isEmpty());
        QCOMPARE(computed(live, QStringLiteral("#title"), QStringLiteral("padding-top")), QStringLiteral("24px"));
        // A second change to the same property is one edit, keeping the first "before".
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#0f172a")).isEmpty());
        QCOMPARE(live.edits().size(), size_t(2));
        QCOMPARE(live.edits().front().before, QStringLiteral("#0f172a"));

        // Text edits come from the page through the binding.
        QSignalSpy applied(&live, &LiveSession::editApplied);
        QVERIFY(live.evaluate(QStringLiteral("window.__oma.editText('.lead', 'Edited live.')")).toBool());
        QTRY_VERIFY(!applied.isEmpty());
        QCOMPARE(live.edits().back().property, QStringLiteral("text"));
        QCOMPARE(live.edits().back().before, QStringLiteral("Edit me live."));
        QCOMPARE(live.edits().back().after, QStringLiteral("Edited live."));

        // A reload keeps the overlay and its tokens.
        QString error;
        live.evaluate(QStringLiteral("location.reload()"), &error);
        QTRY_VERIFY_WITH_TIMEOUT(live.evaluate(QStringLiteral("!!(window.__oma && document.getElementById('omastrator-overlay'))")).toBool(), 10'000);
        live.stop();
        QCOMPARE(live.state(), LiveSession::State::off);
        QVERIFY(!live.browser().isRunning());
    }

    void unregisteredPagesAreMockups()
    {
        StaticServer server;
        QVERIFY(server.serve(m_fixtures + QStringLiteral("/plain")).isEmpty());
        LiveSession live;
        QVERIFY(live.start(target(QString(), server.url())).isEmpty());
        QVERIFY2(waitRunning(live), qPrintable(live.message()));
        QVERIFY(live.isMockup());
        QVERIFY(live.message().contains(QLatin1String("Not your site")));
        QCOMPARE(live.status()["mockup"].toBool(), true);
        live.stop();
        QVERIFY(live.start(target(QString(), QUrl(QStringLiteral("ftp://example.com")))).contains(QLatin1String("http")));
        QVERIFY(live.start(target(m_directory.filePath(QStringLiteral("missing")))).contains(QLatin1String("isn't a folder")));
    }

    void viteTailwindSnapsToTheTheme()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("npm")).isEmpty() || QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty())
            QSKIP("npm and node aren't installed, so the dev-server fixture can't run.");
        const QString project = m_directory.filePath(QStringLiteral("vite-tailwind"));
        copyFolder(m_fixtures + QStringLiteral("/vite-tailwind"), project);
        LiveSession live;
        QVERIFY(live.start(target(project)).isEmpty());
        QVERIFY2(waitRunning(live), qPrintable(live.message()));
        QCOMPARE(live.devServer().command().description, QStringLiteral("npm run dev"));
        QCOMPARE(live.url().host(), QStringLiteral("localhost"));
        QVERIFY(live.tokens().hasTailwind());
        QCOMPARE(live.tokens().spacingPx(), 4.0);

        click(live, QStringLiteral("#cta"));
        QTRY_COMPARE(live.selection().size(), 1);
        QCOMPARE(live.selection()[0].toObject()["classes"].toString(), QStringLiteral("bg-sky-500 text-white p-4 rounded-md font-bold"));

        // 13px padding is the scale's 12px: p-4 becomes p-3, shown inline until Tailwind compiles p-3.
        QVERIFY(live.edit(QStringLiteral("#cta"), QStringLiteral("padding"), QStringLiteral("13px")).isEmpty());
        QCOMPARE(computed(live, QStringLiteral("#cta"), QStringLiteral("padding-top")), QStringLiteral("12px"));
        const QString classes = live.evaluate(QStringLiteral("document.querySelector('#cta').className")).toString();
        QVERIFY2(classes.split(QLatin1Char(' ')).contains(QStringLiteral("p-3")) && !classes.contains(QLatin1String("p-4")), qPrintable(classes));
        QCOMPARE(live.edits().back().removeClass, QStringLiteral("p-4"));
        QCOMPARE(live.edits().back().addClass, QStringLiteral("p-3"));

        // Near sky-700 is sky-700; its class exists, so nothing goes inline.
        QVERIFY(live.edit(QStringLiteral("#cta"), QStringLiteral("background-color"), QStringLiteral("#046aa8")).isEmpty());
        QCOMPARE(live.edits().back().token, QStringLiteral("--color-sky-700"));
        QVERIFY(live.evaluate(QStringLiteral("document.querySelector('#cta').className")).toString().contains(QLatin1String("bg-sky-700")));
        QVERIFY(!live.evaluate(QStringLiteral("document.querySelector('#cta').style.backgroundColor")).toString().size());

        // Size classes and colour classes share text-; only the size changes.
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("font-size"), QStringLiteral("17px")).isEmpty());
        QCOMPARE(computed(live, QStringLiteral("#title"), QStringLiteral("font-size")), QStringLiteral("18px"));
        QCOMPARE(live.evaluate(QStringLiteral("document.querySelector('#title').className")).toString(),
                 QStringLiteral("font-bold text-slate-900 text-lg"));
        live.stop();
        QVERIFY(!DevServer::answers(live.url(), 500));
    }

    void webAppsAndElectronAppsGoThroughLive()
    {
        StaticServer server;
        QVERIFY(server.serve(m_fixtures + QStringLiteral("/plain")).isEmpty());

        // An Omarchy web app: the page in its own app window.
        LiveSession webApp;
        LiveSession::Target app = target(QString(), server.url());
        app.app = true;
        QVERIFY(webApp.start(app).isEmpty());
        QVERIFY2(waitRunning(webApp), qPrintable(webApp.message()));
        click(webApp, QStringLiteral("#title"));
        QTRY_COMPARE(webApp.selection().size(), 1);
        QVERIFY(webApp.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty());
        QCOMPARE(webApp.edits().front().token, QStringLiteral("--brand"));
        webApp.stop();

        // An Electron-style app: a Chromium program run with its own arguments, relaunched with
        // debugging in a dedicated profile, found through what it prints.
        const QString project = m_directory.filePath(QStringLiteral("electron-app"));
        copyFolder(m_fixtures + QStringLiteral("/plain"), project);
        LiveSession electron;
        LiveSession::Target command;
        command.command = QStringLiteral("\"%1\" --headless=new --no-first-run --app=%2").arg(Browser::executable(), server.url().toString());
        command.folder = project;
        command.profile = m_directory.filePath(QStringLiteral("electron-profile"));
        QVERIFY(electron.start(command).isEmpty());
        QVERIFY2(waitRunning(electron), qPrintable(electron.message()));
        QCOMPARE(electron.url(), server.url());
        QCOMPARE(electron.project(), QFileInfo(project).canonicalFilePath());
        QVERIFY(QFileInfo::exists(m_directory.filePath(QStringLiteral("electron-profile"))));
        click(electron, QStringLiteral(".lead"));
        QTRY_COMPARE(electron.selection().size(), 1);
        QVERIFY(electron.edit(QStringLiteral(".lead"), QStringLiteral("padding-top"), QStringLiteral("23px")).isEmpty());
        QCOMPARE(computed(electron, QStringLiteral(".lead"), QStringLiteral("padding-top")), QStringLiteral("24px"));
        electron.stop();
        QVERIFY(!electron.browser().isRunning());

        // Something that isn't Chromium-based is said plainly.
        LiveSession notElectron;
        LiveSession::Target plain;
        plain.command = QStringLiteral("/bin/true");
        plain.profile = m_directory.filePath(QStringLiteral("true-profile"));
        QVERIFY(notElectron.start(plain).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(notElectron.state() == LiveSession::State::failed, 20'000);
        QVERIFY(notElectron.message().contains(QLatin1String("Electron")));
    }
};

QTEST_GUILESS_MAIN(LiveBrowserTests)
#include "LiveBrowserTests.moc"
