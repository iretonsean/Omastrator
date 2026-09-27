#include "Cloud/CloudStorage.h"
#include "Cloud/CloudUploader.h"
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// The real rclone, end to end, on a `local` remote in a throwaway config. Skipped without rclone.
namespace {
template <typename... Values>
struct Wait {
    std::optional<std::tuple<Values...>> got;
    auto callback()
    {
        return [this](const Values &...values) { got = std::make_tuple(values...); };
    }
    bool wait(int ms = 30000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!got && timer.elapsed() < ms)
            QTest::qWait(20);
        return got.has_value();
    }
};

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class CloudRcloneTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void aLocalRemoteWorksEndToEnd();

private:
    QTemporaryDir m_dir;
    QString m_config;
};

void CloudRcloneTests::initTestCase()
{
    qunsetenv("OMASTRATOR_RCLONE");
    if (QStandardPaths::findExecutable(QStringLiteral("rclone")).isEmpty())
        QSKIP("rclone isn't installed");
    // Never the user's config or cache: rclone reads RCLONE_CONFIG, and we pass --config too.
    m_config = m_dir.filePath(QStringLiteral("rclone.conf"));
    qputenv("RCLONE_CONFIG", m_config.toUtf8());
    qputenv("XDG_CACHE_HOME", m_dir.filePath(QStringLiteral("cache")).toUtf8());
    qputenv("XDG_CONFIG_HOME", m_dir.filePath(QStringLiteral("config")).toUtf8());
}

void CloudRcloneTests::aLocalRemoteWorksEndToEnd()
{
    CloudStorage storage;
    storage.setConfigFile(m_config);
    QVERIFY(CloudStorage::isInstalled());
    Wait<CloudConfigStep> created;
    storage.createRemote(QStringLiteral("scratch"), QStringLiteral("local"), {}, false, created.callback());
    QVERIFY(created.wait());
    QVERIFY2(std::get<0>(*created.got).finished, qPrintable(std::get<0>(*created.got).error));
    QVERIFY(read(m_config).contains("[scratch]"));

    QSignalSpy changed(&storage, &CloudStorage::remotesChanged);
    storage.refreshRemotes();
    QVERIFY(changed.wait(30000));
    QCOMPARE(storage.remotes().size(), 1);
    QCOMPARE(storage.remotes().first().type, QString("local"));

    // A local remote's path is absolute; rclone runs from the root, so a relative one is too.
    const QString folder = m_dir.filePath(QStringLiteral("remote")).mid(1);
    write(QLatin1Char('/') + folder + QStringLiteral("/Designs/logo.omai"), "original");
    const CloudLocation designs{QStringLiteral("scratch"), folder + QStringLiteral("/Designs")};
    Wait<QList<CloudEntry>, QString> listed;
    storage.list(designs, listed.callback());
    QVERIFY(listed.wait());
    QVERIFY2(std::get<1>(*listed.got).isEmpty(), qPrintable(std::get<1>(*listed.got)));
    QCOMPARE(std::get<0>(*listed.got).size(), 1);
    QCOMPARE(std::get<0>(*listed.got).first().name, QString("logo.omai"));

    const CloudLocation logo = designs.child(QStringLiteral("logo.omai"));
    Wait<CloudStamp, QString> base;
    storage.stat(logo, base.callback());
    QVERIFY(base.wait());
    QVERIFY(std::get<0>(*base.got).exists);
    QCOMPARE(std::get<0>(*base.got).size, 8);
    Wait<CloudStamp, QString> missing;
    storage.stat(designs.child(QStringLiteral("none.omai")), missing.callback());
    QVERIFY(missing.wait());
    QVERIFY(!std::get<0>(*missing.got).exists);
    QVERIFY(std::get<1>(*missing.got).isEmpty());

    const QString local = m_dir.filePath(QStringLiteral("cache/logo.omai"));
    Wait<QString> downloaded;
    storage.download(logo, local, downloaded.callback());
    QVERIFY(downloaded.wait());
    QVERIFY2(std::get<0>(*downloaded.got).isEmpty(), qPrintable(std::get<0>(*downloaded.got)));
    QCOMPARE(read(local), QByteArray("original"));

    // Edit, upload against the base, then someone else's change stops the next one.
    write(local, "my edit, saved");
    CloudUploader uploader(storage);
    QSignalSpy uploaded(&uploader, &CloudUploader::uploaded);
    QSignalSpy conflicted(&uploader, &CloudUploader::conflicted);
    uploader.upload(QStringLiteral("tab"), local, logo, std::get<0>(*base.got));
    QVERIFY(uploaded.wait(30000));
    QCOMPARE(read(QLatin1Char('/') + logo.path), QByteArray("my edit, saved"));
    const CloudStamp next = uploaded.first().at(2).value<CloudStamp>();
    write(QLatin1Char('/') + logo.path, "their edit");
    write(local, "my second edit");
    uploader.upload(QStringLiteral("tab"), local, logo, next);
    QVERIFY(conflicted.wait(30000));
    QCOMPARE(read(QLatin1Char('/') + logo.path), QByteArray("their edit"));
    uploader.keepBoth(QStringLiteral("tab"), QDateTime(QDate(2026, 9, 27), QTime(9, 5)));
    QVERIFY(uploaded.wait(30000));
    QCOMPARE(read(QLatin1Char('/') + folder + QStringLiteral("/Designs/logo (conflict 2026-09-27 0905).omai")), QByteArray("my second edit"));

    Wait<QString> made;
    storage.makeFolder(designs.child(QStringLiteral("New Folder")), made.callback());
    QVERIFY(made.wait());
    QVERIFY(QFileInfo(QLatin1Char('/') + folder + QStringLiteral("/Designs/New Folder")).isDir());

    Wait<QString> deleted;
    storage.deleteRemote(QStringLiteral("scratch"), deleted.callback());
    QVERIFY(deleted.wait());
    QVERIFY(!read(m_config).contains("[scratch]"));
    Wait<QList<CloudEntry>, QString> gone;
    storage.list(designs, gone.callback());
    QVERIFY(gone.wait());
    QVERIFY(std::get<1>(*gone.got).contains(QStringLiteral("didn't find section in config file")));
}

QTEST_GUILESS_MAIN(CloudRcloneTests)
#include "CloudRcloneTests.moc"
