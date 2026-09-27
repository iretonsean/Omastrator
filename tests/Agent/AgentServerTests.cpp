#include "Agent/AgentClient.h"
#include "Agent/AgentServer.h"
#include "Agent/AgentTools.h"
#include "FakeAgentHost.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <sstream>

namespace {
const QString square = QStringLiteral(
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 50'><rect width='100' height='50' fill='#ff0000'/></svg>");

// The app's side, in a thread of its own so the blocking CLI can talk to it.
struct Backend {
    FakeAgentHost host;
    AgentTools tools{host};
    AgentServer server{tools};
};

int code(const QJsonObject &reply)
{
    return reply["error"].toObject()["code"].toInt();
}

std::vector<QJsonObject> lines(const std::string &text)
{
    std::vector<QJsonObject> result;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
        result.push_back(QJsonDocument::fromJson(QByteArray::fromStdString(line)).object());
    return result;
}
}

class AgentServerTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QThread m_thread;
    QObject *m_anchor = nullptr;
    Backend *m_backend = nullptr;
    QString m_path;

    // Runs `work` on the backend's thread and waits for it.
    template<typename Work>
    void onBackend(Work work)
    {
        QMetaObject::invokeMethod(m_anchor, work, Qt::BlockingQueuedConnection);
    }

    int cli(const QStringList &args, QString *out = nullptr, QString *err = nullptr, const std::string &input = {})
    {
        QString output, errors;
        QTextStream outStream(&output), errStream(&errors);
        std::istringstream in(input);
        const int code = AgentClient::runCli(args, outStream, errStream, in);
        outStream.flush();
        errStream.flush();
        if (out)
            *out = output;
        if (err)
            *err = errors;
        return code;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_path = m_directory.filePath(QStringLiteral("omastrator.sock"));
        qputenv("OMASTRATOR_SOCKET", m_path.toUtf8());
        QCOMPARE(AgentProtocol::socketPath(), m_path);
        m_anchor = new QObject;
        m_anchor->moveToThread(&m_thread);
        m_thread.start();
        QString failure;
        onBackend([&] {
            m_backend = new Backend;
            m_backend->host.editor.createDocument({200, 100});
            failure = m_backend->server.listen();
        });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    void cleanupTestCase()
    {
        onBackend([&] { delete m_backend; });
        QMetaObject::invokeMethod(m_anchor, &QObject::deleteLater);
        m_thread.quit();
        m_thread.wait();
        QVERIFY(!QFileInfo::exists(m_path));
    }

    void socketIsTheUsersAlone()
    {
        const QFile::Permissions permissions = QFileInfo(m_path).permissions();
        QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
    }

    void protocolAnswersBadRequests()
    {
        const auto call = [](const QString &, const QJsonObject &) { return QJsonObject{{"ok", true}}; };
        QCOMPARE(code(AgentProtocol::respond("{nope", call)), int(AgentProtocol::parseError));
        QCOMPARE(code(AgentProtocol::respond(R"({"id": 1})", call)), int(AgentProtocol::invalidRequest));
        QCOMPARE(code(AgentProtocol::respond(R"({"jsonrpc": "2.0", "id": 1, "method": "x", "params": [1]})", call)),
                 int(AgentProtocol::invalidParams));
        QVERIFY(AgentProtocol::respond(R"({"jsonrpc": "2.0", "method": "x"})", call).isEmpty());
        const QJsonObject ok = AgentProtocol::respond(R"({"jsonrpc": "2.0", "id": "a", "method": "x"})", call);
        QCOMPARE(ok["id"].toString(), QStringLiteral("a"));
        QVERIFY(ok["result"].toObject()["ok"].toBool());
    }

    void cliRoundTrip()
    {
        QString out, err;
        QCOMPARE(cli({QStringLiteral("document_get")}, &out, &err), 0);
        QCOMPARE(QJsonDocument::fromJson(out.toUtf8()).object()["width"].toDouble(), 200.0);

        const QString params = QString::fromUtf8(QJsonDocument(QJsonObject{{"svg", square}}).toJson(QJsonDocument::Compact));
        QCOMPARE(cli({QStringLiteral("insert_svg"), params}, &out, &err), 0);
        const QUuid group = QUuid::fromString(QJsonDocument::fromJson(out.toUtf8()).object()["id"].toString());
        bool proposed = false;
        onBackend([&] { proposed = m_backend->host.editor.isInteracting() && m_backend->host.editor.document()->find(group); });
        QVERIFY(proposed);

        // Params from stdin, for long SVG.
        QCOMPARE(cli({QStringLiteral("set_style"), QStringLiteral("-")}, &out, &err, "{\"fill\": \"#00ff00\"}"), 0);
        onBackend([&] { proposed = m_backend->host.editor.document()->find(m_backend->host.editor.document()->children(group).front())->fill == Paint::solid(Qt::green); });
        QVERIFY(proposed);
        onBackend([&] { m_backend->host.editor.cancelInteraction(); });
    }

    void cliExplainsItsFailures()
    {
        QString out, err;
        QCOMPARE(cli({}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("Usage")));
        QCOMPARE(cli({QStringLiteral("--help")}, &out, &err), 0);
        QVERIFY(out.contains(QLatin1String("insert_svg")) && out.contains(QLatin1String("show_roast")));
        QCOMPARE(cli({QStringLiteral("insert_svg"), QStringLiteral("--help")}, &out, &err), 0);
        QVERIFY(out.contains(QLatin1String("svg (string, required)")));
        QCOMPARE(cli({QStringLiteral("frobnicate")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("frobnicate")));
        QCOMPARE(cli({QStringLiteral("set_style"), QStringLiteral("{bad")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("JSON object")));
        QCOMPARE(cli({QStringLiteral("set_style"), QStringLiteral("{}")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("Nothing is selected")));
        QVERIFY(out.isEmpty());

        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("nobody.sock")).toUtf8());
        QCOMPARE(cli({QStringLiteral("document_get")}, &out, &err), 1);
        QVERIFY(err.contains(QLatin1String("not running")));
        qputenv("OMASTRATOR_SOCKET", m_path.toUtf8());
    }

    void clientsTakeTurns()
    {
        AgentClient::Connection first, second;
        for (int round = 0; round < 3; ++round) {
            QCOMPARE(first.call(QStringLiteral("document_get"), {})["height"].toDouble(), 100.0);
            QCOMPARE(second.call(QStringLiteral("selection_get"), {})["selection"].toArray().size(), 0);
        }
        // A raw client sending two requests in one write gets both answers.
        QLocalSocket raw;
        raw.connectToServer(m_path);
        QVERIFY(raw.waitForConnected(1000));
        raw.write(AgentProtocol::frame(AgentProtocol::request(7, QStringLiteral("document_get"), {}))
                  + AgentProtocol::frame(AgentProtocol::request(8, QStringLiteral("nope"), {})));
        QByteArray received;
        while (received.count('\n') < 2 && raw.waitForReadyRead(2000))
            received += raw.readAll();
        const std::vector<QJsonObject> replies = lines(received.toStdString());
        QCOMPARE(replies.size(), size_t(2));
        QCOMPARE(replies[0]["id"].toInt(), 7);
        QCOMPARE(code(replies[1]), int(AgentProtocol::methodNotFound));
    }

    void staleSocketsAreReplacedAndLiveOnesKept()
    {
        const QString stale = m_directory.filePath(QStringLiteral("stale.sock"));
        QFile file(stale);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QString failure;
        onBackend([&] {
            AgentServer other(m_backend->tools);
            failure = other.listen(stale);
        });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        onBackend([&] {
            AgentServer other(m_backend->tools);
            failure = other.listen(m_path);
        });
        QVERIFY(failure.contains(QLatin1String("already listening")));
        QVERIFY(QFileInfo::exists(m_path));
    }

    void mcpOverTheSocket()
    {
        std::string input;
        for (const QJsonObject &message : {
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                             {"params", QJsonObject{{"protocolVersion", "2025-06-18"}, {"capabilities", QJsonObject()},
                                                    {"clientInfo", QJsonObject{{"name", "test"}, {"version", "1"}}}}}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                             {"params", QJsonObject{{"name", "insert_svg"}, {"arguments", QJsonObject{{"svg", square}}}}}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/call"},
                             {"params", QJsonObject{{"name", "transform"}, {"arguments", QJsonObject()}}}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 5}, {"method", "tools/call"},
                             {"params", QJsonObject{{"name", "render"}, {"arguments", QJsonObject{{"selectionOnly", true}}}}}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 6}, {"method", "tools/call"}, {"params", QJsonObject{{"name", "nope"}}}},
                 QJsonObject{{"jsonrpc", "2.0"}, {"id", 7}, {"method", "resources/list"}},
             })
            input += AgentProtocol::frame(message).toStdString();
        std::istringstream in(input);
        std::ostringstream out;
        QCOMPARE(AgentClient::serveMcp(in, out), 0);
        const std::vector<QJsonObject> replies = lines(out.str());
        QCOMPARE(replies.size(), size_t(7));

