#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <memory>

// The Frame tool's Browser View switch and the server behind it (docs/BROWSER-VIEW.md, "The Browser View switch"): on runs
// the project's dev server, off freezes it (the same PID, stopped), on wakes it, and quitting ends it. A real headless page
// on a throwaway profile; the project's dev server is Python's http.server on 127.0.0.1, and "production" is a tiny server
// on 127.0.0.2. Skips without Chromium or python3.
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

// A process's state letter from /proc ('T' is stopped), or 0 once it's gone or reaped.
char stateOf(qint64 pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (pid <= 0 || !stat.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = stat.readAll();
    const qsizetype close = line.lastIndexOf(')');
    const char state = close > 0 && close + 2 < line.size() ? line.at(close + 2) : 0;
    return state == 'Z' ? 0 : state;
}

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

// A plain frame on a shown canvas, streamed by the session's Browser Views.
struct Hosted {
    EditorCanvas canvas;
    QUuid frame;
    Hosted(EditorSession &session, std::optional<QUrl> page = std::nullopt) : canvas(session)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 600, 400}, QStringLiteral("Site"));
        if (page)
            view.browser = BrowserView{*page, {}, {}};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
        canvas.resize(1000, 800);
        canvas.show();
        BrowserViews::of(session)->attach(&canvas);
    }
};
}

#define NEEDS_CHROMIUM_AND_PYTHON \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed."); \
    if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty()) \
        QSKIP("python3 isn't installed, and the fake dev server is Python's.")

class BrowserSwitchTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;
    QStringList m_folders;

    QString project()
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/project%1").arg(++m_sites);
        m_folders.append(folder);
        write(folder + QStringLiteral("/index.html"), pageSaying("dev"));
        write(folder + QStringLiteral("/omastrator.json"),
              QJsonDocument(QJsonObject{{"dev", "exec python3 -u -m http.server 0 --bind 127.0.0.1"}}).toJson());
        return folder;
    }

    QString where(EditorSession &session, const QUuid &frame)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        if (key.isNull())
            return {};
        auto answer = std::make_shared<std::atomic<int>>(0);
        auto text = std::make_shared<QString>();
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"),
                                   {{"expression", "document.getElementById('where') ? document.getElementById('where').textContent : ''"},
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
        BrowserViews::setSignInAnswered(true);
        QFile::remove(ProjectRegistry::path());
    }

    void cleanup()
    {
        for (const QString &folder : std::as_const(m_folders))
            LiveFrames::clearPending(folder);
        m_folders.clear();
        BrowserViews::shutdownPool();
        DevServers::shared().stopAll();
    }

    void onRunsTheServerOffFreezesItOnWakesTheSameOneAndQuittingEndsIt()
    {
        NEEDS_CHROMIUM_AND_PYTHON;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session);
        BrowserViews *views = BrowserViews::of(session);
        QSignalSpy changes(views, &BrowserViews::browserViewChanged);
        // A plain frame: the switch makes it a Browser View, and the address field's answer names the page.
        views->setBrowserViewOn(hosted.frame, true);
        QCOMPARE(session.undoNames().back(), QStringLiteral("Turn On Browser View"));
        QVERIFY(views->browserViewOn(hosted.frame));
        QTRY_VERIFY(!changes.isEmpty());
        QCOMPARE(changes.back().at(1).toBool(), true);
        session.setBrowserUrl(hosted.frame, production.url());

        QTRY_VERIFY_WITH_TIMEOUT(DevServers::shared().running(folder), patience);
        QCOMPARE(DevServers::shared().holders(folder), 1);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
        QTRY_COMPARE_WITH_TIMEOUT(views->server(hosted.frame), BrowserViews::Server::running, patience);
        const qint64 pid = DevServers::shared().processId(folder);
        QVERIFY(pid > 0);
        // The document keeps the production address.
        QCOMPARE(session.document()->find(hosted.frame)->browser->url, production.url());

        // Off: the stream stops, the picture stays, and the server is frozen, not ended.
        views->setBrowserViewOn(hosted.frame, false);
        QCOMPARE(session.undoNames().back(), QStringLiteral("Turn Off Browser View"));
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(pid), 'T', 10'000);
        QVERIFY(DevServers::shared().paused(folder));
        QTRY_COMPARE(views->state(hosted.frame), BrowserViews::State::paused);
        QTRY_COMPARE(views->server(hosted.frame), BrowserViews::Server::paused);
        QCOMPARE(changes.back().at(1).toBool(), false);
        QCOMPARE(DevServers::shared().holders(folder), 1);

        // On again: the same process wakes, and the page streams from it.
        views->setBrowserViewOn(hosted.frame, true);
        QTRY_VERIFY_WITH_TIMEOUT(stateOf(pid) != 'T', 10'000);
        QCOMPARE(DevServers::shared().processId(folder), pid);
        QTRY_COMPARE_WITH_TIMEOUT(views->state(hosted.frame), BrowserViews::State::live, patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);

        // Undo and redo follow the same way, and never start a second server.
        session.undo();
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(pid), 'T', 10'000);
        session.redo();
        QTRY_VERIFY_WITH_TIMEOUT(stateOf(pid) != 'T', 10'000);
        QCOMPARE(DevServers::shared().processId(folder), pid);
        QCOMPARE(DevServers::shared().holders(folder), 1);

        // Frozen at quit: it's woken and ended, and nothing is left behind.
        views->setBrowserViewOn(hosted.frame, false);
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(pid), 'T', 10'000);
        BrowserViews::shutdownPool();
        DevServers::shared().stopAll();
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(pid), char(0), 10'000);
        QVERIFY(!DevServers::shared().running(folder));
    }

    // Opening a file never runs `npm install`: a view saved on shows its page, and its server waits for the switch.
    void aFileOpeningWithTheSwitchOnStartsNoServer()
    {
        NEEDS_CHROMIUM_AND_PYTHON;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());
        EditorSession session;
        Hosted hosted(session, production.url());
        BrowserViews *views = BrowserViews::of(session);
        QTRY_COMPARE_WITH_TIMEOUT(views->state(hosted.frame), BrowserViews::State::live, patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QTest::qWait(500);
        QVERIFY(!DevServers::shared().running(folder));
        QCOMPARE(DevServers::shared().holders(folder), 0);
        // Off, then on, is the designer asking for it.
        views->setBrowserViewOn(hosted.frame, false);
        views->setBrowserViewOn(hosted.frame, true);
        QTRY_VERIFY_WITH_TIMEOUT(DevServers::shared().running(folder), patience);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
    }

    // The app keeps running in the background after its last window closes: the servers stop then, and a frame still on
    // runs its server again when its canvas is seen.
    void theLastWindowClosingStopsTheServersAndShowingItAgainRestartsThem()
    {
        NEEDS_CHROMIUM_AND_PYTHON;
        Production production;
        QVERIFY(production.listen());
        const QString folder = project();
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());
        EditorSession session;
        Hosted hosted(session);
        BrowserViews *views = BrowserViews::of(session);
        views->setBrowserViewOn(hosted.frame, true);
        session.setBrowserUrl(hosted.frame, production.url());
        QTRY_VERIFY_WITH_TIMEOUT(DevServers::shared().running(folder), patience);
        const qint64 pid = DevServers::shared().processId(folder);
        QVERIFY(pid > 0);

        hosted.canvas.hide();
        BrowserViews::stopServers();
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(pid), char(0), 10'000);
        QVERIFY(!DevServers::shared().running(folder));
        QTest::qWait(300);
        QVERIFY(!DevServers::shared().running(folder));

        hosted.canvas.show();
        QTRY_VERIFY_WITH_TIMEOUT(DevServers::shared().running(folder), patience);
        QVERIFY(DevServers::shared().processId(folder) != pid);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("dev"), patience);
    }
};

QTEST_MAIN(BrowserSwitchTests)
#include "BrowserSwitchTests.moc"
