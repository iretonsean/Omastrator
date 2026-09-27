#pragma once
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

// A fake rclone with its remotes in a temporary folder, and temporary XDG folders so
// nothing touches the user's config or cache.
class FakeCloud {
public:
    // The token the fake writes into its config for signed-in remotes; it must never show anywhere.
    static constexpr const char *secret = "FAKE-SECRET-TOKEN-7f3a";

    FakeCloud()
    {
        qputenv("FAKE_RCLONE_ROOT", root().toUtf8());
        qputenv("OMASTRATOR_RCLONE", FAKE_RCLONE);
        qputenv("XDG_CACHE_HOME", m_dir.filePath(QStringLiteral("cache")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
        qunsetenv("FAKE_RCLONE_FAIL");
        qunsetenv("FAKE_RCLONE_DELAY_MS");
        qunsetenv("FAKE_RCLONE_LEAK");
    }
    ~FakeCloud()
    {
        qunsetenv("OMASTRATOR_RCLONE");
        qunsetenv("FAKE_RCLONE_FAIL");
        qunsetenv("FAKE_RCLONE_DELAY_MS");
        qunsetenv("FAKE_RCLONE_LEAK");
    }

    QString root() const { return m_dir.filePath(QStringLiteral("rclone")); }
    QString cacheRoot() const { return m_dir.filePath(QStringLiteral("cache/omastrator/cloud")); }

    void addRemote(const QString &name, const QString &type)
    {
        QDir().mkpath(root());
        QFile file(QDir(root()).filePath(QStringLiteral("config.json")));
        QJsonObject config;
        if (file.open(QIODevice::ReadOnly))
            config = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QJsonObject remotes = config.value(QStringLiteral("remotes")).toObject();
        remotes.insert(name, QJsonObject{{"type", type}, {"token", QString::fromLatin1(secret)}});
        config.insert(QStringLiteral("remotes"), remotes);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(config).toJson());
        QDir().mkpath(remoteFile(name, QString()));
    }

    QString remoteFile(const QString &remote, const QString &path) const
    {
        return QDir::cleanPath(root() + QStringLiteral("/remotes/") + remote + QLatin1Char('/') + path);
    }

    void put(const QString &remote, const QString &path, const QByteArray &bytes, const QDateTime &modified = QDateTime())
    {
        const QString file = remoteFile(remote, path);
        QDir().mkpath(QFileInfo(file).absolutePath());
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(bytes);
        if (modified.isValid())
            out.setFileTime(modified, QFileDevice::FileModificationTime);
    }

    QByteArray read(const QString &remote, const QString &path) const
    {
        QFile in(remoteFile(remote, path));
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    }

    QByteArray calls() const
    {
        QFile in(QDir(root()).filePath(QStringLiteral("calls.log")));
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    }

    QByteArray config() const
    {
        QFile in(QDir(root()).filePath(QStringLiteral("config.json")));
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    }

    static void failing(const char *what)
    {
        if (what)
            qputenv("FAKE_RCLONE_FAIL", what);
        else
            qunsetenv("FAKE_RCLONE_FAIL");
    }

private:
    QTemporaryDir m_dir;
};
