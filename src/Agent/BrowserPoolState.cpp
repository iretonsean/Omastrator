#include "Agent/BrowserPoolState.h"
#include "Agent/Island.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QThread>
#include <csignal>

namespace BrowserPoolState {
QString path()
{
    return QDir(Island::runtimeDirectory()).filePath(QStringLiteral("browser-view.json"));
}

State read()
{
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    return {object["pid"].toInteger(), object["profile"].toString()};
}

QString write(const State &state)
{
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile file(path());
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Couldn't write the browser's state: ") + file.errorString();
    file.write(QJsonDocument(QJsonObject{{"pid", state.pid}, {"profile", state.profile}}).toJson(QJsonDocument::Indented));
    return file.commit() ? QString() : QStringLiteral("Couldn't write the browser's state.");
}

void clear()
{
    QFile::remove(path());
}

static bool namesProfile(qint64 pid, const QString &profile)
{
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (profile.isEmpty() || !file.open(QIODevice::ReadOnly))
        return false;
    for (const QByteArray &argument : file.readAll().split('\0')) {
        if (QString::fromLocal8Bit(argument) == QLatin1String("--user-data-dir=") + profile)
            return true;
    }
    return false;
}

bool endLeftover()
{
    const State state = read();
    clear();
    if (state.isEmpty() || !namesProfile(state.pid, state.profile))
        return false;
    ::kill(static_cast<pid_t>(state.pid), SIGTERM);
    // Give it a moment to go, then make sure: reset must leave nothing on the profile.
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 3000 && QFileInfo::exists(QStringLiteral("/proc/%1").arg(state.pid)) && namesProfile(state.pid, state.profile))
        QThread::msleep(50);
    if (namesProfile(state.pid, state.profile))
        ::kill(static_cast<pid_t>(state.pid), SIGKILL);
    return true;
}
}
