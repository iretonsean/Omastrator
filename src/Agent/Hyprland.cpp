#include "Agent/Hyprland.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <algorithm>

namespace {
QRect rectOf(const QJsonObject &object)
{
    const QJsonArray at = object["at"].toArray(), size = object["size"].toArray();
    return QRect(at.at(0).toInt(), at.at(1).toInt(), size.at(0).toInt(), size.at(1).toInt());
}

QString program()
{
    return qEnvironmentVariable("OMASTRATOR_HYPRCTL");
}

QByteArray runProgram(const QString &path, const QStringList &args, QString *error, int timeoutMs = 3000)
{
    QProcess process;
    process.start(path, args);
    if (!process.waitForStarted(timeoutMs) || !process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        if (error)
            *error = QStringLiteral("hyprctl didn't answer.");
        return {};
    }
    const QByteArray output = process.readAllStandardOutput();
    // hyprctl prints its errors (an `eval` that failed) on stdout, so a failure is never left without a reason.
    if (process.exitCode() != 0 && error) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = QString::fromUtf8(output).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("hyprctl exited with %1.").arg(process.exitCode());
    }
    return output;
}

// Hyprland's request socket: one request per connection, answered then closed.
QByteArray askSocket(const QByteArray &request, QString *error)
{
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty()) {
        if (error)
            *error = QStringLiteral("Hyprland isn't running.");
        return {};
    }
    QLocalSocket socket;
    socket.connectToServer(QDir(runtime).filePath(QStringLiteral("hypr/%1/.socket.sock").arg(signature)));
    if (!socket.waitForConnected(500)) {
        if (error)
            *error = QStringLiteral("Hyprland's socket didn't answer.");
        return {};
    }
    socket.write(request);
    socket.waitForBytesWritten(500);
    QByteArray answer;
    while (socket.state() == QLocalSocket::ConnectedState && socket.waitForReadyRead(500))
        answer += socket.readAll();
    answer += socket.readAll();
    return answer;
}
}

namespace Hyprland {
std::vector<Window> parseClients(const QJsonValue &json)
{
    std::vector<Window> windows;
    for (const QJsonValue &value : json.toArray()) {
        const QJsonObject object = value.toObject();
        Window window;
        window.address = object["address"].toString();
        window.pid = object["pid"].toInteger();
        window.className = object["class"].toString();
        window.title = object["title"].toString();
        window.rect = rectOf(object);
        window.workspace = object["workspace"].toObject()["id"].toInt();
        window.workspaceName = object["workspace"].toObject()["name"].toString();
        window.monitor = object["monitor"].toInt(-1);
        window.floating = object["floating"].toBool();
        window.mapped = object["mapped"].toBool(true);
        window.hidden = object["hidden"].toBool();
        window.fullscreen = object["fullscreen"].toInt() != 0;
        window.focusHistory = object["focusHistoryID"].toInt();
        windows.push_back(window);
    }
    return windows;
}

std::vector<Layer> parseLayers(const QJsonValue &json)
{
    std::vector<Layer> layers;
    const QJsonObject monitors = json.toObject();
    for (auto monitor = monitors.begin(); monitor != monitors.end(); ++monitor) {
        const QJsonObject levels = monitor.value().toObject()["levels"].toObject();
        for (const QJsonValue &level : levels) {
            for (const QJsonValue &value : level.toArray()) {
                const QJsonObject layer = value.toObject();
                layers.push_back({layer["namespace"].toString(),
                                  QRect(layer["x"].toInt(), layer["y"].toInt(), layer["w"].toInt(), layer["h"].toInt()), monitor.key()});
            }
        }
    }
    return layers;
}

std::vector<Monitor> parseMonitors(const QJsonValue &json)
{
    std::vector<Monitor> monitors;
    for (const QJsonValue &value : json.toArray()) {
        const QJsonObject object = value.toObject();
        Monitor monitor;
        monitor.id = object["id"].toInt();
        monitor.name = object["name"].toString();
        monitor.scale = object["scale"].toDouble(1);
        if (monitor.scale <= 0)
            monitor.scale = 1;
        // Pixels to layout size; a quarter turn swaps the sides.
        int width = int(object["width"].toInt() / monitor.scale + 0.5);
        int height = int(object["height"].toInt() / monitor.scale + 0.5);
        if (object["transform"].toInt() % 2 == 1)
            std::swap(width, height);
        monitor.rect = QRect(object["x"].toInt(), object["y"].toInt(), width, height);
        monitor.focused = object["focused"].toBool();
        monitor.reservedTop = object["reserved"].toArray().at(1).toInt();
        monitor.activeWorkspace = object["activeWorkspace"].toObject()["id"].toInt();
        monitor.specialWorkspace = object["specialWorkspace"].toObject()["id"].toInt();
        monitor.specialName = object["specialWorkspace"].toObject()["name"].toString();
        monitors.push_back(monitor);
    }
    return monitors;
}

std::optional<QPoint> parseCursor(const QJsonValue &json)
{
    const QJsonObject object = json.toObject();
    if (!object.contains("x") || !object.contains("y"))
        return std::nullopt;
    return QPoint(object["x"].toInt(), object["y"].toInt());
}

QJsonValue query(const QString &what, QString *error)
{
    const QString overridden = program();
    const QByteArray answer = overridden.isEmpty() ? askSocket("j/" + what.toUtf8(), error)
                                                   : runProgram(overridden, {QStringLiteral("-j"), what}, error);
    if (answer.trimmed().isEmpty())
        return QJsonValue();
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(answer, &parse);
    if (parse.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("Hyprland's answer couldn't be read.");
        return QJsonValue();
    }
    return document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
}

bool usesLua()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : given;
    return QFileInfo::exists(QDir(config).filePath(QStringLiteral("hypr/hyprland.lua")));
}

