#include "Live/DevServer.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
void pause(int ms)
{
    QEventLoop wait;
    QTimer::singleShot(ms, &wait, &QEventLoop::quit);
    wait.exec();
}
}

std::optional<DevCommand> DevCommand::detect(const QString &folder, QString *why)
{
    const QDir dir(folder);
    if (!dir.exists()) {
        *why = QStringLiteral("%1 doesn't exist.").arg(folder);
        return std::nullopt;
    }
    DevCommand command;
    QFile override(dir.filePath(QStringLiteral("omastrator.json")));
    if (override.open(QIODevice::ReadOnly)) {
        const QJsonObject json = QJsonDocument::fromJson(override.readAll()).object();
        if (!json["dev"].toString().trimmed().isEmpty()) {
            command.kind = Kind::override;
            command.program = QStringLiteral("/bin/sh");
            command.arguments = {QStringLiteral("-c"), json["dev"].toString()};
            command.description = json["dev"].toString();
            command.url = QUrl(json["url"].toString());
            return command;
        }
    }
    QFile package(dir.filePath(QStringLiteral("package.json")));
    if (package.open(QIODevice::ReadOnly)) {
        const QJsonObject json = QJsonDocument::fromJson(package.readAll()).object();
        const QJsonObject scripts = json["scripts"].toObject();
        const QString script = scripts.contains("dev") ? QStringLiteral("dev") : scripts.contains("start") ? QStringLiteral("start") : QString();
        if (!script.isEmpty()) {
            // The lockfile says which package manager the project uses.
            QString manager = QStringLiteral("npm");
            if (dir.exists(QStringLiteral("pnpm-lock.yaml")))
                manager = QStringLiteral("pnpm");
            else if (dir.exists(QStringLiteral("yarn.lock")))
                manager = QStringLiteral("yarn");
            else if (dir.exists(QStringLiteral("bun.lockb")) || dir.exists(QStringLiteral("bun.lock")))
                manager = QStringLiteral("bun");
            const QString program = QStandardPaths::findExecutable(manager);
            if (program.isEmpty()) {
                *why = QStringLiteral("This project runs with %1, which isn't installed.").arg(manager);
                return std::nullopt;
            }
            command.kind = Kind::script;
            command.program = program;
            command.arguments = {QStringLiteral("run"), script};
            command.description = QStringLiteral("%1 run %2").arg(manager, script);
            const bool needsPackages = !json["dependencies"].toObject().isEmpty() || !json["devDependencies"].toObject().isEmpty();
            if (needsPackages && !dir.exists(QStringLiteral("node_modules")))
                command.install = {QStringLiteral("install")};
            return command;
        }
    }
    if (dir.exists(QStringLiteral("index.html"))) {
        command.kind = Kind::staticSite;
        command.description = QStringLiteral("Omastrator serving the folder");
        return command;
    }
    *why = QStringLiteral("%1 has no dev script in package.json, no omastrator.json and no index.html, so there's nothing to open.")
               .arg(QFileInfo(folder).fileName());
    return std::nullopt;
}

