#include "Agent/BrowserHost.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace BrowserHost {
namespace {
// Chromium sends at most 64 MB to a host, and takes at most 1 MB from one.
constexpr quint32 maximumIn = 64u * 1024 * 1024;
constexpr qsizetype maximumOut = 1024 * 1024;

void send(const QJsonObject &message)
{
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.size() > maximumOut)
        return;
    const auto length = std::uint32_t(body.size());
    QByteArray frame(reinterpret_cast<const char *>(&length), sizeof length);
    frame += body;
    for (qsizetype written = 0; written < frame.size();) {
        const ssize_t now = ::write(STDOUT_FILENO, frame.constData() + written, size_t(frame.size() - written));
        if (now <= 0)
            return QCoreApplication::exit(0);
        written += now;
    }
}

// A copy of the daemon, detached so it outlives Chromium's host.
void wake()
{
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--daemon")});
}
}

QString socketPath()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_BROWSER_SOCKET");
    if (!overridden.isEmpty())
        return overridden;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir(runtime).exists())
        return QDir(runtime).filePath(QStringLiteral("omastrator-browser.sock"));
    return QStringLiteral("/tmp/omastrator-browser-%1.sock").arg(getuid());
}

int run()
{
    QLocalSocket socket;
    QByteArray fromChromium;
    QByteArray fromOmastrator;
    bool linked = false;
    const qint64 chromium = qint64(getppid());

    QTimer retry;
    retry.setInterval(1500);
    auto reach = [&] {
        if (socket.state() == QLocalSocket::UnconnectedState)
            socket.connectToServer(socketPath());
    };
    QObject::connect(&retry, &QTimer::timeout, reach);
    QObject::connect(&socket, &QLocalSocket::connected, [&] {
        const QJsonObject hello{{"type", "hello"}, {"pid", chromium}, {"version", QCoreApplication::applicationVersion()}};
        socket.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
        linked = true;
        send({{"type", "link"}, {"connected", true}});
    });
    auto unlinked = [&] {
        fromOmastrator.clear();
        if (linked)
            send({{"type", "link"}, {"connected", false}});
        linked = false;
    };
    QObject::connect(&socket, &QLocalSocket::disconnected, unlinked);
    QObject::connect(&socket, &QLocalSocket::errorOccurred, [&](QLocalSocket::LocalSocketError) {
        if (socket.state() != QLocalSocket::ConnectedState)
            unlinked();
    });
    QObject::connect(&socket, &QLocalSocket::readyRead, [&] {
        fromOmastrator += socket.readAll();
        for (qsizetype end; (end = fromOmastrator.indexOf('\n')) >= 0;) {
            const QByteArray line = fromOmastrator.left(end);
            fromOmastrator.remove(0, end + 1);
            const QJsonObject message = QJsonDocument::fromJson(line).object();
            if (!message.isEmpty())
                send(message);
        }
    });

    // Chromium's side: a 32-bit length in native order, then that much JSON.
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&input, &QSocketNotifier::activated, [&] {
        char chunk[65536];
        const ssize_t got = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (got <= 0) {
            // Chromium closed the port: the extension or the browser went away.
            input.setEnabled(false);
            return QCoreApplication::exit(0);
        }
        fromChromium.append(chunk, got);
        while (fromChromium.size() >= 4) {
            std::uint32_t length = 0;
            std::memcpy(&length, fromChromium.constData(), sizeof length);
            if (length > maximumIn)
                return QCoreApplication::exit(1);
            if (fromChromium.size() < qsizetype(4 + length))
                return;
            const QJsonObject message = QJsonDocument::fromJson(fromChromium.mid(4, length)).object();
            fromChromium.remove(0, 4 + length);
            const QString type = message["type"].toString();
            if (type == QLatin1String("wake")) {
                if (!linked)
                    wake();
                continue;
            }
            if (linked) {
                socket.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
                continue;
            }
            // Without Omastrator, a call is answered here and anything else is dropped.
            if (type == QLatin1String("call"))
                send({{"type", "reply"}, {"id", message["id"]}, {"error", QStringLiteral("Omastrator isn't running.")}});
        }
    });

    send({{"type", "link"}, {"connected", false}});
    reach();
    retry.start();
    return QCoreApplication::exec();
}
}
