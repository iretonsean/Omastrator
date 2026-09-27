#include "Agent/StatusStream.h"
#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace StatusStream {
QJsonObject compose(const QJsonObject &app, const Island::State &island)
{
    // What the island reads when the app is closed: nothing waiting, nothing ready.
    QJsonObject status{{"running", false}, {"document", false}, {"tool", "select"}, {"proposal", ""}, {"summary", ""},
                       {"waiting", ""}, {"task", ""}, {"agent", ""}, {"variations", 0}, {"variationsId", ""},
                       {"roastId", ""}, {"offer", ""}, {"ready", false}, {"error", ""}, {"live", QJsonObject{{"state", "off"}}},
                       {"design", QJsonObject{{"on", false}, {"overlays", QJsonArray()}}}, {"window", false}};
    for (auto it = app.begin(); it != app.end(); ++it)
        status.insert(it.key(), it.value());
    // Dictation's state, from its own file beside the island's.
    QFile dictation(QDir(Island::runtimeDirectory()).filePath(QStringLiteral("dictation.json")));
    const QJsonObject heard = dictation.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(dictation.readAll()).object() : QJsonObject();
    status.insert(QStringLiteral("dictation"), heard["state"].toString(QStringLiteral("idle")));
    const QJsonObject own = island.toJson();
    for (auto it = own.begin(); it != own.end(); ++it)
        status.insert(it.key(), it.value());
    return status;
}

Follower::Follower(QTextStream &out, QObject *parent) : QObject(parent), m_out(out)
{
    m_retry.setInterval(500);
    connect(&m_retry, &QTimer::timeout, this, &Follower::connectToApp);
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        m_socket.write(AgentProtocol::frame(AgentProtocol::request(1, QStringLiteral("status_follow"), {})));
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, &Follower::readApp);
    connect(&m_socket, &QLocalSocket::disconnected, this, &Follower::appGone);
    connect(&m_socket, &QLocalSocket::stateChanged, this, [this](QLocalSocket::LocalSocketState state) {
        if (state == QLocalSocket::UnconnectedState)
            appGone();
    });
    // The state files are replaced whole, so watch their folders.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Follower::print);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &Follower::print);
}

Follower::~Follower()
{
    // The socket goes last and says it disconnected; nobody is left to hear it.
    m_socket.disconnect(this);
}

void Follower::start()
{
    for (const QString &path : {Island::statePath(), Island::seenPath()}) {
        const QString folder = QFileInfo(path).absolutePath();
        QDir().mkpath(folder);
        m_watcher.addPath(folder);
    }
    print();
    // Retries while the app is closed; a no-op while connected.
    m_retry.start();
    connectToApp();
}

void Follower::connectToApp()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_buffer.clear();
    m_socket.connectToServer(AgentProtocol::socketPath());
}

void Follower::appGone()
{
    if (!m_app.isEmpty()) {
        m_app = {};
        print();
    }
}

void Follower::readApp()
{
    m_buffer += m_socket.readAll();
    qsizetype end;
    bool changed = false;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QJsonObject message = QJsonDocument::fromJson(m_buffer.left(end)).object();
        m_buffer.remove(0, end + 1);
        if (message.contains("result")) {
            m_app = message["result"].toObject();
            changed = true;
        } else if (message["method"].toString() == QLatin1String("status")) {
            m_app = message["params"].toObject();
            changed = true;
        }
    }
    if (changed)
        print();
}

void Follower::print()
{
    const QJsonObject status = compose(m_app, Island::read());
    if (status == m_last)
        return;
    m_last = status;
    m_out << QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact)) << '\n';
    m_out.flush();
    emit printed(status);
}

int runCli(const QStringList &args, QTextStream &out, QTextStream &err)
{
    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        out << "Usage: omastrator status [--follow]\n\n"
               "Prints what Omastrator and its island are doing as one JSON line.\n"
               "With --follow, prints a new line each time it changes, until killed.\n";
        return 0;
    }
    if (args.contains(QStringLiteral("--follow"))) {
        Follower follower(out);
        follower.start();
        return QCoreApplication::exec();
    }
    if (!args.isEmpty()) {
        err << QStringLiteral("status takes only --follow.\n");
        return 1;
    }
    QJsonObject app;
    if (Island::appIsRunning()) {
        try {
            AgentClient::Connection connection;
            app = connection.call(QStringLiteral("status_get"), {}, 5000);
        } catch (const AgentProtocol::Error &) {
            // Gone between the probe and the call: report it as closed.
        }
    }
    out << QString::fromUtf8(QJsonDocument(compose(app, Island::read())).toJson(QJsonDocument::Compact)) << '\n';
    return 0;
}
}
