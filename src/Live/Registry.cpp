#include "Live/Registry.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <unistd.h>

namespace {
// The pool's thread and the UI's both remember sites; each read-modify-write is one turn.
QMutex &registryMutex()
{
    static QMutex mutex;
    return mutex;
}

QJsonObject load()
{
    QFile file(ProjectRegistry::path());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

QString save(const QJsonObject &projects)
{
    const QString path = ProjectRegistry::path();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(projects).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
    return {};
}

QString link(const QString &path)
{
    char target[4096];
    const ssize_t length = ::readlink(QFile::encodeName(path).constData(), target, sizeof target - 1);
    return length > 0 ? QString::fromLocal8Bit(target, length) : QString();
}

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.read(256 * 1024)) : QString();
}

// "www.my-site.co.uk" → "my-site"; "app.vercel.app" → "app".
QString stem(const QString &host)
{
    QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (!labels.isEmpty() && labels.front() == QLatin1String("www"))
        labels.removeFirst();
    return labels.value(0).toLower();
}

// A project folder: where the served files' package.json or .git is, walking up from `start`.
QString projectRoot(QString start)
{
    for (QDir dir(start); ; ) {
        if (dir.exists(QStringLiteral("package.json")) || dir.exists(QStringLiteral(".git")))
            return dir.path();
        if (!dir.cdUp() || dir.isRoot())
            return start;
    }
}
}

namespace ProjectRegistry {
QString path()
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    const QString config = given.isEmpty() ? QDir::home().filePath(QStringLiteral(".config")) : given;
    return QDir(config).filePath(QStringLiteral("omastrator/projects.json"));
}

QString originOf(const QUrl &url)
{
    QString origin = url.scheme() + QStringLiteral("://") + url.host().toLower();
    if (url.port() > 0)
        origin += QLatin1Char(':') + QString::number(url.port());
    return origin;
}

std::optional<QString> folderFor(const QUrl &url)
{
    QMutexLocker lock(&registryMutex());
    const QString folder = load()[originOf(url)].toString();
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return std::nullopt;
    return folder;
}

bool owns(const QUrl &url)
{
    return folderFor(url).has_value();
}

QString remember(const QUrl &url, const QString &folder)
{
    if (!QFileInfo(folder).isDir())
        return QStringLiteral("%1 isn't a folder.").arg(folder);
    QMutexLocker lock(&registryMutex());
    QJsonObject projects = load();
    projects[originOf(url)] = QFileInfo(folder).canonicalFilePath();
    return save(projects);
}

QString forget(const QUrl &url)
{
    QMutexLocker lock(&registryMutex());
    QJsonObject projects = load();
    projects.remove(originOf(url));
    return save(projects);
}

QStringList defaultRoots()
{
    QStringList roots;
    for (const char *name : {"Projects", "projects", "Code", "code", "src", "dev", "Developer", "work", "Sites", "git", "repos", "Documents/GitHub"}) {
        const QString path = QDir::home().filePath(QLatin1String(name));
        if (QFileInfo(path).isDir() && !roots.contains(QFileInfo(path).canonicalFilePath()))
            roots << QFileInfo(path).canonicalFilePath();
    }
    // Folders already registered say where this user keeps projects.
    QMutexLocker lock(&registryMutex());
    const QJsonObject projects = load();
    for (auto it = projects.begin(); it != projects.end(); ++it) {
        const QString parent = QFileInfo(it.value().toString()).absolutePath();
        if (QFileInfo(parent).isDir() && !roots.contains(parent))
            roots << parent;
    }
    return roots;
}

std::optional<QString> folderServing(int port)
{
    // The socket's inode, from the kernel's listening tables.
    QString inode;
    for (const char *table : {"/proc/net/tcp", "/proc/net/tcp6"}) {
        const QStringList lines = read(QLatin1String(table)).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QStringList fields = line.simplified().split(QLatin1Char(' '));
            // local_address is "ADDR:PORT" in hex; state 0A is LISTEN.
            if (fields.size() > 9 && fields[3] == QLatin1String("0A") && fields[1].section(QLatin1Char(':'), -1).toInt(nullptr, 16) == port) {
                inode = fields[9];
                break;
            }
        }
        if (!inode.isEmpty())
            break;
    }
    if (inode.isEmpty())
        return std::nullopt;
    const QString wanted = QStringLiteral("socket:[%1]").arg(inode);
    for (const QString &pid : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (pid.toInt() <= 0)
            continue;
        // readlink, not QFileInfo: "socket:[…]" is no path, and Qt would make it one.
        const QDir fds(QStringLiteral("/proc/%1/fd").arg(pid));
        for (const QString &fd : fds.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot)) {
            if (link(fds.filePath(fd)) == wanted) {
                const QString cwd = link(QStringLiteral("/proc/%1/cwd").arg(pid));
                if (!cwd.isEmpty())
                    return projectRoot(cwd);
            }
        }
    }
    return std::nullopt;
}

