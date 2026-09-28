#include "Live/Browser.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <csignal>
#include <sys/prctl.h>

Browser::Browser(QObject *parent) : QObject(parent)
{
    // Omastrator's browser goes when Omastrator does, even if it crashes.
    m_process.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        if (m_draining)
            m_process.readAllStandardError();
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        m_cdp.close();
        emit exited();
    });
}

Browser::~Browser()
{
    m_process.disconnect(this);
    stop();
}

QString Browser::executable()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_CHROMIUM");
    if (!overridden.isEmpty())
        return QStandardPaths::findExecutable(overridden).isEmpty() ? overridden : QStandardPaths::findExecutable(overridden);
    for (const char *name : {"chromium", "chromium-browser", "google-chrome-stable", "google-chrome"}) {
        const QString found = QStandardPaths::findExecutable(QLatin1String(name));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

QString Browser::defaultProfile()
{
    const QString given = qEnvironmentVariable("XDG_DATA_HOME");
    const QString data = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".local/share")) : given;
    return QDir(data).filePath(QStringLiteral("omastrator/browser"));
}

bool Browser::isRunning() const
{
    return m_process.state() == QProcess::Running && m_cdp.isOpen();
}

QString Browser::start(const Options &options)
{
    stop();
    const bool electron = !options.program.isEmpty();
    const QString program = electron ? options.program : executable();
    if (program.isEmpty())
        return QStringLiteral("Chromium isn't installed. Install it with: sudo pacman -S chromium");
    m_profile = options.profile.isEmpty() ? defaultProfile() : options.profile;
    QDir().mkpath(m_profile);
    const QString portFile = QDir(m_profile).filePath(QStringLiteral("DevToolsActivePort"));
    QFile::remove(portFile);
    // Port 0 is a random free port, and DevTools answers on localhost only.
    QStringList arguments{QStringLiteral("--user-data-dir=") + m_profile, QStringLiteral("--remote-debugging-port=0"),
                          QStringLiteral("--remote-debugging-address=127.0.0.1")};
    if (electron) {
        arguments = options.programArguments + arguments;
    } else {
        arguments << QStringLiteral("--no-first-run") << QStringLiteral("--no-default-browser-check") << QStringLiteral("--disable-sync");
        if (options.headless)
            // Headless is tests (docs/OS-SUITE.md): keep the profile small, since it usually lives in a QTemporaryDir.
            arguments << QStringLiteral("--headless=new") << QStringLiteral("--window-size=1280,800") << QStringLiteral("--disk-cache-size=1")
                      << QStringLiteral("--media-cache-size=1") << QStringLiteral("--disable-gpu-shader-disk-cache");
        arguments << options.extraArguments;
        arguments << (options.app.isValid() ? QStringLiteral("--app=") + options.app.toString() : QStringLiteral("about:blank"));
    }
    m_draining = false;
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.start(program, arguments);
    if (!m_process.waitForStarted(10'000))
        return QStringLiteral("Could not start %1: %2").arg(program, m_process.errorString());
    // The socket is announced on stderr, and written to DevToolsActivePort; whichever comes first.
    static const QRegularExpression announcement(QStringLiteral("DevTools listening on (ws://\\S+)"));
    QByteArray printed;
    QUrl socket;
    for (int waited = 0; waited < 20'000 && socket.isEmpty(); waited += 100) {
        printed += m_process.readAllStandardError();
        if (const auto match = announcement.match(QString::fromUtf8(printed)); match.hasMatch())
            socket = QUrl(match.captured(1));
        QFile file(portFile);
        if (socket.isEmpty() && file.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> lines = file.readAll().split('\n');
            if (lines.size() >= 2 && !lines[0].trimmed().isEmpty() && !lines[1].trimmed().isEmpty())
                socket = QUrl(QStringLiteral("ws://127.0.0.1:%1%2").arg(QString::fromLatin1(lines[0].trimmed()), QString::fromLatin1(lines[1].trimmed())));
        }
        if (!socket.isEmpty())
            break;
        if (m_process.state() != QProcess::Running)
            return electron ? QStringLiteral("%1 quit as it started, without opening DevTools. It may not be an Electron app.").arg(program)
                            : QStringLiteral("Chromium quit as it started. Another Chromium may be using Omastrator's profile at %1.").arg(m_profile);
        QEventLoop wait;
        QTimer::singleShot(100, &wait, &QEventLoop::quit);
        wait.exec();
    }
    // Nobody reads it from here on; unread output mustn't pile up.
    m_draining = true;
    if (socket.isEmpty())
        return electron ? QStringLiteral("%1 didn't open DevTools. Only Chromium-based apps (Electron, web apps) can be edited live.").arg(program)
                        : QStringLiteral("Chromium did not open DevTools in time.");
    QString error;
    if (!m_cdp.openAndWait(socket, &error))
        return QStringLiteral("Could not connect to Chromium's DevTools: %1").arg(error);
    return {};
}

void Browser::stop()
{
    if (m_cdp.isOpen()) {
        m_cdp.call(QStringLiteral("Browser.close"), {}, QString());
        m_process.waitForFinished(3000);
    }
    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
    m_cdp.close();
}

std::optional<Browser::Page> Browser::attachPage(const QUrl &url, QString *error)
{
    QString failure;
    QString target;
    // The window's own tab can take a moment to appear.
    for (int attempt = 0; attempt < 50 && target.isEmpty(); ++attempt) {
        const QJsonObject targets = m_cdp.callAndWait(QStringLiteral("Target.getTargets"), {}, QString(), &failure);
        for (const QJsonValue &each : targets["targetInfos"].toArray()) {
            if (each["type"].toString() == QLatin1String("page")) {
                target = each["targetId"].toString();
                break;
            }
        }
        if (target.isEmpty()) {
            QEventLoop wait;
            QTimer::singleShot(100, &wait, &QEventLoop::quit);
            wait.exec();
        }
    }
    if (target.isEmpty()) {
        const QJsonObject created = m_cdp.callAndWait(QStringLiteral("Target.createTarget"), {{"url", "about:blank"}}, QString(), &failure);
        target = created["targetId"].toString();
    }
    const QJsonObject attached = m_cdp.callAndWait(QStringLiteral("Target.attachToTarget"), {{"targetId", target}, {"flatten", true}}, QString(), &failure);
    const QString session = attached["sessionId"].toString();
    if (session.isEmpty()) {
        if (error)
            *error = QStringLiteral("Could not open a tab in Chromium: %1").arg(failure);
        return std::nullopt;
    }
    for (const char *domain : {"Page.enable", "Runtime.enable", "DOM.enable"})
        m_cdp.callAndWait(QLatin1String(domain), {}, session);
    Page page{target, session};
    if (url.isValid() && !url.isEmpty()) {
        if (const QString failed = navigate(page, url); !failed.isEmpty()) {
            if (error)
                *error = failed;
            return std::nullopt;
        }
    }
    return page;
}

QString Browser::navigate(const Page &page, const QUrl &url, int timeoutMs)
{
    QEventLoop loop;
    bool loaded = false;
    const auto watch = connect(&m_cdp, &CdpConnection::event, &loop, [&](const QString &method, const QJsonObject &, const QString &session) {
        if (session == page.sessionId && method == QLatin1String("Page.loadEventFired")) {
            loaded = true;
            loop.quit();
        }
    });
    QString error;
    const QJsonObject result = url.isEmpty() ? m_cdp.callAndWait(QStringLiteral("Page.reload"), {}, page.sessionId, &error)
                                             : m_cdp.callAndWait(QStringLiteral("Page.navigate"), {{"url", url.toString()}}, page.sessionId, &error);
    if (!error.isEmpty() || !result["errorText"].toString().isEmpty()) {
        disconnect(watch);
        return QStringLiteral("Couldn't open %1: %2").arg(url.toString(), error.isEmpty() ? result["errorText"].toString() : error);
    }
    if (!loaded) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    disconnect(watch);
    return loaded ? QString() : QStringLiteral("%1 didn't finish loading in time.").arg(url.toString());
}
