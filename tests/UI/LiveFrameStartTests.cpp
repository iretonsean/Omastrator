#include "../RestoreEnvironment.h"
#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <memory>

// How Live starts on a Browser View for each kind of site (docs/LIVE-IN-FRAME.md, section 2), against a real headless page
// on a throwaway profile. "Production" is a tiny server on 127.0.0.2, which isn't loopback as far as Live is concerned.
// Skips without Chromium.
namespace {
constexpr int patience = 60'000;

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray pageSaying(const char *where)
{
    return QByteArray("<!doctype html><title>t</title><h1 id=\"where\">") + where + "</h1>";
}

// Answers every request with the same page.
class Production : public QObject {
public:
    Production()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->readAll();
                    const QByteArray body = pageSaying("production");
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    bool listen() { return m_server.listen(QHostAddress(QStringLiteral("127.0.0.2")), 0); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.2:%1/index.html").arg(m_server.serverPort())); }

private:
    QTcpServer m_server;
};

struct Hosted {
    EditorCanvas canvas;
    QUuid frame;
    Hosted(EditorSession &session, const QUrl &url) : canvas(session)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 600, 400}, QStringLiteral("Site"));
        view.browser = BrowserView{url, {}, {}};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
        canvas.resize(1000, 800);
        canvas.show();
        BrowserViews::of(session)->attach(&canvas);
    }
};
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class LiveFrameStartTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;
    QStringList m_folders;

    // A project folder that the shared static server can serve, with an optional dev override.
    QString project(const QString &dev = QString())
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/project%1").arg(++m_sites);
        m_folders.append(folder);
        write(folder + QStringLiteral("/index.html"), pageSaying("dev"));
        if (!dev.isEmpty())
            write(folder + QStringLiteral("/omastrator.json"), QJsonDocument(QJsonObject{{"dev", dev}}).toJson());
        return folder;
    }

    // Reads the tab's page straight off the pool, so it works whether or not Live is running on the frame.
    QString where(EditorSession &session, const QUuid &frame)
    {
        return text(session, frame, QStringLiteral("document.getElementById('where') ? document.getElementById('where').textContent : ''"));
    }

    // A string `expression` gives in the frame's tab, read off the pool.
    QString text(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        if (key.isNull())
            return {};
        auto answer = std::make_shared<std::atomic<int>>(0);
        auto text = std::make_shared<QString>();
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"),
                                   {{"expression", expression},
                                    {"returnByValue", true}},
                                   [answer, text](const QJsonObject &result, const QString &error) {
                                       if (error.isEmpty())
                                           *text = result.value("result").toObject().value("value").toString();
                                       answer->store(1);
                                   });
        for (int i = 0; i < 200 && !answer->load(); ++i)
            QTest::qWait(25);
        return answer->load() ? *text : QString();
    }

    // Runs `expression` in the frame's tab from the pool, whether or not Live is on it.
    void inTab(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        QVERIFY(!key.isNull());
        auto answered = std::make_shared<std::atomic<int>>(0);
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"), {{"expression", expression}},
                                   [answered](const QJsonObject &, const QString &) { answered->store(1); });
        for (int i = 0; i < 200 && !answered->load(); ++i)
            QTest::qWait(25);
        QVERIFY(answered->load());
    }

    // Whether the picture has a patch of `colour` (within a small tolerance) anywhere.
    static bool shows(const QImage &picture, const QColor &colour)
    {
        if (picture.isNull())
            return false;
        int hits = 0;
        for (int y = 0; y < picture.height(); y += 3) {
            for (int x = 0; x < picture.width(); x += 3) {
                const QColor pixel = picture.pixelColor(x, y);
                if (qAbs(pixel.red() - colour.red()) < 24 && qAbs(pixel.green() - colour.green()) < 24 && qAbs(pixel.blue() - colour.blue()) < 24 && ++hits > 20)
                    return true;
            }
        }
        return false;
    }

    void edit(EditorSession &session, const QUuid &frame, const QString &selector, const QString &property, const QString &value)
    {
        QString failure = QStringLiteral("pending");
        LiveFrames::of(session)->edit(frame, selector, property, value, [&](const QString &error) { failure = error; });
        QTRY_VERIFY_WITH_TIMEOUT(failure != QLatin1String("pending"), patience);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
        BrowserViews::setFolderChooser({});
        QFile::remove(ProjectRegistry::path());
    }

    void cleanup()
    {
        for (const QString &folder : std::as_const(m_folders))
            LiveFrames::clearPending(folder);
        m_folders.clear();
        BrowserViews::shutdownPool();
        BrowserViews::setFolderChooser({});
    }

    void anOwnedSiteRunsFromItsDevServerAndGoesBackWhenStopped()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY(!host->bar(hosted.frame).notYours);
        QVERIFY(!host->bar(hosted.frame).dev);

        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::running, patience);
        QVERIFY(!frames->snapshot(hosted.frame).serverUrl.isEmpty());
        // The tab moved to the project's server, and the document still says production.
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
        QCOMPARE(session.document()->find(hosted.frame)->browser->url, production.url());
        QVERIFY(host->bar(hosted.frame).dev);
        QVERIFY(host->bar(hosted.frame).devTip.contains(frames->snapshot(hosted.frame).serverUrl.host()));
        QCOMPARE(DevServers::shared().holders(folder), 1);

        frames->stop(hosted.frame);
        QCOMPARE(DevServers::shared().holders(folder), 0);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY(!host->bar(hosted.frame).dev);
        QCOMPARE(session.document()->find(hosted.frame)->browser->url, production.url());
    }

    void navigationsOnTheDevServerAreWrittenBackAsProductionAddresses()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        write(folder + QStringLiteral("/second.html"), pageSaying("second"));
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
        const QUrl dev = frames->snapshot(hosted.frame).serverUrl;
        QVERIFY(!dev.isEmpty());

        // A navigation on the dev server (a link, a route change) is the production page as far as the document goes.
        inTab(session, hosted.frame, QStringLiteral("location.href = '/second.html'"));
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("second"), patience);
        const QUrl saved = session.document()->find(hosted.frame)->browser->url;
        QCOMPARE(saved.host(), production.url().host());
        QCOMPARE(saved.port(), production.url().port());
        QCOMPARE(saved.path(), QStringLiteral("/second.html"));

        // Stopping while the tab is still on the server's page never lets the server's address into the document.
        frames->stop(hosted.frame);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        const QUrl after = session.document()->find(hosted.frame)->browser->url;
        QCOMPARE(after.host(), production.url().host());
        QCOMPARE(after.port(), production.url().port());
        QCOMPARE(after.path(), QStringLiteral("/second.html"));
        QVERIFY(after.port() != dev.port());
    }

    void everyWayALiveEndsLetsItsDevServerGo()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        const auto served = [&](EditorSession &session, Hosted &hosted) {
            QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
            QVERIFY2(BrowserViews::of(session)->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
            QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).state, LiveSession::State::running, patience);
            QTRY_COMPARE_WITH_TIMEOUT(DevServers::shared().holders(folder), 1, patience);
        };
        {
            // Reset.
            EditorSession session;
            Hosted hosted(session, production.url());
            served(session, hosted);
            BrowserViews::resetAll();
            QCOMPARE(DevServers::shared().holders(folder), 0);
        }
        {
            // Deleting the frame.
            EditorSession session;
            Hosted hosted(session, production.url());
            served(session, hosted);
            session.select({hosted.frame});
            session.deleteSelection();
            QTRY_COMPARE_WITH_TIMEOUT(DevServers::shared().holders(folder), 0, 10'000);
        }
        {
            // Closing the document.
            auto session = std::make_unique<EditorSession>();
            auto hosted = std::make_unique<Hosted>(*session, production.url());
            served(*session, *hosted);
            hosted.reset();
            session.reset();
            QCOMPARE(DevServers::shared().holders(folder), 0);
        }
    }

    void stoppingAFrameWhileItsDevServerStartsIsQuickAndLeavesNothingBehind()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project(QStringLiteral("sleep 6; echo it came up late; exit 3"));
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).message.contains(QLatin1String("Starting the project")), patience);
        QCOMPARE(DevServers::shared().holders(folder), 1);

        // The pool's thread isn't held by the wait: other work on it goes on meanwhile.
        bool answered = false;
        frames->run(hosted.frame, [](LiveSession &) { return QString(); }, [&answered](const QString &) { answered = true; });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 2000);

        // Stopping mustn't wait for the server, which has five more seconds to go.
        frames->stop(hosted.frame);
        QTRY_COMPARE_WITH_TIMEOUT(DevServers::shared().holders(folder), 0, 3000);
        QVERIFY(!frames->active(hosted.frame));
        // The session went with it: nothing of it is left to answer when the server would have failed.
        QTest::qWait(7000);
        QCOMPARE(where(session, hosted.frame), QStringLiteral("production"));
        QCOMPARE(DevServers::shared().holders(folder), 0);
    }

    void navigatingAwayWhileTheDevServerStartsEndsTheStartAndComingBackServesAgain()
    {
        NEEDS_CHROMIUM;
        Production production;
        Production elsewhere;
        QVERIFY(production.listen() && elsewhere.listen());
        const QString folder = project(QStringLiteral("sleep 6; echo it came up late; exit 3"));
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).message.contains(QLatin1String("Starting the project")), patience);
        QCOMPARE(DevServers::shared().holders(folder), 1);

        // Another site's page, in the middle of the start: the project's server is let go and the frame is a mock-up, not stuck.
        inTab(session, hosted.frame, QStringLiteral("location.href = '%1'").arg(elsewhere.url().toString()));
        QTRY_VERIFY2_WITH_TIMEOUT(frames->snapshot(hosted.frame).state == LiveSession::State::running, qPrintable(frames->snapshot(hosted.frame).message), 10'000);
        QVERIFY(frames->snapshot(hosted.frame).mockup);
        QVERIFY(!frames->snapshot(hosted.frame).startingServer);
        QVERIFY(!frames->snapshot(hosted.frame).message.contains(QLatin1String("Starting the project")));
        QCOMPARE(DevServers::shared().holders(folder), 0);

        // Back on the project's site, it is served again.
        inTab(session, hosted.frame, QStringLiteral("location.href = '%1'").arg(production.url().toString()));
        QTRY_COMPARE_WITH_TIMEOUT(DevServers::shared().holders(folder), 1, 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).message.contains(QLatin1String("Starting the project")), 10'000);
        QVERIFY(!frames->snapshot(hosted.frame).mockup);
        frames->stop(hosted.frame);
        QCOMPARE(DevServers::shared().holders(folder), 0);
    }

    // The project's dev server started through npm, as a real project's is: `npm run dev` prints Vite's banner, whose address
    // is read from it and is on `localhost`. LiveFramePictureTests runs the same follow-the-page steps against a Python
    // server named by omastrator.json; this is the other way in, npm's own start. The package has no dependencies, so
    // nothing is installed and the test needs no network: dev-server.mjs stands in for `vite`.
    void thePictureFollowsThePageOnTheProjectsNpmDevServer()
    {
        NEEDS_CHROMIUM;
        if (QStandardPaths::findExecutable(QStringLiteral("npm")).isEmpty() || QStandardPaths::findExecutable(QStringLiteral("node")).isEmpty())
            QSKIP("npm and node aren't installed, so the dev-server fixture can't run.");
        // npm looks for a newer npm on the registry unless told not to; nothing here goes to the network.
        const RestoreEnvironment notifier("NPM_CONFIG_UPDATE_NOTIFIER");
        qputenv("NPM_CONFIG_UPDATE_NOTIFIER", "false");
        Production production;
        QVERIFY(production.listen());
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/npm%1").arg(++m_sites);
        const QString fixture = QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/vite-tailwind");
        QDirIterator files(fixture, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            const QString relative = QDir(fixture).relativeFilePath(path);
            // Its own package.json names Vite and Tailwind, which `npm install` would fetch; this one names nothing.
            if (relative == QLatin1String("package.json") || relative == QLatin1String("package-lock.json"))
                continue;
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            write(folder + QLatin1Char('/') + relative, file.readAll());
        }
        write(folder + QStringLiteral("/package.json"), R"({"name": "dev-server-fixture", "private": true, "type": "module", "scripts": {"dev": "node dev-server.mjs"}})");
        m_folders.append(folder);
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViews *views = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QTRY_VERIFY_WITH_TIMEOUT(!views->picture(hosted.frame).isNull(), patience);

        // Edit Page starts the project's dev server, and the picture moves to it: the fixture's sky-500 button.
        QVERIFY2(views->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::running, patience);
        QVERIFY(!frames->snapshot(hosted.frame).serverUrl.isEmpty());
        QCOMPARE(frames->snapshot(hosted.frame).serverUrl.host(), QStringLiteral("localhost"));
        QCOMPARE(frames->snapshot(hosted.frame).serverCommand, QStringLiteral("npm run dev"));
        const QString overlay = QStringLiteral("document.querySelector('#cta') && window.__oma ? 'ready' : ''");
        QTRY_COMPARE_WITH_TIMEOUT(text(session, hosted.frame, overlay), QStringLiteral("ready"), patience);
        QTRY_VERIFY2_WITH_TIMEOUT(shows(views->picture(hosted.frame), QColor(0x00, 0xa6, 0xf4)), "the picture never showed the dev server's page", 15'000);

        // An edit reaches the picture, and so does the next one.
        edit(session, hosted.frame, QStringLiteral("#title"), QStringLiteral("background-color"), QStringLiteral("#e11d48"));
        QTRY_VERIFY2_WITH_TIMEOUT(shows(views->picture(hosted.frame), QColor(0xe1, 0x1d, 0x48)), "the picture didn't show the first edit", 15'000);
        edit(session, hosted.frame, QStringLiteral("#cta"), QStringLiteral("background-color"), QStringLiteral("#16a34a"));
        QTRY_VERIFY2_WITH_TIMEOUT(shows(views->picture(hosted.frame), QColor(0x16, 0xa3, 0x4a)), "the picture didn't show the second edit", 15'000);

        // The code changes on disk, as an agent's Build It changes it, and Reload shows it; edits after that still show.
        QFile style(folder + QStringLiteral("/src/style.css"));
        QVERIFY(style.open(QIODevice::Append));
        style.write("\nbody { background-color: #7c3aed; }\n");
        style.close();
        views->act(hosted.frame, BrowserViewHost::Action::reload);
        QTRY_VERIFY2_WITH_TIMEOUT(shows(views->picture(hosted.frame), QColor(0x7c, 0x3a, 0xed)), "the picture didn't follow Reload", 15'000);
        QTRY_COMPARE_WITH_TIMEOUT(text(session, hosted.frame, overlay), QStringLiteral("ready"), patience);
        edit(session, hosted.frame, QStringLiteral("#title"), QStringLiteral("background-color"), QStringLiteral("#f59e0b"));
        QTRY_VERIFY2_WITH_TIMEOUT(shows(views->picture(hosted.frame), QColor(0xf5, 0x9e, 0x0b)), "the picture stopped following after Reload", 15'000);
        frames->stop(hosted.frame);
    }

    void aSiteThatIsntYoursStaysOnItsOwnServer()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY(host->bar(hosted.frame).notYours);

        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page must start");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::running, patience);
        QVERIFY(frames->snapshot(hosted.frame).serverUrl.isEmpty());
        QCOMPARE(where(session, hosted.frame), QStringLiteral("production"));
        QVERIFY(!host->bar(hosted.frame).dev);
    }

    void aDevServerThatFailsSaysSoAndTheTabStaysOnProduction()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project(QStringLiteral("echo the dev script blew up; exit 3"));
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);

        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "Edit Page begins; the failure comes from the server");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::failed, patience);
        QVERIFY2(host->message(hosted.frame).contains(QLatin1String("Couldn't start the project")), qPrintable(host->message(hosted.frame)));
        QVERIFY(!host->bar(hosted.frame).dev);

        // Fixing the project and choosing Edit Page again starts it.
        write(folder + QStringLiteral("/omastrator.json"), "{}");
        QVERIFY2(host->beginEditPage(hosted.frame).isEmpty(), "A new Edit Page must restart it");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::running, patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
    }

    void thisIsMySiteRegistersTheFolderAndOnlyOffersItForOtherPeoplesSites()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViewHost *host = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QVERIFY(host->bar(hosted.frame).notYours);

        const auto offered = [&host, &hosted] {
            QMenu menu;
            host->extendBarMenu(hosted.frame, &menu);
            QStringList titles;
            for (const QAction *action : menu.actions())
                titles << action->text();
            return titles;
        };
        QVERIFY(offered().contains(QStringLiteral("This Is My Site…")));
        QVERIFY(offered().contains(QStringLiteral("Show Original")));

        QUrl asked;
        BrowserViews::setFolderChooser([&asked, folder](const QUrl &url) {
            asked = url;
            return folder;
        });
        host->act(hosted.frame, BrowserViewHost::Action::thisIsMySite);
        QCOMPARE(asked, production.url());
        QCOMPARE(ProjectRegistry::folderFor(production.url()).value_or(QString()), folder);
        QVERIFY(!host->bar(hosted.frame).notYours);
        // Now it is the project's menu, not the other people's-site one.
        QVERIFY(!offered().contains(QStringLiteral("This Is My Site…")) && !offered().contains(QStringLiteral("Show Original")));
        QVERIFY(offered().contains(QStringLiteral("Deploy")) && offered().contains(QStringLiteral("History")));
    }
};

QTEST_MAIN(LiveFrameStartTests)
#include "LiveFrameStartTests.moc"