QString reload()
{
    QString error;
    const QString overridden = program();
    if (overridden.isEmpty())
        askSocket("reload config-only", &error);
    else
        runProgram(overridden, {QStringLiteral("reload"), QStringLiteral("config-only")}, &error, 10'000);
    return error;
}

ConfigCheck verifyConfig(const QString &path)
{
    ConfigCheck check;
    const QString overridden = qEnvironmentVariable("OMASTRATOR_HYPRLAND");
    const QString binary = overridden.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("Hyprland")) : overridden;
    if (binary.isEmpty() || !QFileInfo(binary).isExecutable())
        return check;
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(binary, {QStringLiteral("--verify-config"), QStringLiteral("-c"), path});
    if (!process.waitForStarted(5000))
        return check;
    check.available = true;
    if (!process.waitForFinished(30'000)) {
        process.kill();
        process.waitForFinished(500);
        check.errors = QStringLiteral("Hyprland's config checker didn't finish.");
        return check;
    }
    QString text = QString::fromUtf8(process.readAll());
    // Its debug lines come first; the verdict follows this heading.
    const QString heading = QStringLiteral("Config parsing result:");
    if (const qsizetype at = text.lastIndexOf(heading); at >= 0)
        text = text.mid(at + heading.size());
    text = text.trimmed();
    check.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!check.ok)
        check.errors = text.isEmpty() ? QStringLiteral("Hyprland's config checker failed without saying why.") : text;
    return check;
}

QString dispatch(const QString &lua, const QString &legacy)
{
    const QString overridden = program();
    if (overridden.isEmpty() && qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty())
        return QStringLiteral("Hyprland isn't running.");
    const QString path = overridden.isEmpty() ? QStringLiteral("hyprctl") : overridden;
    QStringList args;
    if (usesLua())
        args = {QStringLiteral("eval"), lua};
    else
        args = QStringList{QStringLiteral("dispatch")} + legacy.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString error;
    runProgram(path, args, &error);
    return error;
}

namespace {
QString luaString(QString text)
{
    return text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
        .replace(QLatin1Char('"'), QStringLiteral("\\\""))
        .replace(QLatin1Char('\n'), QStringLiteral("\\n"))
        .replace(QLatin1Char('\r'), QStringLiteral("\\r"));
}

QString withZeroX(const QString &address)
{
    return address.startsWith(QLatin1String("0x")) ? address : QStringLiteral("0x") + address;
}
}

QString workspaceSelector(int id, const QString &name)
{
    if (id > 0)
        return QString::number(id);
    if (name.startsWith(QLatin1String("special:")))
        return name;
    return QStringLiteral("name:") + name;
}

Dispatch moveWindowDispatch(const QString &address, const QString &workspace, bool follow)
{
    const QString window = withZeroX(address);
    return {QStringLiteral("hl.dispatch(hl.dsp.window.move({ workspace = \"%1\", follow = %2, window = \"address:%3\" }))")
                .arg(luaString(workspace), follow ? QStringLiteral("true") : QStringLiteral("false"), window),
            QStringLiteral("%1 %2,address:%3").arg(follow ? QStringLiteral("movetoworkspace") : QStringLiteral("movetoworkspacesilent"), workspace, window)};
}

