#include "Cloud/CloudLocation.h"
#include "IO/FileError.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString cleanPath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QStringList parts;
    for (const QString &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part != QLatin1String("."))
            parts << part;
    }
    return parts.join(QLatin1Char('/'));
}

QString indexPath()
{
    return QDir(CloudCache::root()).filePath(QStringLiteral("index.json"));
}

QJsonObject readIndex()
{
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeIndex(const QJsonObject &index)
{
    QDir().mkpath(CloudCache::root());
    QSaveFile file(indexPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(index).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
}

std::optional<CloudLocation> CloudLocation::parse(const QString &text)
{
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    if (text.startsWith(QLatin1Char('/')) || colon <= 0)
        return std::nullopt;
    const QString remote = text.left(colon);
    if (!isValidRemoteName(remote))
        return std::nullopt;
    return CloudLocation{remote, cleanPath(text.mid(colon + 1))};
}

bool CloudLocation::isValidRemoteName(const QString &name)
{
    static const QRegularExpression allowed(QStringLiteral("^[A-Za-z0-9_.+@][A-Za-z0-9_.+@ -]{0,63}$"));
    return allowed.match(name).hasMatch() && !name.endsWith(QLatin1Char(' '));
}

QString CloudLocation::fileName() const
{
    return path.section(QLatin1Char('/'), -1);
}

CloudLocation CloudLocation::parent() const
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return {remote, slash < 0 ? QString() : path.left(slash)};
}

CloudLocation CloudLocation::child(const QString &name) const
{
    return {remote, cleanPath(path + QLatin1Char('/') + name)};
}

QStringList CloudLocation::segments() const
{
    return path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool CloudStamp::sameVersion(const CloudStamp &other) const
{
    if (exists != other.exists)
        return false;
    if (!exists)
        return true;
    for (auto hash = hashes.cbegin(); hash != hashes.cend(); ++hash) {
        const QString theirs = other.hashes.value(hash.key()).toString();
        if (!theirs.isEmpty() && !hash.value().toString().isEmpty())
            return theirs.compare(hash.value().toString(), Qt::CaseInsensitive) == 0;
    }
    if (size != other.size)
        return false;
    if (modified.isValid() != other.modified.isValid())
        return false;
    return !modified.isValid() || std::abs(modified.msecsTo(other.modified)) < 1000;
}

QVariantMap CloudStamp::toMap() const
{
    return {{QStringLiteral("exists"), exists},
            {QStringLiteral("size"), size},
            {QStringLiteral("modified"), modified.toString(Qt::ISODateWithMs)},
            {QStringLiteral("hashes"), hashes}};
}

CloudStamp CloudStamp::fromMap(const QVariantMap &map)
{
    CloudStamp stamp;
    stamp.exists = map.value(QStringLiteral("exists")).toBool();
    stamp.size = map.value(QStringLiteral("size"), -1).toLongLong();
    stamp.modified = QDateTime::fromString(map.value(QStringLiteral("modified")).toString(), Qt::ISODateWithMs);
    stamp.hashes = map.value(QStringLiteral("hashes")).toMap();
    return stamp;
}

namespace CloudCache {
QString root()
{
    QString cache = qEnvironmentVariable("XDG_CACHE_HOME");
    if (cache.isEmpty() || !QDir::isAbsolutePath(cache))
        cache = QDir::home().filePath(QStringLiteral(".cache"));
    return QDir(cache).filePath(QStringLiteral("omastrator/cloud"));
}

QString localPath(const CloudLocation &location)
{
    const QStringList parts = location.segments();
    if (!CloudLocation::isValidRemoteName(location.remote) || parts.isEmpty() || parts.contains(QStringLiteral("..")))
        throw FileError(QStringLiteral("“%1” is not a file Omastrator can keep a copy of.").arg(location.toString()));
    return QDir(root()).filePath(location.remote + QLatin1Char('/') + parts.join(QLatin1Char('/')));
}

std::optional<CloudStamp> recorded(const CloudLocation &location)
{
    const QJsonObject index = readIndex();
    const QJsonValue entry = index.value(location.toString());
    if (!entry.isObject())
        return std::nullopt;
    return CloudStamp::fromMap(entry.toObject().toVariantMap());
}

void record(const CloudLocation &location, const CloudStamp &stamp)
{
    QJsonObject index = readIndex();
    index.insert(location.toString(), QJsonObject::fromVariantMap(stamp.toMap()));
    writeIndex(index);
}

void forget(const CloudLocation &location)
{
    QJsonObject index = readIndex();
    if (index.contains(location.toString())) {
        index.remove(location.toString());
        writeIndex(index);
    }
}
}