        const QJsonObject initialize = replies[0]["result"].toObject();
        QCOMPARE(initialize["protocolVersion"].toString(), QStringLiteral("2025-06-18"));
        QVERIFY(initialize["capabilities"].toObject().contains("tools"));
        QCOMPARE(initialize["serverInfo"]["name"].toString(), QStringLiteral("omastrator"));

        const QJsonArray tools = replies[1]["result"]["tools"].toArray();
        QCOMPARE(tools.size(), std::count_if(AgentProtocol::methods().begin(), AgentProtocol::methods().end(),
                                             [](const AgentProtocol::Method &each) { return each.mcp; }));
        for (const QJsonValue &tool : tools)
            QVERIFY(tool["name"].toString() != QLatin1String("apply_color"));
        for (const QJsonValue &tool : tools) {
            QCOMPARE(tool["inputSchema"]["type"].toString(), QStringLiteral("object"));
            QVERIFY(!tool["description"].toString().isEmpty());
        }

        const QJsonObject inserted = replies[2]["result"].toObject();
        QVERIFY(!inserted["isError"].toBool());
        const QJsonObject payload = QJsonDocument::fromJson(inserted["content"][0]["text"].toString().toUtf8()).object();
        QVERIFY(!QUuid::fromString(payload["id"].toString()).isNull());