DevServer::DevServer(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyRead, this, [this] {
        // Enough to find the URL and show what went wrong, not the whole session.
        m_output += m_process.readAll();
        if (m_output.size() > 256 * 1024)
            m_output = m_output.right(128 * 1024);
    });
    connect(&m_process, &QProcess::finished, this, &DevServer::exited);
    // Its own process group, so stopping it stops what the script started too.
    m_process.setChildProcessModifier([] {
        ::setsid();
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
}

DevServer::~DevServer()
{
    m_process.disconnect(this);
    stop();
}

QUrl DevServer::urlIn(const QString &output)
{
    static const QRegularExpression ansi(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]"));
    static const QRegularExpression local(QStringLiteral(R"(https?://(?:localhost|127\.0\.0\.1|0\.0\.0\.0|\[::1\])(?::\d+)?[^\s'"<>]*)"));
    QString clean = output;
    clean.remove(ansi);
    const auto match = local.match(clean);
    if (!match.hasMatch())
        return {};
    QUrl url(match.captured(0));
    if (url.host() == QLatin1String("0.0.0.0"))
        url.setHost(QStringLiteral("127.0.0.1"));
    return url;
}

bool DevServer::answers(const QUrl &url, int timeoutMs)
{
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setTransferTimeout(timeoutMs);
    QNetworkReply *reply = network.head(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs + 200, &loop, &QEventLoop::quit);
    loop.exec();
    // Any HTTP answer, even a 404, means the server is up.
    const bool up = reply->isFinished() && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
    reply->abort();
    reply->deleteLater();
    return up;
}

QString DevServer::start(const QString &folder, int timeoutMs)
{
    stop();
    QString why;
    const auto detected = DevCommand::detect(folder, &why);
    if (!detected)
        return why;
    m_command = *detected;
    m_output.clear();
    if (m_command.kind == DevCommand::Kind::staticSite) {
        if (const QString failure = m_static.serve(folder); !failure.isEmpty())
            return failure;
        m_url = m_static.url();
        return {};
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Dev servers must not open the user's own browser, nor colour their output.
    environment.insert(QStringLiteral("BROWSER"), QStringLiteral("none"));
    environment.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    environment.insert(QStringLiteral("FORCE_COLOR"), QStringLiteral("0"));
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(folder);
    if (!m_command.install.isEmpty())
        if (const QString failure = install(folder); !failure.isEmpty())
            return failure;
    m_output.clear();
    emit step(QStringLiteral("Starting the project (%1)…").arg(m_command.description));
    m_process.start(m_command.program, m_command.arguments);
    if (!m_process.waitForStarted(10'000))
        return QStringLiteral("Could not start %1: %2").arg(m_command.description, m_process.errorString());
    for (int waited = 0; waited < timeoutMs; waited += 250) {
        if (m_process.state() == QProcess::NotRunning) {
            const QString tail = output().right(600).trimmed();
            return QStringLiteral("%1 stopped before it served anything.%2").arg(m_command.description, tail.isEmpty() ? QString() : QStringLiteral("\n") + tail);
        }
        const QUrl url = m_command.url.isValid() && !m_command.url.isEmpty() ? m_command.url : urlIn(output());
        if (url.isValid() && !url.isEmpty() && answers(url, 1000)) {
            m_url = url;
            return {};
        }
        pause(250);
    }
    stop();
    return QStringLiteral("%1 didn't answer within %2 seconds.").arg(m_command.description, QString::number(timeoutMs / 1000));
}

QString DevServer::install(const QString &folder)
{
    const QString what = QStringLiteral("%1 %2").arg(QFileInfo(m_command.program).fileName(), m_command.install.join(QLatin1Char(' ')));
    emit step(QStringLiteral("Installing %1's packages (%2)…").arg(QDir(folder).dirName(), what));
    m_process.start(m_command.program, m_command.install);
    if (!m_process.waitForStarted(10'000))
        return QStringLiteral("Could not start %1: %2").arg(what, m_process.errorString());
    // Pausing, not blocking, so a stop() while it installs ends it.
    for (int waited = 0; m_process.state() != QProcess::NotRunning; waited += 250) {
        if (waited >= 600'000) {
            stop();
            return QStringLiteral("%1 didn't finish within 10 minutes.").arg(what);
        }
        pause(250);
    }
    if (m_process.exitStatus() != QProcess::NormalExit || m_process.exitCode() != 0) {
        const QString tail = output().right(600).trimmed();
        return QStringLiteral("%1 failed, so the project can't start.%2").arg(what, tail.isEmpty() ? QString() : QStringLiteral("\n") + tail);
    }
    return {};
}

void DevServer::stop()
{
    m_static.stop();
    if (m_process.state() != QProcess::NotRunning) {
        const qint64 group = m_process.processId();
        if (group > 0)
            ::kill(-pid_t(group), SIGTERM);
        if (!m_process.waitForFinished(5000)) {
            if (group > 0)
                ::kill(-pid_t(group), SIGKILL);
            m_process.waitForFinished(2000);
        }
    }
    m_url.clear();
}
