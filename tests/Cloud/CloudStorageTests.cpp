#include "Cloud/CloudProviders.h"
#include "Cloud/CloudStorage.h"
#include "Cloud/CloudUploader.h"
#include "FakeCloud.h"
#include "IO/FileError.h"
#include <QSignalSpy>
#include <QtTest>

// rclone through the fake: remotes, listing, transfers, connecting, uploads and conflicts.
namespace {
QStringList logged;

void keepLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    logged << message;
}

bool loggedSecret()
{
    return logged.join(QLatin1Char('\n')).contains(QLatin1String(FakeCloud::secret)) || logged.join(QLatin1Char('\n')).contains(QLatin1String("FAKE-REFRESH"));
}

// Waits for one callback's values.
template <typename... Values>
struct Wait {
    std::optional<std::tuple<Values...>> got;
    auto callback()
    {
        return [this](const Values &...values) { got = std::make_tuple(values...); };
    }
    bool wait(int ms = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!got && timer.elapsed() < ms)
            QTest::qWait(10);
        return got.has_value();
    }
};
}

class CloudStorageTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qInstallMessageHandler(keepLog); }
    void cleanupTestCase()
    {
        qInstallMessageHandler(nullptr);
        QVERIFY(!loggedSecret());
    }
    void locationsParseAndNavigate();
    void cachePathsStayInTheCache();
    void stampsCompareByHashThenSizeAndTime();
    void errorsAreCleanAndHideSecrets();
    void questionsParseWithoutSecretDefaults();
    void missingRcloneSaysSo();
    void listsRemotesByNameAndTypeOnly();
    void browsesFoldersFirst();
    void statsDownloadsUploadsAndMakesFolders();
    void aSlowJobCancels();
    void connectingAnswersRclonesQuestions();
    void disconnectingDeletesTheRemote();
    void uploadsWhenTheRemoteIsUnchanged();
    void aChangedRemoteIsAConflict();
    void keepBothUploadsACopy();
    void offlineUploadsRetryUntilTheyWork();
    void aSaveDuringAnUploadGoesAgain();
};

void CloudStorageTests::locationsParseAndNavigate()
{
    QVERIFY(!CloudLocation::parse(QStringLiteral("/home/me/a.omai")));
    QVERIFY(!CloudLocation::parse(QStringLiteral("a.omai")));
    QVERIFY(!CloudLocation::parse(QStringLiteral("-x:foo")));
    const CloudLocation file = CloudLocation::parse(QStringLiteral("my drive:/Designs//logo.omai")).value();
    QCOMPARE(file.remote, QString("my drive"));
    QCOMPARE(file.path, QString("Designs/logo.omai"));
    QCOMPARE(file.toString(), QString("my drive:Designs/logo.omai"));
    QCOMPARE(file.fileName(), QString("logo.omai"));
    QCOMPARE(file.parent().toString(), QString("my drive:Designs"));
    QCOMPARE(file.parent().parent().toString(), QString("my drive:"));
    QCOMPARE(file.parent().child("x.svg").path, QString("Designs/x.svg"));
    QCOMPARE(file.segments(), QStringList({"Designs", "logo.omai"}));
    QCOMPARE(CloudUploader::conflictName("logo.omai", QDateTime(QDate(2026, 9, 27), QTime(14, 12))), QString("logo (conflict 2026-09-27 1412).omai"));
}

void CloudStorageTests::cachePathsStayInTheCache()
{
    FakeCloud cloud;
    const QString path = CloudCache::localPath(CloudLocation{"work", "Designs/logo.omai"});
    QCOMPARE(path, cloud.cacheRoot() + "/work/Designs/logo.omai");
    QVERIFY_THROWS_EXCEPTION(FileError, CloudCache::localPath(CloudLocation{"work", "../../escape.omai"}));
    QVERIFY_THROWS_EXCEPTION(FileError, CloudCache::localPath(CloudLocation{"work", ""}));
    const CloudLocation where{"work", "a.omai"};
    QVERIFY(!CloudCache::recorded(where));
    CloudCache::record(where, CloudStamp{true, 12, QDateTime::fromSecsSinceEpoch(1000), {{"md5", "abc"}}});
    QCOMPARE(CloudCache::recorded(where)->size, 12);
    QCOMPARE(CloudCache::recorded(where)->hashes.value("md5").toString(), QString("abc"));
    CloudCache::forget(where);
    QVERIFY(!CloudCache::recorded(where));
}