        const QJsonObject failed = replies[3]["result"].toObject();
        QVERIFY(failed["isError"].toBool());
        QVERIFY(failed["content"][0]["text"].toString().contains(QLatin1String("translate")));

        const QJsonArray rendered = replies[4]["result"]["content"].toArray();
        QCOMPARE(rendered.size(), 2);
        QCOMPARE(rendered[1]["type"].toString(), QStringLiteral("image"));
        QVERIFY(!QImage::fromData(QByteArray::fromBase64(rendered[1]["data"].toString().toLatin1()), "PNG").isNull());
        QFile::remove(QJsonDocument::fromJson(rendered[0]["text"].toString().toUtf8()).object()["path"].toString());

        QCOMPARE(code(replies[5]), int(AgentProtocol::invalidParams));
        QCOMPARE(code(replies[6]), int(AgentProtocol::methodNotFound));
        onBackend([&] { m_backend->host.editor.cancelInteraction(); });
    }

    void mcpListsToolsWithoutTheApp()
    {
        const QJsonObject reply = AgentClient::answerMcp(R"({"jsonrpc": "2.0", "id": 1, "method": "tools/list"})",
                                                         [](const QString &, const QJsonObject &) -> QJsonObject {
                                                             throw AgentProtocol::Error(AgentProtocol::notRunning, QStringLiteral("down"));
                                                         });
        QVERIFY(!reply["result"]["tools"].toArray().isEmpty());
        const QJsonObject call = AgentClient::answerMcp(R"({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "document_get"}})",
                                                        [](const QString &, const QJsonObject &) -> QJsonObject {
                                                            throw AgentProtocol::Error(AgentProtocol::notRunning, QStringLiteral("down"));
                                                        });
        QVERIFY(call["result"]["isError"].toBool());
        QCOMPARE(call["result"]["content"][0]["text"].toString(), QStringLiteral("down"));
    }
};

QTEST_MAIN(AgentServerTests)
#include "AgentServerTests.moc"