std::vector<Suggestion> suggest(const QUrl &url, const QStringList &givenRoots)
{
    std::vector<Suggestion> found;
    auto add = [&](const QString &folder, const QString &reason, int score) {
        const QString canonical = QFileInfo(folder).canonicalFilePath();
        for (Suggestion &each : found) {
            if (each.folder == canonical) {
                if (score > each.score)
                    each = {canonical, reason, score};
                return;
            }
        }
        found.push_back({canonical, reason, score});
    };
    const QString host = url.host().toLower();
    const bool local = host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("[::1]");
    if (local && url.port() > 0) {
        if (const auto serving = folderServing(url.port()))
            add(*serving, QStringLiteral("it is serving %1:%2").arg(host, QString::number(url.port())), 100);
    }
    const QString name = stem(host);
    const QString bare = host.startsWith(QLatin1String("www.")) ? host.mid(4) : host;
    const QStringList roots = givenRoots.isEmpty() ? defaultRoots() : givenRoots;
    for (const QString &root : roots) {
        // Projects sit one or two folders down.
        QStringList candidates;
        for (const QFileInfo &first : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            candidates << first.filePath();
            for (const QFileInfo &second : QDir(first.filePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                if (!second.fileName().startsWith(QLatin1Char('.')) && second.fileName() != QLatin1String("node_modules"))
                    candidates << second.filePath();
        }
        for (const QString &folder : std::as_const(candidates)) {
            if (local)
                continue;
            const QDir dir(folder);
            const QString folderName = dir.dirName().toLower();
            if (folderName == bare || folderName == host)
                add(folder, QStringLiteral("the folder is named %1").arg(dir.dirName()), 70);
            else if (!name.isEmpty() && folderName == name)
                add(folder, QStringLiteral("the folder is named %1").arg(dir.dirName()), 55);
            const QString vercel = read(dir.filePath(QStringLiteral(".vercel/project.json")));
            if (!vercel.isEmpty()) {
                const QString project = QJsonDocument::fromJson(vercel.toUtf8()).object()["projectName"].toString().toLower();
                if (!project.isEmpty() && (host.startsWith(project + QLatin1Char('.')) || host.startsWith(project + QLatin1Char('-'))))
                    add(folder, QStringLiteral("its Vercel project is %1").arg(project), 90);
            }
            const QString wrangler = read(dir.filePath(QStringLiteral("wrangler.toml")));
            static const QRegularExpression wranglerName(QStringLiteral(R"(^\s*name\s*=\s*["']([^"']+)["'])"), QRegularExpression::MultilineOption);
            if (const auto match = wranglerName.match(wrangler); match.hasMatch()) {
                const QString worker = match.captured(1).toLower();
                if (host.startsWith(worker + QLatin1Char('.')) || wrangler.contains(bare))
                    add(folder, QStringLiteral("its wrangler.toml deploys %1").arg(worker), 90);
            }
            const QString netlify = read(dir.filePath(QStringLiteral("netlify.toml")));
            if (!netlify.isEmpty() && (netlify.contains(bare) || (host.endsWith(QLatin1String(".netlify.app")) && folderName == name)))
                add(folder, QStringLiteral("its netlify.toml names %1").arg(bare), 85);
            const QJsonObject package = QJsonDocument::fromJson(read(dir.filePath(QStringLiteral("package.json"))).toUtf8()).object();
            if (package["homepage"].toString().contains(bare))
                add(folder, QStringLiteral("its package.json homepage is %1").arg(package["homepage"].toString()), 95);
            else if (!name.isEmpty() && package["name"].toString().toLower() == name)
                add(folder, QStringLiteral("its package.json is named %1").arg(name), 50);
            const QString git = read(dir.filePath(QStringLiteral(".git/config")));
            static const QRegularExpression remote(QStringLiteral(R"(url\s*=\s*(\S+))"));
            for (auto match = remote.globalMatch(git); match.hasNext();) {
                const QString address = match.next().captured(1).toLower();
                if (address.contains(bare))
                    add(folder, QStringLiteral("its git remote names %1").arg(bare), 80);
                else if (!name.isEmpty() && name.size() > 2 && QRegularExpression(QStringLiteral("[/:]%1(\\.git)?$").arg(QRegularExpression::escape(name))).match(address).hasMatch())
                    add(folder, QStringLiteral("its git remote is %1").arg(address), 60);
            }
        }
    }
    std::sort(found.begin(), found.end(), [](const Suggestion &a, const Suggestion &b) { return a.score > b.score; });
    return found;
}
}
