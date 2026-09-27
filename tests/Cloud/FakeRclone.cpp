// A stand-in for rclone, run through OMASTRATOR_RCLONE. Each remote is a folder under
// $FAKE_RCLONE_ROOT/remotes/<name>; the "config" is $FAKE_RCLONE_ROOT/config.json and, like
// rclone's, holds a token for signed-in remotes.
//   FAKE_RCLONE_FAIL      comma list: lsjson, stat, upload, download, mkdir, listremotes, config, all
//   FAKE_RCLONE_DELAY_MS  sleep first, to be slow
//   FAKE_RCLONE_LEAK      print a token on stderr when failing, as a careless backend might
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cstdio>

namespace {
const QString secretToken = QStringLiteral("{\"access_token\":\"FAKE-SECRET-TOKEN-7f3a\",\"refresh_token\":\"FAKE-REFRESH-9c1d\"}");
QString root;

void out(const QByteArray &bytes)
{
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
}

int fail(const QString &message, int code)
{
    QByteArray line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")).toUtf8() + " NOTICE: " + message.toUtf8() + "\n";
    if (qEnvironmentVariableIsSet("FAKE_RCLONE_LEAK"))
        line = "2026/01/01 00:00:00 ERROR : token = " + secretToken.toUtf8() + " expired\n" + line;
    fwrite(line.constData(), 1, size_t(line.size()), stderr);
    return code;
}

bool failing(const QString &what)
{
    const QStringList list = qEnvironmentVariable("FAKE_RCLONE_FAIL").split(QLatin1Char(','), Qt::SkipEmptyParts);
    return list.contains(what) || list.contains(QStringLiteral("all"));
}

QJsonObject readConfig()
{
    QFile file(QDir(root).filePath(QStringLiteral("config.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeConfig(const QJsonObject &config)
{
    QFile file(QDir(root).filePath(QStringLiteral("config.json")));
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(config).toJson());
}

// "name:path" to a folder under remotes/; plain paths stay local. Empty when the remote is unknown.
std::optional<QString> resolve(const QString &argument, bool *remote = nullptr)
{
    const qsizetype colon = argument.indexOf(QLatin1Char(':'));
    if (argument.startsWith(QLatin1Char('/')) || colon <= 0) {
        if (remote)
            *remote = false;
        return argument;
    }
    if (remote)
        *remote = true;
    const QString name = argument.left(colon);
    if (!readConfig().value(QStringLiteral("remotes")).toObject().contains(name))
        return std::nullopt;
    return QDir::cleanPath(QDir(root).filePath(QStringLiteral("remotes/") + name + QLatin1Char('/') + argument.mid(colon + 1)));
}

QJsonObject describe(const QFileInfo &info, bool hash)
{
    QJsonObject object{{"Path", info.fileName()},
                       {"Name", info.fileName()},
                       {"Size", info.isDir() ? -1 : info.size()},
                       {"ModTime", info.lastModified().toString(Qt::ISODateWithMs)},
                       {"IsDir", info.isDir()}};
    if (hash && !info.isDir()) {
        QFile file(info.filePath());
        if (file.open(QIODevice::ReadOnly))
            object.insert("Hashes", QJsonObject{{"md5", QString::fromLatin1(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Md5).toHex())}});
    }
    return object;
}

QJsonObject question(const QString &state, const QString &name, const QString &help, const QString &error = QString())
{
    const QJsonObject option{{"Name", name}, {"Help", help}, {"Default", ""}, {"DefaultStr", ""}, {"Examples", QJsonArray()},
                             {"Required", true}, {"IsPassword", false}, {"Exclusive", false}, {"Type", "string"}};
    return {{"State", state}, {"Option", option}, {"Error", error}, {"Result", ""}};
}

QJsonObject finished()
{
    return {{"State", ""}, {"Option", QJsonValue()}, {"Error", ""}, {"Result", ""}};
}

bool browserType(const QString &type)
{
    return QStringList{"drive", "dropbox", "onedrive", "box", "pcloud", "fakeoauth"}.contains(type);
}

void storeRemote(const QString &name, const QJsonObject &remote)
{
    QJsonObject config = readConfig();
    QJsonObject remotes = config.value(QStringLiteral("remotes")).toObject();
    remotes.insert(name, remote);
    config.insert(QStringLiteral("remotes"), remotes);
    writeConfig(config);
}

int config(const QStringList &arguments)
{
    if (failing(QStringLiteral("config")))
        return fail(QStringLiteral("Failed to configure: the service said no"), 1);
    const QString verb = arguments.value(1), name = arguments.value(2);
    QJsonObject remotes = readConfig().value(QStringLiteral("remotes")).toObject();
    if (verb == QLatin1String("delete")) {
        remotes.remove(name);
        QJsonObject config = readConfig();
        config.insert(QStringLiteral("remotes"), remotes);
        writeConfig(config);
        return 0;
    }
    if (verb == QLatin1String("create")) {
        const QString type = arguments.value(3);
        QJsonObject remote{{"type", type}};
        for (const QString &pair : arguments.mid(4)) {
            const qsizetype equals = pair.indexOf(QLatin1Char('='));
            if (!pair.startsWith(QLatin1String("--")) && equals > 0)
                remote.insert(pair.left(equals), pair.mid(equals + 1));
        }
        storeRemote(name, remote);
        QDir().mkpath(QDir(root).filePath(QStringLiteral("remotes/") + name));
        if (!arguments.contains(QStringLiteral("--no-output")) && !arguments.contains(QStringLiteral("--non-interactive"))) {
            // Real rclone prints the new section, secrets and all.
            out("[" + name.toUtf8() + "]\ntype = " + type.toUtf8() + "\n");
        }
        if (browserType(type))
            out(QJsonDocument(question("*oauth-islocal", "config_is_local", "Use web browser to automatically authenticate rclone with remote?")).toJson());
        else if (type == QLatin1String("iclouddrive"))
            out(QJsonDocument(question("2fa", "config_2fa", "Two-factor authentication: please enter your 2FA code")).toJson());
        else
            out(QJsonDocument(finished()).toJson());
        return 0;
    }
    if (verb == QLatin1String("update")) {
        const QString state = arguments.value(arguments.indexOf(QStringLiteral("--state")) + 1);
        const QString result = arguments.value(arguments.indexOf(QStringLiteral("--result")) + 1);
        QJsonObject remote = remotes.value(name).toObject();
        if (state == QLatin1String("*oauth-islocal")) {
            // The browser round trip; the token lands in the config, as rclone's does.
            remote.insert(QStringLiteral("token"), secretToken);
            storeRemote(name, remote);
            const QByteArray note = "NOTICE: Got token " + secretToken.toUtf8() + "\n";
            fwrite(note.constData(), 1, size_t(note.size()), stderr);
            out(QJsonDocument(finished()).toJson());
            return 0;
        }
        if (state == QLatin1String("2fa")) {
            if (result != QLatin1String("123456")) {
                out(QJsonDocument(question("2fa", "config_2fa", "Two-factor authentication: please enter your 2FA code", "Incorrect code")).toJson());
                return 0;
            }
            remote.insert(QStringLiteral("cookies"), secretToken);
            storeRemote(name, remote);
            out(QJsonDocument(finished()).toJson());
            return 0;
        }
        out(QJsonDocument(finished()).toJson());
        return 0;
    }
    return fail(QStringLiteral("unknown config command"), 1);
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    root = qEnvironmentVariable("FAKE_RCLONE_ROOT");
    QStringList arguments = application.arguments().mid(1);
    {
        QFile log(QDir(root).filePath(QStringLiteral("calls.log")));
        if (log.open(QIODevice::Append))
            log.write(arguments.join(QLatin1Char(' ')).toUtf8() + "\n");
    }
    // Flags that take a value, then the rest.
    for (const QString flag : {QStringLiteral("--config"), QStringLiteral("--retries")}) {
        const qsizetype at = arguments.indexOf(flag);
        if (at >= 0)
            arguments.remove(at, 2);
    }
    if (const int delay = qEnvironmentVariableIntValue("FAKE_RCLONE_DELAY_MS"); delay > 0)
        QThread::msleep(ulong(delay));
    const QString command = arguments.value(0);
    if (command == QLatin1String("version")) {
        out("rclone v1.75.1-fake\n");
        return 0;
    }
    if (command == QLatin1String("config"))
        return config(arguments);
    if (command == QLatin1String("listremotes")) {
        if (failing(command))
            return fail(QStringLiteral("Failed to listremotes: config unreadable"), 1);
        QJsonArray list;
        const QJsonObject remotes = readConfig().value(QStringLiteral("remotes")).toObject();
        for (auto each = remotes.begin(); each != remotes.end(); ++each)
            list.append(QJsonObject{{"name", each.key()}, {"type", each.value().toObject().value(QStringLiteral("type"))}, {"source", "file"}, {"description", ""}});
        out(QJsonDocument(list).toJson(QJsonDocument::Compact));
        return 0;
    }
    const QStringList positional = [&] {
        QStringList kept;
        for (const QString &argument : arguments.mid(1)) {
            if (!argument.startsWith(QLatin1String("--")))
                kept << argument;
        }
        return kept;
    }();
    if (command == QLatin1String("lsjson")) {
        const bool statOnly = arguments.contains(QStringLiteral("--stat"));
        if (failing(statOnly ? QStringLiteral("stat") : command) || failing(QStringLiteral("offline")))
            return fail(QStringLiteral("Failed to lsjson: couldn't connect: dial tcp: lookup api.example.com: no such host"), 1);
        const std::optional<QString> path = resolve(positional.value(0));
        if (!path)
            return fail(QStringLiteral("Failed to create file system for \"%1\": didn't find section in config file").arg(positional.value(0)), 1);
        const QFileInfo info(*path);
        if (statOnly) {
            if (!info.exists())
                return fail(QStringLiteral("Failed to lsjson: object not found"), 3);
            out(QJsonDocument(describe(info, arguments.contains(QStringLiteral("--hash")))).toJson());
            return 0;
        }
        if (!info.isDir())
            return fail(QStringLiteral("Failed to lsjson: directory not found"), 3);
        QJsonArray list;
        for (const QFileInfo &each : QDir(*path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden))
            list.append(describe(each, false));
        out(QJsonDocument(list).toJson());
        return 0;
    }
    if (command == QLatin1String("copyto")) {
        bool fromRemote = false, toRemote = false;
        const std::optional<QString> from = resolve(positional.value(0), &fromRemote), to = resolve(positional.value(1), &toRemote);
        if ((toRemote && failing(QStringLiteral("upload"))) || (fromRemote && failing(QStringLiteral("download"))) || failing(QStringLiteral("offline")))
            return fail(QStringLiteral("Failed to copyto: couldn't connect: dial tcp: lookup api.example.com: no such host"), 1);
        if (!from || !to)
            return fail(QStringLiteral("Failed to create file system: didn't find section in config file"), 1);
        if (!QFileInfo(*from).isFile())
            return fail(QStringLiteral("Failed to copyto: directory not found"), 3);
        QDir().mkpath(QFileInfo(*to).absolutePath());
        QFile::remove(*to);
        if (!QFile::copy(*from, *to))
            return fail(QStringLiteral("Failed to copyto: can't write"), 1);
        QFile copied(*to);
        if (copied.open(QIODevice::ReadWrite))
            copied.setFileTime(QFileInfo(*from).lastModified(), QFileDevice::FileModificationTime);
        return 0;
    }
    if (command == QLatin1String("mkdir")) {
        if (failing(command) || failing(QStringLiteral("offline")))
            return fail(QStringLiteral("Failed to mkdir: couldn't connect"), 1);
        const std::optional<QString> path = resolve(positional.value(0));
        if (!path)
            return fail(QStringLiteral("Failed to create file system: didn't find section in config file"), 1);
        QDir().mkpath(*path);
        return 0;
    }
    return fail(QStringLiteral("unknown command \"%1\"").arg(command), 1);
}
