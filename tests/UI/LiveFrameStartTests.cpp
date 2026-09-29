#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Registry.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
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

    // A project folder that the shared static server can serve, with an optional dev override.
    QString project(const QString &dev = QString())
    {
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/project%1").arg(++m_sites);
        write(folder + QStringLiteral("/index.html"), pageSaying("dev"));
        if (!dev.isEmpty())
            write(folder + QStringLiteral("/omastrator.json"), QJsonDocument(QJsonObject{{"dev", dev}}).toJson());
        return folder;
    }

    // Reads the tab's page straight off the pool, so it works whether or not Live is running on the frame.
    QString where(EditorSession &session, const QUuid &frame)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        if (key.isNull())
            return {};
        auto answer = std::make_shared<std::atomic<int>>(0);
        auto text = std::make_shared<QString>();
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"),
                                   {{"expression", QStringLiteral("document.getElementById('where') ? document.getElementById('where').textContent : ''")},
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
        BrowserViews::setSignInAnswered(false);
        BrowserViews::setFolderChooser({});
    }

    void cleanup()
    {
        LiveFrames::clearPending(QString());
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

        frames->stop(hosted.frame);
        QTRY_COMPARE_WITH_TIMEOUT(where(session, hosted.frame), QStringLiteral("production"), patience);
        QVERIFY(!host->bar(hosted.frame).dev);
        QCOMPARE(session.document()->find(hosted.frame)->browser->url, production.url());
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
        QVERIFY(offered().isEmpty());
    }
};

QTEST_MAIN(LiveFrameStartTests)
#include "LiveFrameStartTests.moc"
