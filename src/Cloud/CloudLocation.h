#pragma once
#include <QDateTime>
#include <QString>
#include <QVariantMap>
#include <optional>

// A file or folder on an rclone remote: `remote:path`, the path without a leading slash.
struct CloudLocation {
    QString remote;
    QString path;

    // "work:Designs/logo.omai"; recent files keep this form.
    QString toString() const { return remote + QLatin1Char(':') + path; }
    // A local path always starts with '/', so anything else with "name:" is a remote.
    static std::optional<CloudLocation> parse(const QString &text);
    // Remote names rclone accepts, and nothing that could read as a flag.
    static bool isValidRemoteName(const QString &name);

    QString fileName() const;
    CloudLocation parent() const;
    CloudLocation child(const QString &name) const;
    // Folder names from the root, for breadcrumbs.
    QStringList segments() const;
    bool operator==(const CloudLocation &other) const = default;
};

// What a remote file looked like at one moment, to notice when someone else changes it.
struct CloudStamp {
    bool exists = false;
    qint64 size = -1;
    QDateTime modified;
    // Only the hashes rclone gave for free, by type ("md5", "sha1", "quickxor"...).
    QVariantMap hashes;

    // A shared hash decides; otherwise size and time, to the second, since backends round.
    bool sameVersion(const CloudStamp &other) const;
    QVariantMap toMap() const;
    static CloudStamp fromMap(const QVariantMap &map);
};

// Where cloud documents are copied to be opened: $XDG_CACHE_HOME/omastrator/cloud/<remote>/<path>.
namespace CloudCache {
QString root();
// Throws FileError for a path that would leave the cache, such as one with "..".
QString localPath(const CloudLocation &location);
// The stamp recorded when this copy last matched the remote.
std::optional<CloudStamp> recorded(const CloudLocation &location);
void record(const CloudLocation &location, const CloudStamp &stamp);
void forget(const CloudLocation &location);
}