void CloudStorageTests::stampsCompareByHashThenSizeAndTime()
{
    const QDateTime when = QDateTime::fromSecsSinceEpoch(1000);
    const CloudStamp a{true, 10, when, {{"md5", "aa"}}};
    QVERIFY(a.sameVersion(CloudStamp{true, 10, when.addMSecs(400), {}}));
    QVERIFY(!a.sameVersion(CloudStamp{true, 11, when, {}}));
    QVERIFY(!a.sameVersion(CloudStamp{true, 10, when.addSecs(5), {}}));
    // A shared hash wins over the time a backend rounded.
    QVERIFY(a.sameVersion(CloudStamp{true, 10, when.addSecs(5), {{"md5", "AA"}}}));
    QVERIFY(!a.sameVersion(CloudStamp{true, 10, when, {{"md5", "bb"}}}));
    QVERIFY(!a.sameVersion(CloudStamp{}));
    QVERIFY(CloudStamp{}.sameVersion(CloudStamp{}));
    QCOMPARE(CloudStamp::fromMap(a.toMap()).modified, when);
}

void CloudStorageTests::errorsAreCleanAndHideSecrets()
{
    QCOMPARE(CloudStorage::cleanError("2026/09/27 00:10:48 NOTICE: Failed to lsjson: directory not found\n", 3), QString("Directory not found."));
    const QByteArray retried = "2026/09/27 00:10:48 ERROR : x: error reading source root directory: directory not found\n"
                               "2026/09/27 00:10:48 ERROR : Attempt 1/3 failed with 1 errors and: directory not found\n"
                               "2026/09/27 00:10:48 NOTICE: Failed to copyto: couldn't connect: no such host\n";
    QCOMPARE(CloudStorage::cleanError(retried, 1), QString("Couldn't connect: no such host."));
    const QByteArray usage = "Error: authSRPComplete: sign in failed: incorrect username or password\nUsage:\n  rclone config create name type\n";
    QCOMPARE(CloudStorage::cleanError(usage, 1), QString("AuthSRPComplete: sign in failed: incorrect username or password."));
    QCOMPARE(CloudStorage::cleanError("", 7), QString("rclone stopped with code 7."));
    const QString leaked = CloudStorage::cleanError("ERROR : token = {\"access_token\":\"FAKE-SECRET-TOKEN-7f3a\"} expired\n", 1);
    QVERIFY(!leaked.contains("FAKE-SECRET"));
    QVERIFY(!CloudStorage::scrub("password=hunter2 then").contains("hunter2"));
    QVERIFY(!CloudStorage::scrub("\"refresh_token\":\"1//abc\"").contains("1//abc"));
    QVERIFY(!CloudStorage::scrub("x ya29.a0AfH6SMBxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx y").contains("a0AfH6SMB"));
    QCOMPARE(CloudStorage::scrub("scratch:tmp/CloudRcloneTests-pWByMH/remote/Designs/a_really_long_file_name_for_a_poster.omai"),
             QString("scratch:tmp/CloudRcloneTests-pWByMH/remote/Designs/a_really_long_file_name_for_a_poster.omai"));
    QCOMPARE(CloudStorage::scrub("Visit http://127.0.0.1:53682/auth?state=abc"), QString("Visit http://127.0.0.1:53682/auth?state=abc"));
}