QString moveWindow(const QString &address, const QString &workspace, bool follow)
{
    const Dispatch move = moveWindowDispatch(address, workspace, follow);
    return dispatch(move.lua, move.legacy);
}

QString dispatchAll(const std::vector<Dispatch> &dispatches)
{
    if (dispatches.empty())
        return {};
    if (dispatches.size() == 1)
        return dispatch(dispatches.front().lua, dispatches.front().legacy);
    const QString overridden = program();
    if (overridden.isEmpty() && qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE").isEmpty())
        return QStringLiteral("Hyprland isn't running.");
    QStringList parts;
    QStringList args;
    if (usesLua()) {
        for (const Dispatch &each : dispatches)
            parts << each.lua;
        args = {QStringLiteral("eval"), parts.join(QStringLiteral("; "))};
    } else {
        // --batch splits on ";", which a page's name may hold: then one call each, as before, stopping at a refusal.
        if (std::any_of(dispatches.begin(), dispatches.end(), [](const Dispatch &each) { return each.legacy.contains(QLatin1Char(';')); })) {
            for (const Dispatch &each : dispatches) {
                if (const QString error = dispatch(each.lua, each.legacy); !error.isEmpty())
                    return error;
            }
            return {};
        }
        for (const Dispatch &each : dispatches)
            parts << QStringLiteral("dispatch ") + each.legacy;
        args = {QStringLiteral("--batch"), parts.join(QStringLiteral(" ; "))};
    }
    QString error;
    runProgram(overridden.isEmpty() ? QStringLiteral("hyprctl") : overridden, args, &error);
    return error;
}

QString addWorkspaceRule(const QString &classRegex, const QString &titleRegex, const QString &workspace)
{
    if (!usesLua())
        return QStringLiteral("Runtime window rules need a Lua config.");
    return dispatch(QStringLiteral("hl.window_rule({ match = { class = \"%1\", title = \"%2\" }, workspace = \"%3 silent\" })")
                        .arg(luaString(classRegex), luaString(titleRegex), luaString(workspace)),
                    QString());
}

QString focusWorkspace(const QString &workspace)
{
    return dispatch(QStringLiteral("hl.dispatch(hl.dsp.focus({ workspace = \"%1\" }))").arg(luaString(workspace)),
                    QStringLiteral("workspace %1").arg(workspace));
}

QString focusWindow(const QString &address)
{
    const QString window = withZeroX(address);
    return dispatch(QStringLiteral("hl.dispatch(hl.dsp.focus({ window = \"address:%1\" }))").arg(window),
                    QStringLiteral("focuswindow address:%1").arg(window));
}

bool isShown(const Window &window, const std::vector<Monitor> &monitors)
{
    if (!window.mapped || window.hidden)
        return false;
    return std::any_of(monitors.begin(), monitors.end(), [&](const Monitor &monitor) {
        return window.workspace == monitor.activeWorkspace || (monitor.specialWorkspace != 0 && window.workspace == monitor.specialWorkspace);
    });
}

std::optional<Window> windowAt(QPoint point, const std::vector<Window> &windows, const std::vector<Monitor> &monitors)
{
    const Window *best = nullptr;
    auto rank = [&](const Window &window) {
        const bool special = window.workspace < 0;
        return std::tuple(special, window.fullscreen, window.floating, -window.focusHistory);
    };
    for (const Window &window : windows) {
        if (!isShown(window, monitors) || !window.rect.contains(point))
            continue;
        // A special workspace covers its monitor: only its own windows are under the pointer there.
        if (!best || rank(window) > rank(*best))
            best = &window;
    }
    if (!best)
        return std::nullopt;
    return *best;
}

std::optional<Monitor> monitorAt(QPoint point, const std::vector<Monitor> &monitors)
{
    for (const Monitor &monitor : monitors) {
        if (monitor.rect.contains(point))
            return monitor;
    }
    return std::nullopt;
}

std::optional<Monitor> monitorNamed(const QString &name, const std::vector<Monitor> &monitors)
{
    for (const Monitor &monitor : monitors) {
        if (monitor.name == name)
            return monitor;
    }
    return std::nullopt;
}

std::optional<Monitor> focusedMonitor(const std::vector<Monitor> &monitors)
{
    for (const Monitor &monitor : monitors) {
        if (monitor.focused)
            return monitor;
    }
    if (monitors.empty())
        return std::nullopt;
    return monitors.front();
}
}
