#include "Live/Browser.h"
#include "Live/BrowserLink.h"
#include "Live/LiveSession.h"
#include "Live/StaticServer.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

// Live in the user's own Chromium (docs/OS-SUITE.md): a stand-in for the
// extension relays the browser link's DevTools messages to headless Chromium,
// as chrome.debugger does on a real tab.
namespace {
class FakeExtension : public QObject {
public:
    FakeExtension(Browser &browser, const QString &socket) : m_browser(browser)
    {
        connect(&m_socket, &QLocalSocket::readyRead, this, [this] {
            m_buffer += m_socket.readAll();
            for (qsizetype end; (end = m_buffer.indexOf('\n')) >= 0;) {
                const QJsonObject message = QJsonDocument::fromJson(m_buffer.left(end)).object();
                m_buffer.remove(0, end + 1);
                if (message["type"] == QLatin1String("status"))
                    status = message["status"].toObject();
                else if (message["type"] == QLatin1String("cdp"))
                    run(message);
            }
        });
        connect(&m_browser.cdp(), &CdpConnection::event, this, [this](const QString &method, const QJsonObject &params, const QString &sessionId) {
            if (m_sessions.contains(sessionId))
                send({{"type", "cdp"}, {"method", method}, {"params", params}, {"sessionId", sessionId}});
        });
        m_socket.connectToServer(socket);
        m_socket.waitForConnected(2000);
        send({{"type", "hello"}, {"pid", 4242}, {"version", "test"}});
    }

    void send(const QJsonObject &message)
    {
        m_socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        m_socket.flush();
    }
    // What Chromium's "is debugging" bar's Cancel does.
    void cancel()
    {
        for (const QString &session : std::exchange(m_sessions, {}))
            send({{"type", "cdp"}, {"method", "Omastrator.detached"}, {"params", QJsonObject{{"sessionId", session}, {"reason", "canceled_by_user"}}}});
    }
    void quit() { m_socket.disconnectFromServer(); }
    bool attached() const { return !m_sessions.isEmpty(); }

    QJsonObject status;

private:
    void run(const QJsonObject &message)
    {
        const QJsonValue id = message["id"];
        auto answer = [this, id](const QJsonObject &result, const QString &error) {
            QJsonObject reply{{"type", "cdp"}, {"id", id}};
            if (error.isEmpty())
                reply["result"] = result;
            else
                reply["error"] = QJsonObject{{"message", error}};
            send(reply);
        };
        const QString method = message["method"].toString();
        CdpConnection &cdp = m_browser.cdp();
        QString error;
        if (method == QLatin1String("Omastrator.attach")) {
            QJsonObject page;
            for (const QJsonValue &target : cdp.callAndWait(QStringLiteral("Target.getTargets"), {}, {}, &error)["targetInfos"].toArray())
                if (target["type"] == QLatin1String("page"))
                    page = target.toObject();
            const QString session =
                cdp.callAndWait(QStringLiteral("Target.attachToTarget"), {{"targetId", page["targetId"]}, {"flatten", true}}, {}, &error)["sessionId"].toString();
            if (error.isEmpty())
                m_sessions.insert(session);
            return answer({{"tabId", 7}, {"sessionId", session}, {"url", page["url"]}, {"title", page["title"]}}, error);
        }
        if (method == QLatin1String("Omastrator.detach")) {
            const QString session = message["params"]["sessionId"].toString();
            m_sessions.remove(session);
            cdp.callAndWait(QStringLiteral("Target.detachFromTarget"), {{"sessionId", session}}, {}, &error);
            return answer({}, {});
        }
        cdp.call(method, message["params"].toObject(), message["sessionId"].toString(), answer);
    }

    Browser &m_browser;
    QLocalSocket m_socket;
    QByteArray m_buffer;
    QSet<QString> m_sessions;
};
}

class LiveTabTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_fixtures = QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures");

    bool waitFor(LiveSession &live, LiveSession::State state, int timeoutMs = 30'000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < timeoutMs && live.state() != state && live.state() != LiveSession::State::failed)
            QTest::qWait(50);
        return live.state() == state;
    }

    // Asked in the browser's own session, which Live never touches: what the user sees.
    QJsonValue page(Browser &browser, const Browser::Page &tab, const QString &expression)
    {
        QString error;
        const QJsonObject result = browser.cdp().callAndWait(QStringLiteral("Runtime.evaluate"), {{"expression", expression}, {"returnByValue", true}},
                                                             tab.sessionId, &error);
        return result["result"].toObject()["value"];
    }