void CloudStorageTests::questionsParseWithoutSecretDefaults()
{
    const QByteArray json = R"json({"State":"s1","Option":{"Name":"config_driveid","Help":"Choose a drive","DefaultStr":"b!x","Exclusive":true,
        "Examples":[{"Value":"b!x","Help":"OneDrive (personal)"},{"Value":"b!y","Help":"Documents"}],"IsPassword":false},"Error":""})json";
    const CloudConfigStep step = CloudStorage::parseStep(json);
    QVERIFY(!step.finished);
    QCOMPARE(step.question->name, QString("config_driveid"));
    QCOMPARE(step.question->choices, QStringList({"b!x", "b!y"}));
    QCOMPARE(step.question->defaultValue, QString("b!x"));
    const CloudConfigStep secret = CloudStorage::parseStep(R"({"State":"p","Option":{"Name":"pass","IsPassword":true,"DefaultStr":"obscured"},"Error":""})");
    QVERIFY(secret.question->password);
    QVERIFY(secret.question->defaultValue.isEmpty());
    QVERIFY(CloudStorage::parseStep(R"({"State":"","Option":null,"Error":""})").finished);
    const CloudConfigStep failed = CloudStorage::parseStep(R"({"State":"","Option":null,"Error":"bad"})");
    QVERIFY(!failed.finished);
    QCOMPARE(failed.error, QString("bad"));
}

void CloudStorageTests::missingRcloneSaysSo()
{
    FakeCloud cloud;
    qputenv("OMASTRATOR_RCLONE", cloud.root().toUtf8() + "/no-such-rclone");
    QVERIFY(!CloudStorage::isInstalled());
    CloudStorage storage;
    Wait<QList<CloudEntry>, QString> listed;
    storage.list(CloudLocation{"x", ""}, listed.callback());
    QVERIFY(listed.wait());
    QCOMPARE(std::get<1>(*listed.got), QString("rclone isn't installed, so cloud storage can't be reached."));
    QSignalSpy changed(&storage, &CloudStorage::remotesChanged);
    storage.refreshRemotes();
    QVERIFY(storage.remotes().isEmpty());
}

void CloudStorageTests::listsRemotesByNameAndTypeOnly()
{
    FakeCloud cloud;
    cloud.addRemote("work drive", "drive");
    cloud.addRemote("box", "dropbox");
    QVERIFY(CloudStorage::isInstalled());
    CloudStorage storage;
    QSignalSpy changed(&storage, &CloudStorage::remotesChanged);
    storage.refreshRemotes();
    QVERIFY(changed.wait());
    QCOMPARE(storage.remotes().size(), 2);
    QCOMPARE(storage.remote("work drive")->type, QString("drive"));
    QCOMPARE(storage.serviceName("work drive"), QString("Google Drive"));
    QCOMPARE(storage.serviceName("box"), QString("Dropbox"));
    QCOMPARE(CloudProviders::forType("webdav").name, QString("Nextcloud or WebDAV"));
    QCOMPARE(CloudProviders::all().back().name, QString("Other (any rclone backend)"));
    // Omastrator asks for names and types, never the config itself.
    QVERIFY(!cloud.calls().contains("config dump"));
    QVERIFY(!cloud.calls().contains("config show"));
}

void CloudStorageTests::browsesFoldersFirst()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    cloud.put("work", "b.omai", "x");
    cloud.put("work", "a.svg", "y");
    cloud.put("work", "Zeta/c.omai", "z");
    CloudStorage storage;
    Wait<QList<CloudEntry>, QString> listed;
    storage.list(CloudLocation{"work", ""}, listed.callback());
    QVERIFY(listed.wait());
    const QList<CloudEntry> entries = std::get<0>(*listed.got);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].name, QString("Zeta"));
    QVERIFY(entries[0].isDir);
    QCOMPARE(entries[1].name, QString("a.svg"));
    QCOMPARE(entries[2].size, 1);
    QVERIFY(entries[2].modified.isValid());
    Wait<QList<CloudEntry>, QString> missing;
    storage.list(CloudLocation{"nope", ""}, missing.callback());
    QVERIFY(missing.wait());
    QVERIFY(std::get<1>(*missing.got).contains("didn't find section in config file"));
}

