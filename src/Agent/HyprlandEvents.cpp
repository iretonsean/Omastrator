#include "Agent/HyprlandEvents.h"
#include <QDir>

namespace {
QString withZeroX(const QString &address)
{
    return address.startsWith(QLatin1String("0x")) ? address : QStringLiteral("0x") + address;
}
}

HyprlandEvents::HyprlandEvents(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] { feed(m_socket.readAll()); });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        if (!m_stopping)
            emit closed();
    });
}

std::optional<HyprlandEvents::Event> HyprlandEvents::parse(const QString &line)
{
    const qsizetype split = line.indexOf(QLatin1String(">>"));
    if (split < 0)
        return std::nullopt;
    const QString name = line.left(split), data = line.mid(split + 2);
    Event event;
    // A reload drops runtime rules, such as the stand-ins' (docs/WORKSPACES.md).
    if (name == QLatin1String("configreloaded")) {
        event.kind = Event::Kind::configReloaded;
        return event;
    }
    if (name == QLatin1String("workspacev2") || name == QLatin1String("destroyworkspacev2")) {
        const qsizetype comma = data.indexOf(QLatin1Char(','));
        bool ok = false;
        const int id = comma < 0 ? 0 : data.left(comma).toInt(&ok);
        if (!ok)
            return std::nullopt;
        event.kind = name == QLatin1String("workspacev2") ? Event::Kind::workspace : Event::Kind::destroyWorkspace;
        event.workspaceId = id;
        event.workspaceName = data.mid(comma + 1);
        return event;
    }
    if (name == QLatin1String("closewindow")) {
        if (data.isEmpty())
            return std::nullopt;
        event.kind = Event::Kind::closeWindow;
        event.address = withZeroX(data);
        return event;
    }
    if (name == QLatin1String("movewindowv2")) {
        const QStringList parts = data.split(QLatin1Char(','));
        bool ok = false;
        if (parts.size() < 3 || parts.at(0).isEmpty())
            return std::nullopt;
        event.workspaceId = parts.at(1).toInt(&ok);
        if (!ok)
            return std::nullopt;
        event.kind = Event::Kind::moveWindow;
        event.address = withZeroX(parts.at(0));
        event.workspaceName = data.section(QLatin1Char(','), 2);
        return event;
    }
    if (name == QLatin1String("openwindow")) {
        // address,workspace name,class,title: the title is last, so it may hold commas.
        const QStringList parts = data.split(QLatin1Char(','));
        if (parts.size() < 4 || parts.at(0).isEmpty())
            return std::nullopt;
        event.kind = Event::Kind::openWindow;
        event.address = withZeroX(parts.at(0));
        event.workspaceName = parts.at(1);
        event.title = data.section(QLatin1Char(','), 3);
        return event;
    }
    return std::nullopt;
}

QString HyprlandEvents::socketPath()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_HYPRLAND_EVENTS");
    if (!overridden.isEmpty())
        return overridden;
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return {};
    return QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket2.sock").arg(signature));
}

QString HyprlandEvents::start()
{
    if (isRunning())
        return {};
    const QString path = socketPath();
    if (path.isEmpty())
        return QStringLiteral("Hyprland isn't running.");
    m_pending.clear();
    m_stopping = false;
    m_socket.connectToServer(path);
    if (!m_socket.waitForConnected(500))
        return QStringLiteral("Hyprland's event socket didn't answer.");
    return {};
}

void HyprlandEvents::stop()
{
    m_stopping = true;
    m_socket.abort();
    m_pending.clear();
}

void HyprlandEvents::feed(const QByteArray &bytes)
{
    m_pending += bytes;
    qsizetype newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_pending.left(newline));
        m_pending.remove(0, newline + 1);
        if (const std::optional<Event> parsed = parse(line))
            emit event(*parsed);
    }
}
