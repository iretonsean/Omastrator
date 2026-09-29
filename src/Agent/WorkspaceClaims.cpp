#include "Agent/WorkspaceClaims.h"
#include "Agent/Hyprland.h"
#include "Agent/Island.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <unistd.h>

namespace WorkspaceClaims {
QString path()
{
    return QDir(Island::runtimeDirectory()).filePath(QStringLiteral("workspaces.json"));
}

State read()
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    State state;
    state.signature = object["signature"].toString();
    state.pid = object["pid"].toInteger();
    state.returnWorkspace = object["return"].toString();
    state.returnId = object["returnId"].toInt();
    for (const QJsonValue &value : object["claims"].toArray()) {
        const QJsonObject each = value.toObject();
        Claim claim;
        claim.name = each["name"].toString();
        claim.tabId = each["tab"].toString();
        claim.pageId = each["page"].toString();
        for (const QJsonValue &window : each["windows"].toArray())
            claim.windows << window.toString();
        if (!claim.name.isEmpty())
            state.claims.append(claim);
    }
    return state;
}

QString write(const State &state)
{
    QJsonObject object;
    if (!state.isEmpty()) {
        object["signature"] = state.signature;
        object["pid"] = state.pid;
        object["return"] = state.returnWorkspace;
        object["returnId"] = state.returnId;
        QJsonArray claims;
        for (const Claim &claim : state.claims) {
            claims.append(QJsonObject{{"name", claim.name},
                                      {"tab", claim.tabId},
                                      {"page", claim.pageId},
                                      {"windows", QJsonArray::fromStringList(claim.windows)}});
        }
        object["claims"] = claims;
    }
    if (state.isEmpty() && !QFileInfo::exists(path()))
        return {};
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile file(path());
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Couldn't write the workspace claims: ") + file.errorString();
    file.write(state.isEmpty() ? QByteArray("{}\n") : QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit() ? QString() : QStringLiteral("Couldn't write the workspace claims.");
}

GiveBack giveBack(const State &state)
{
    GiveBack result;
    result.to = state.returnWorkspace;
    if (state.isEmpty() || state.returnWorkspace.isEmpty())
        return result;
    QSet<QString> claimed, ours;
    for (const Claim &claim : state.claims) {
        claimed.insert(claim.name);
        for (const QString &window : claim.windows)
            ours.insert(window.startsWith(QLatin1String("0x")) ? window : QStringLiteral("0x") + window);
    }
    const QString back = Hyprland::workspaceSelector(state.returnId, state.returnWorkspace);
    QString error;
    const QJsonValue clients = Hyprland::query(QStringLiteral("clients"), &error);
    if (!error.isEmpty()) {
        result.error = error;
        return result;
    }
    for (const Hyprland::Window &window : Hyprland::parseClients(clients)) {
        if (!claimed.contains(window.workspaceName) || ours.contains(window.address) || (state.pid > 0 && window.pid == state.pid))
            continue;
        const QString failure = Hyprland::moveWindow(window.address, back, false);
        if (failure.isEmpty())
            ++result.moved;
        else if (result.error.isEmpty())
            result.error = failure;
    }
    // Standing on a claimed workspace, the user goes back with their windows.
    const QJsonValue active = Hyprland::query(QStringLiteral("activeworkspace"), &error);
    if (claimed.contains(active.toObject()["name"].toString())) {
        const QString failure = Hyprland::focusWorkspace(back);
        if (!failure.isEmpty() && result.error.isEmpty())
            result.error = failure;
    }
    return result;
}

GiveBack giveBackFromFile()
{
    const State state = read();
    GiveBack result = giveBack(state);
    if (const QString failure = write({}); !failure.isEmpty() && result.error.isEmpty())
        result.error = failure;
    return result;
}

bool cleanUp(qint64 ownPid)
{
    const State state = read();
    if (state.isEmpty())
        return false;
    if (ownPid == 0)
        ownPid = getpid();
    if (state.pid == ownPid)
        return false;
    if (state.pid > 0 && QFileInfo::exists(QStringLiteral("/proc/%1").arg(state.pid)))
        return false;
    const QString current = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    // With a fake hyprctl (tests) there's no signature; then the file's own is what counts.
    if (state.signature == current || current.isEmpty())
        giveBack(state);
    write({});
    return true;
}
}