void CloudStorageTests::statsDownloadsUploadsAndMakesFolders()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    cloud.put("work", "Designs/logo.omai", "hello");
    CloudStorage storage;
    Wait<CloudStamp, QString> stat;
    storage.stat(CloudLocation{"work", "Designs/logo.omai"}, stat.callback());
    QVERIFY(stat.wait());
    const CloudStamp stamp = std::get<0>(*stat.got);
    QVERIFY(stamp.exists);
    QCOMPARE(stamp.size, 5);
    QVERIFY(!stamp.hashes.value("md5").toString().isEmpty());
    Wait<CloudStamp, QString> absent;
    storage.stat(CloudLocation{"work", "Designs/none.omai"}, absent.callback());
    QVERIFY(absent.wait());
    QVERIFY(!std::get<0>(*absent.got).exists);
    QVERIFY(std::get<1>(*absent.got).isEmpty());

    const QString local = CloudCache::localPath(CloudLocation{"work", "Designs/logo.omai"});
    Wait<QString> downloaded;
    storage.download(CloudLocation{"work", "Designs/logo.omai"}, local, downloaded.callback());
    QVERIFY(downloaded.wait());
    QVERIFY(std::get<0>(*downloaded.got).isEmpty());
    QFile copy(local);
    QVERIFY(copy.open(QIODevice::ReadOnly));
    QCOMPARE(copy.readAll(), QByteArray("hello"));

    Wait<QString> made;
    storage.makeFolder(CloudLocation{"work", "New Folder"}, made.callback());
    QVERIFY(made.wait());
    QVERIFY(QFileInfo(cloud.remoteFile("work", "New Folder")).isDir());
    Wait<QString> uploaded;
    storage.upload(local, CloudLocation{"work", "New Folder/copy.omai"}, uploaded.callback());
    QVERIFY(uploaded.wait());
    QCOMPARE(cloud.read("work", "New Folder/copy.omai"), QByteArray("hello"));

    FakeCloud::failing("upload");
    Wait<QString> offline;
    storage.upload(local, CloudLocation{"work", "x.omai"}, offline.callback());
    QVERIFY(offline.wait());
    QCOMPARE(std::get<0>(*offline.got), QString("Couldn't connect: dial tcp: lookup api.example.com: no such host."));
}

void CloudStorageTests::aSlowJobCancels()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    qputenv("FAKE_RCLONE_DELAY_MS", "5000");
    CloudStorage storage;
    Wait<QList<CloudEntry>, QString> listed;
    QElapsedTimer timer;
    timer.start();
    CloudJob *job = storage.list(CloudLocation{"work", ""}, listed.callback());
    QVERIFY(job->isRunning());
    job->cancel();
    QVERIFY(listed.wait());
    QVERIFY(timer.elapsed() < 4000);
    QCOMPARE(std::get<1>(*listed.got), QString("Cancelled."));
}

void CloudStorageTests::connectingAnswersRclonesQuestions()
{
    FakeCloud cloud;
    CloudStorage storage;
    // A browser sign-in: rclone asks whether to use the browser, then the token arrives.
    Wait<CloudConfigStep> first;
    storage.createRemote("gdrive", "drive", {}, false, first.callback());
    QVERIFY(first.wait());
    const CloudConfigStep asked = std::get<0>(*first.got);
    QVERIFY(!asked.finished);
    QCOMPARE(asked.question->name, QString("config_is_local"));
    Wait<CloudConfigStep> second;
    storage.answer("gdrive", asked.question->state, "true", second.callback());
    QVERIFY(second.wait());
    QVERIFY(std::get<0>(*second.got).finished);
    QVERIFY(cloud.config().contains(FakeCloud::secret));
    // A key-based one: fields go to rclone as key=value, with passwords obscured by rclone.
    Wait<CloudConfigStep> webdav;
    storage.createRemote("nextcloud", "webdav", {{"url", "https://cloud.example.com"}, {"user", "me"}, {"pass", "hunter2"}}, false,
                         webdav.callback());
    QVERIFY(webdav.wait());
    QVERIFY(std::get<0>(*webdav.got).finished);
    QVERIFY(cloud.calls().contains("config create nextcloud webdav url=https://cloud.example.com user=me pass=hunter2 --obscure --non-interactive"));
    // A 2FA question, answered wrong, then right.
    Wait<CloudConfigStep> icloud;
    storage.createRemote("icloud", "iclouddrive", {{"apple_id", "me@icloud.com"}, {"password", "pw"}}, false, icloud.callback());
    QVERIFY(icloud.wait());
    QCOMPARE(std::get<0>(*icloud.got).question->name, QString("config_2fa"));
    Wait<CloudConfigStep> wrong;
    storage.answer("icloud", "2fa", "000000", wrong.callback());
    QVERIFY(wrong.wait());
    QCOMPARE(std::get<0>(*wrong.got).question->error, QString("Incorrect code"));
    Wait<CloudConfigStep> right;
    storage.answer("icloud", "2fa", "123456", right.callback());
    QVERIFY(right.wait());
    QVERIFY(std::get<0>(*right.got).finished);
    // rclone's failure, with a token in its stderr, arrives clean.
    FakeCloud::failing("config");
    qputenv("FAKE_RCLONE_LEAK", "1");
    Wait<CloudConfigStep> failed;
    storage.createRemote("broken", "drive", {}, false, failed.callback());
    QVERIFY(failed.wait());
    QCOMPARE(std::get<0>(*failed.got).error, QString("The service said no."));
    QVERIFY(!loggedSecret());
}

