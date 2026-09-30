#include "Agent/StatusStream.h"
#include "Agent/AgentClient.h"
#include "Agent/AgentProtocol.h"
#include "Agent/WorkspaceClaims.h"
#include <QCoreApplication>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace StatusStream {
// The pages that hold a workspace, in page order, for the bar's page dots. A claims file from a version that didn't
// record the names gives them back from the workspace name, `design:<document> · <page>`.
static QJsonArray pageWorkspaces()
{
    QJsonArray pages;
    for (const WorkspaceClaims::Claim &claim : WorkspaceClaims::read().claims) {
        QString document = claim.document, page = claim.pageName;
        if (document.isEmpty() || page.isEmpty()) {
            const QString plain = claim.name.startsWith(QLatin1String("design:")) ? claim.name.mid(7) : claim.name;
            const qsizetype split = plain.indexOf(QStringLiteral(" · "));
            document = split < 0 ? QString() : plain.left(split);
            page = split < 0 ? plain : plain.mid(split + 3);
        }
        pages.append(QJsonObject{{"name", claim.name}, {"page", page}, {"document", document}});
    }
    return pages;
}

QJsonObject compose(const QJsonObject &app, const Island::State &island)
{
    // What a reader gets when the app is closed: nothing waiting, nothing ready.
    QJsonObject status{{"running", false}, {"document", false}, {"tool", "select"}, {"proposal", ""}, {"summary", ""},
                       {"waiting", ""}, {"task", ""}, {"agent", ""}, {"variations", 0}, {"variationsId", ""},
                       {"roastId", ""}, {"offer", ""}, {"ready", false}, {"error", ""}, {"live", QJsonObject{{"state", "off"}}},
                       {"design", QJsonObject{{"on", false}, {"overlays", QJsonArray()}}}, {"window", false},
                       {"pageWorkspaces", QJsonArray()}};
    for (auto it = app.begin(); it != app.end(); ++it)
        status.insert(it.key(), it.value());
    // The claims file belongs to a running app: one left by a crashed app is not shown.
    if (!app.isEmpty())
        status.insert(QStringLiteral("pageWorkspaces"), pageWorkspaces());
    // Dictation's state, from its own file beside the mode's.
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
    const QString folder = QFileInfo(Island::statePath()).absolutePath();
    QDir().mkpath(folder);
    m_watcher.addPath(folder);
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
               "Prints what Omastrator is doing (with the mode and the last activity line) as one JSON line.\n"
               "With --follow, prints a new line each time it changes, until killed.\n";
        return 0;
    }
    if (args.contains(QStringLiteral("--follow"))) {
        // Goes when whatever reads it goes (the shell restarting), instead of lingering as an orphan.
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() == 1)
            return 0;
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