private slots:
    void initTestCase()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed, so Live in a tab can't be tested here.");
        QVERIFY(m_directory.isValid());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
    }

    void yourOwnTabJoinsAndLeaves()
    {
        StaticServer server;
        QVERIFY(server.serve(m_fixtures + QStringLiteral("/plain")).isEmpty());
        Browser browser;
        Browser::Options options;
        options.headless = true;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        QVERIFY(browser.start(options).isEmpty());
        QString error;
        const auto tab = browser.attachPage(server.url(), &error);
        QVERIFY2(tab, qPrintable(error));
        // The user's own state in the page: Live must not reload it.
        page(browser, *tab, QStringLiteral("window.userState = 'kept'"));

        BrowserLink link;
        QVERIFY(link.listen(m_directory.filePath(QStringLiteral("browser.sock"))).isEmpty());
        link.setStatus([] { return QJsonObject{{"live", QJsonObject{{"state", "test"}}}}; });
        LiveSession live;
        live.setBrowserLink(&link);
        LiveSession::Target target;
        target.tab = 0;
        QVERIFY(live.start(target).contains(QLatin1String("extension isn't connected")));

        auto extension = std::make_unique<FakeExtension>(browser, m_directory.filePath(QStringLiteral("browser.sock")));
        QTRY_VERIFY(link.isConnected());
        QCOMPARE(link.chromiumPid(), 4242);
        QTRY_COMPARE(extension->status["live"].toObject()["state"].toString(), QStringLiteral("test"));

        // With its code folder: the user's site, not a mock-up.
        const QString site = m_fixtures + QStringLiteral("/plain");
        target.folder = site;
        QVERIFY(live.start(target).isEmpty());
        QVERIFY2(waitFor(live, LiveSession::State::running), qPrintable(live.message()));
        QVERIFY(live.inUserBrowser());
        QCOMPARE(live.browserProcessId(), 4242);
        QVERIFY(!live.isMockup());
        QCOMPARE(live.url(), server.url());
        QCOMPARE(page(browser, *tab, QStringLiteral("window.userState")).toString(), QStringLiteral("kept"));
        QCOMPARE(page(browser, *tab, QStringLiteral("typeof window.__oma")).toString(), QStringLiteral("object"));
        QVERIFY(live.edit(QStringLiteral("#title"), QStringLiteral("color"), QStringLiteral("#e3204a")).isEmpty());
        QCOMPARE(live.edits().size(), size_t(1));
        QCOMPARE(page(browser, *tab, QStringLiteral("getComputedStyle(document.querySelector('#title')).color")).toString(), QStringLiteral("rgb(225, 29, 72)"));

        // Stop: the overlay leaves the page, the tab is let go, the page stays as the user had it.
        live.stop();
        QCOMPARE(live.state(), LiveSession::State::off);
        QVERIFY(!live.inUserBrowser());
        QTRY_VERIFY(!extension->attached());
        QCOMPARE(page(browser, *tab, QStringLiteral("typeof window.__oma")).toString(), QStringLiteral("undefined"));
        QCOMPARE(page(browser, *tab, QStringLiteral("window.userState")).toString(), QStringLiteral("kept"));

        // Chromium's bar's Cancel ends Live.
        QVERIFY(live.start(target).isEmpty());
        QVERIFY2(waitFor(live, LiveSession::State::running), qPrintable(live.message()));
        extension->cancel();
        QTRY_COMPARE(live.state(), LiveSession::State::off);
        QVERIFY(live.message().contains(QLatin1String("Chromium's bar")));

        // So does Chromium (or the extension) going away.
        QVERIFY(live.start(target).isEmpty());
        QVERIFY2(waitFor(live, LiveSession::State::running), qPrintable(live.message()));
        extension->quit();
        QTRY_COMPARE(live.state(), LiveSession::State::off);
        QVERIFY(!link.isConnected());
        QVERIFY(live.start(target).contains(QLatin1String("extension isn't connected")));
    }

    void callsRunOmastratorsMethods()
    {
        BrowserLink link;
        const QString path = m_directory.filePath(QStringLiteral("calls.sock"));
        QVERIFY(link.listen(path).isEmpty());
        link.setCaller([](const QString &method, const QJsonObject &params, QString *error) {
            if (method == QLatin1String("fail"))
                *error = QStringLiteral("No.");
            return QJsonObject{{"echo", params["value"]}};
        });
        QLocalSocket socket;
        socket.connectToServer(path);
        QVERIFY(socket.waitForConnected(2000));
        socket.write("{\"type\":\"hello\",\"pid\":1}\n{\"type\":\"call\",\"id\":3,\"method\":\"live\",\"params\":{\"value\":5}}\n"
                     "{\"type\":\"call\",\"id\":4,\"method\":\"fail\"}\n");
        QByteArray received;
        QTRY_VERIFY((received += socket.readAll()).count('\n') >= 2);
        const QList<QByteArray> lines = received.trimmed().split('\n');
        QCOMPARE(QJsonDocument::fromJson(lines[0]).object()["result"].toObject()["echo"].toInt(), 5);
        QCOMPARE(QJsonDocument::fromJson(lines[1]).object()["error"].toString(), QStringLiteral("No."));
        // A second host (a restarted Chromium) replaces the first.
        QLocalSocket second;
        second.connectToServer(path);
        QVERIFY(second.waitForConnected(2000));
        second.write("{\"type\":\"hello\",\"pid\":2}\n");
        QTRY_COMPARE(link.chromiumPid(), 2);
        QTRY_COMPARE(socket.state(), QLocalSocket::UnconnectedState);
    }
};

QTEST_GUILESS_MAIN(LiveTabTests)
#include "LiveTabTests.moc"