void CloudStorageTests::disconnectingDeletesTheRemote()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    CloudStorage storage;
    Wait<QString> deleted;
    storage.deleteRemote("work", deleted.callback());
    QVERIFY(deleted.wait());
    QSignalSpy changed(&storage, &CloudStorage::remotesChanged);
    storage.refreshRemotes();
    QVERIFY(changed.wait());
    QVERIFY(storage.remotes().isEmpty());
}

void CloudStorageTests::uploadsWhenTheRemoteIsUnchanged()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    cloud.put("work", "a.omai", "one", QDateTime::currentDateTime().addSecs(-100));
    CloudStorage storage;
    Wait<CloudStamp, QString> base;
    storage.stat(CloudLocation{"work", "a.omai"}, base.callback());
    QVERIFY(base.wait());
    const QString local = CloudCache::localPath(CloudLocation{"work", "a.omai"});
    QDir().mkpath(QFileInfo(local).absolutePath());
    QFile file(local);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("two, longer");
    file.close();
    CloudUploader uploader(storage);
    QSignalSpy done(&uploader, &CloudUploader::uploaded);
    uploader.upload("tab", local, CloudLocation{"work", "a.omai"}, std::get<0>(*base.got));
    QVERIFY(uploader.isPending("tab"));
    QVERIFY(done.wait());
    QVERIFY(!uploader.isPending("tab"));
    QCOMPARE(cloud.read("work", "a.omai"), QByteArray("two, longer"));
    QCOMPARE(done.first().at(2).value<CloudStamp>().size, 11);
}

void CloudStorageTests::aChangedRemoteIsAConflict()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    cloud.put("work", "a.omai", "one");
    CloudStorage storage;
    Wait<CloudStamp, QString> base;
    storage.stat(CloudLocation{"work", "a.omai"}, base.callback());
    QVERIFY(base.wait());
    cloud.put("work", "a.omai", "theirs!");
    const QString local = CloudCache::localPath(CloudLocation{"work", "a.omai"});
    QDir().mkpath(QFileInfo(local).absolutePath());
    QFile file(local);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("mine");
    file.close();
    CloudUploader uploader(storage);
    QSignalSpy conflicts(&uploader, &CloudUploader::conflicted);
    QSignalSpy done(&uploader, &CloudUploader::uploaded);
    uploader.upload("tab", local, CloudLocation{"work", "a.omai"}, std::get<0>(*base.got));
    QVERIFY(conflicts.wait());
    QCOMPARE(uploader.status("tab").phase, CloudUploader::Phase::conflict);
    QCOMPARE(uploader.status("tab").theirs.size, 7);
    QCOMPARE(cloud.read("work", "a.omai"), QByteArray("theirs!"));
    // A save while waiting on the answer doesn't overwrite either.
    uploader.upload("tab", local, CloudLocation{"work", "a.omai"}, std::get<0>(*base.got));
    QTest::qWait(300);
    QCOMPARE(cloud.read("work", "a.omai"), QByteArray("theirs!"));
    uploader.overwrite("tab");
    QVERIFY(done.wait());
    QCOMPARE(cloud.read("work", "a.omai"), QByteArray("mine"));
}

void CloudStorageTests::keepBothUploadsACopy()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    cloud.put("work", "Designs/a.omai", "one");
    CloudStorage storage;
    Wait<CloudStamp, QString> base;
    storage.stat(CloudLocation{"work", "Designs/a.omai"}, base.callback());
    QVERIFY(base.wait());
    cloud.put("work", "Designs/a.omai", "theirs!");
    const QString local = CloudCache::localPath(CloudLocation{"work", "Designs/a.omai"});
    QDir().mkpath(QFileInfo(local).absolutePath());
    QFile file(local);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("mine");
    file.close();
    CloudUploader uploader(storage);
    QSignalSpy conflicts(&uploader, &CloudUploader::conflicted);
    QSignalSpy done(&uploader, &CloudUploader::uploaded);
    uploader.upload("tab", local, CloudLocation{"work", "Designs/a.omai"}, std::get<0>(*base.got));
    QVERIFY(conflicts.wait());
    uploader.keepBoth("tab", QDateTime(QDate(2026, 9, 27), QTime(14, 12)));
    QVERIFY(done.wait());
    QCOMPARE(done.first().at(1).value<CloudLocation>().path, QString("Designs/a (conflict 2026-09-27 1412).omai"));
    QCOMPARE(cloud.read("work", "Designs/a (conflict 2026-09-27 1412).omai"), QByteArray("mine"));
    QCOMPARE(cloud.read("work", "Designs/a.omai"), QByteArray("theirs!"));
}

void CloudStorageTests::offlineUploadsRetryUntilTheyWork()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    CloudStorage storage;
    CloudUploader uploader(storage);
    uploader.setRetryDelays(100, 400);
    const QString local = CloudCache::localPath(CloudLocation{"work", "new.omai"});
    QDir().mkpath(QFileInfo(local).absolutePath());
    QFile file(local);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("fresh");
    file.close();
    FakeCloud::failing("offline");
    QSignalSpy done(&uploader, &CloudUploader::uploaded);
    uploader.upload("tab", local, CloudLocation{"work", "new.omai"}, CloudStamp{});
    QTRY_VERIFY(uploader.status("tab").attempts >= 3);
    QCOMPARE(uploader.status("tab").phase == CloudUploader::Phase::waiting || uploader.status("tab").phase == CloudUploader::Phase::checking, true);
    QVERIFY(uploader.status("tab").error.contains("no such host"));
    // Backoff doubles up to the cap.
    QCOMPARE(uploader.status("tab").retryInMs, 400);
    FakeCloud::failing(nullptr);
    QVERIFY(done.wait());
    QCOMPARE(cloud.read("work", "new.omai"), QByteArray("fresh"));
    // Cancelling one stops it for good.
    FakeCloud::failing("offline");
    uploader.upload("other", local, CloudLocation{"work", "other.omai"}, CloudStamp{});
    QTRY_VERIFY(uploader.status("other").attempts >= 1);
    uploader.cancel("other");
    QVERIFY(!uploader.isPending("other"));
}

void CloudStorageTests::aSaveDuringAnUploadGoesAgain()
{
    FakeCloud cloud;
    cloud.addRemote("work", "drive");
    CloudStorage storage;
    CloudUploader uploader(storage);
    const QString local = CloudCache::localPath(CloudLocation{"work", "a.omai"});
    QDir().mkpath(QFileInfo(local).absolutePath());
    QFile file(local);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("first");
    file.close();
    qputenv("FAKE_RCLONE_DELAY_MS", "300");
    QSignalSpy done(&uploader, &CloudUploader::uploaded);
    uploader.upload("tab", local, CloudLocation{"work", "a.omai"}, CloudStamp{});
    QTRY_COMPARE(uploader.status("tab").phase, CloudUploader::Phase::uploading);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("second, newer");
    file.close();
    uploader.upload("tab", local, CloudLocation{"work", "a.omai"}, CloudStamp{});
    QTRY_VERIFY_WITH_TIMEOUT(!uploader.isPending("tab"), 15000);
    // The second went up against the first's stamp, not as a conflict.
    QCOMPARE(done.count(), 2);
    QCOMPARE(cloud.read("work", "a.omai"), QByteArray("second, newer"));
}

QTEST_GUILESS_MAIN(CloudStorageTests)
#include "CloudStorageTests.moc"
