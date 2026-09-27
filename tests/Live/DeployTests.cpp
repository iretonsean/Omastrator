#include "Live/Deploy.h"
#include "Live/DeployJob.h"
#include "Live/History.h"
#include "Live/WriteBack.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Deploy-first Live (docs/OS-SUITE.md): .env files, how a project deploys, the
// deploy job, history and GitHub, all with throwaway repositories, a local bare
// remote, and stand-ins for gh, the host CLIs and the terminal.
namespace {
void write(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString git(const QString &folder, const QStringList &arguments)
{
    QString error;
    const QString out = WriteBack::git(folder, arguments, &error);
    if (!error.isEmpty())
        qWarning().noquote() << "git" << arguments << error;
    return out;
}

// Stands in for gh: logged in when $FAKE_GH_LOGGED_IN exists; `repo create` makes the repository on "GitHub",
// which pushInsteadOf sends to a local folder.
constexpr const char *fakeGh =
    "#!/bin/sh\n"
    "echo \"$@\" >> \"$FAKE_GH_LOG\"\n"
    "if [ \"$1 $2\" = 'auth status' ]; then\n"
    "  if [ -f \"$FAKE_GH_LOGGED_IN\" ]; then echo 'github.com'; echo '  ✓ Logged in to github.com account tester (keyring)'; exit 0; fi\n"
    "  echo 'You are not logged into any GitHub hosts. To log in, run: gh auth login' >&2; exit 1\n"
    "fi\n"
    "if [ \"$1 $2\" = 'repo create' ]; then\n"
    "  git init -q --bare \"$FAKE_GH_REMOTES/$3.git\" && git remote add origin \"https://github.com/tester/$3.git\" && git push -q -u origin HEAD; exit $?\n"
    "fi\n"
    "exit 2\n";

const QByteArray secret = "s3cr3t-token-value-4242";
}

class DeployTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_bin;
    int m_repos = 0;

    QString repository(bool withRemote = true)
    {
        const QString folder = m_directory.filePath(QStringLiteral("site%1").arg(++m_repos));
        write(folder + "/index.html", "<h1>Hello</h1>\n");
        write(folder + "/.gitignore", ".env*\n");
        git(folder, {"init", "-q", "-b", "main"});
        git(folder, {"add", "-A"});
        git(folder, {"commit", "-q", "-m", "First"});
        if (withRemote) {
            const QString bare = folder + ".git";
            QProcess::execute(QStringLiteral("git"), {"init", "-q", "--bare", bare});
            git(folder, {"remote", "add", "origin", bare});
            git(folder, {"push", "-q", "-u", "origin", "main"});
        }
        return folder;
    }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git isn't installed.");
        QVERIFY(m_directory.isValid());
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        const QString remotes = m_directory.filePath(QStringLiteral("github"));
        QDir().mkpath(remotes);
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n[url \"" + remotes.toUtf8()
                             + "/\"]\n\tpushInsteadOf = https://github.com/tester/\n");
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        m_bin = m_directory.filePath(QStringLiteral("bin"));
        write(m_bin + "/gh", fakeGh, true);
        qputenv("OMASTRATOR_GH", (m_bin + "/gh").toUtf8());
        qputenv("FAKE_GH_LOG", m_directory.filePath(QStringLiteral("gh.log")).toUtf8());
        qputenv("FAKE_GH_LOGGED_IN", m_directory.filePath(QStringLiteral("gh-logged-in")).toUtf8());
        qputenv("FAKE_GH_REMOTES", remotes.toUtf8());
        write(m_directory.filePath(QStringLiteral("terminal")), "#!/bin/sh\necho \"$@\" > \"$FAKE_TERMINAL_OUT\"\n", true);
        qputenv("OMASTRATOR_TERMINAL", m_directory.filePath(QStringLiteral("terminal")).toUtf8());
        qputenv("FAKE_TERMINAL_OUT", m_directory.filePath(QStringLiteral("terminal.out")).toUtf8());
    }

    void dotenvFilesParseAsDotenvDoes()
    {
        const Deploy::Variables parsed = Deploy::parseEnv("# a comment\n"
                                                          "PLAIN=value\n"
                                                          "export EXPORTED=yes\n"
                                                          "  SPACED = padded  \n"
                                                          "DOUBLE=\"two words # not a comment\\nnext\"\n"
                                                          "SINGLE='raw \\n $HOME'\n"
                                                          "INLINE=kept # dropped\n"
                                                          "MULTI=\"line one\nline two\"\n"
                                                          "EMPTY=\n"
                                                          "not a line\n"
                                                          "PLAIN=later\r\n");
        auto value = [&](const QString &key) {
            for (const auto &[name, text] : parsed)
                if (name == key)
                    return text;
            return QStringLiteral("<missing>");
        };
        QCOMPARE(value("PLAIN"), QStringLiteral("later"));
        QCOMPARE(value("EXPORTED"), QStringLiteral("yes"));
        QCOMPARE(value("SPACED"), QStringLiteral("padded"));
        QCOMPARE(value("DOUBLE"), QStringLiteral("two words # not a comment\nnext"));
        QCOMPARE(value("SINGLE"), QStringLiteral("raw \\n $HOME"));
        QCOMPARE(value("INLINE"), QStringLiteral("kept"));
        QCOMPARE(value("MULTI"), QStringLiteral("line one\nline two"));
        QCOMPARE(value("EMPTY"), QString());
        QCOMPARE(Deploy::keys(parsed), (QStringList{"PLAIN", "EXPORTED", "SPACED", "DOUBLE", "SINGLE", "INLINE", "MULTI", "EMPTY"}));
    }

    void laterFilesWinAndTheFilesWinOverOmastratorsEnvironment()
    {
        const QString folder = m_directory.filePath(QStringLiteral("env"));
        write(folder + "/.env", "API_TOKEN=base\nSHARED=env\nPATH=/nowhere\nHOME=/nobody\n");
        write(folder + "/.env.local", "SHARED=local\n");
        write(folder + "/.env.production", "API_TOKEN=" + secret + "\n");
        write(folder + "/.env.production.local", "EXTRA=1\n");
        QStringList files;
        const Deploy::Variables variables = Deploy::projectEnv(folder, QString(), &files);
        QCOMPARE(files.size(), 4);
        qputenv("SHARED", "from-omastrator");
        const QProcessEnvironment environment = Deploy::environment(variables);
        QCOMPARE(environment.value("API_TOKEN").toUtf8(), secret);
        QCOMPARE(environment.value("SHARED"), QStringLiteral("local"));
        // Where programs and home are stay Omastrator's own.
        QCOMPARE(environment.value("PATH"), qEnvironmentVariable("PATH"));
        QCOMPARE(environment.value("HOME"), qEnvironmentVariable("HOME"));
        qunsetenv("SHARED");
        const QString redacted = Deploy::redact(QStringLiteral("token=%1 and 1").arg(QString::fromUtf8(secret)), variables);
        QCOMPARE(redacted, QStringLiteral("token=[API_TOKEN] and 1"));
    }

    void theDeployCommandIsFoundInOrder()
    {
        const QString folder = m_directory.filePath(QStringLiteral("resolve"));
        QDir().mkpath(folder);
        QCOMPARE(Deploy::resolve(folder).source, QStringLiteral("agent"));
        QVERIFY(Deploy::resolve(folder).viaAgent());
        write(folder + "/deploy.sh", "#!/bin/sh\n");
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("sh ./deploy.sh"));
        QFile(folder + "/deploy.sh").setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("./deploy.sh"));
        if (!QStandardPaths::findExecutable(QStringLiteral("make")).isEmpty()) {
            write(folder + "/Makefile", "build:\n\ttrue\ndeploy: build\n\ttrue\n");
            QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("make deploy"));
        }
        // A host's config counts only when its CLI is installed.
        write(folder + "/fly.toml", "app = 'site'\n");
        write(folder + "/wrangler.toml", "name = 'site'\n");
        write(folder + "/netlify.toml", "[build]\n");
        write(folder + "/.vercel/project.json", "{}");
        QVERIFY(Deploy::resolve(folder).source != QLatin1String("vercel"));
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", m_bin.toUtf8() + ':' + path);
        write(m_bin + "/fly", "#!/bin/sh\n", true);
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("fly deploy"));
        write(m_bin + "/wrangler", "#!/bin/sh\n", true);
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("wrangler deploy"));
        write(folder + "/wrangler.toml", "name = 'site'\npages_build_output_dir = 'dist'\n");
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("wrangler pages deploy"));
        write(m_bin + "/netlify", "#!/bin/sh\n", true);
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("netlify deploy --prod"));
        write(m_bin + "/vercel", "#!/bin/sh\n", true);
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("vercel deploy --prod"));
        qputenv("PATH", path);
        write(folder + "/package.json", R"({"scripts": {"dev": "vite", "deploy:prod": "x"}})");
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("npm run deploy:prod"));
        write(folder + "/package.json", R"({"scripts": {"deploy": "x", "deploy:prod": "x"}})");
        write(folder + "/pnpm-lock.yaml", "");
        QCOMPARE(Deploy::resolve(folder).command, QStringLiteral("pnpm run deploy"));
        QCOMPARE(Deploy::resolve(folder).source, QStringLiteral("package.json"));
        // A remembered command wins, and keeps omastrator.json's other keys.
        write(folder + "/omastrator.json", R"({"dev": "npm run dev"})");
        QVERIFY(Deploy::remember(folder, QStringLiteral("./ship --prod"), folder + "/app").isEmpty());
        const QJsonObject config = QJsonDocument::fromJson(read(folder + "/omastrator.json")).object();
        QCOMPARE(config["dev"].toString(), QStringLiteral("npm run dev"));
        QCOMPARE(config["deploy"].toObject()["cwd"].toString(), QStringLiteral("app"));
        const Deploy::Command remembered = Deploy::resolve(folder);
        QCOMPARE(remembered.command, QStringLiteral("./ship --prod"));
        QCOMPARE(remembered.cwd, folder + "/app");
        QCOMPARE(remembered.source, QStringLiteral("omastrator.json"));
    }

    void settingsAndRecordsAreKeptPerProject()
    {
        const QString folder = repository(false);
        QVERIFY(!Deploy::settings(folder).confirmed);
        QVERIFY(Deploy::saveSettings(folder, {true, false}).isEmpty());
        QVERIFY(Deploy::settings(folder).confirmed);
        QVERIFY(Deploy::settingsPath().startsWith(m_directory.path()));
        const QString head = git(folder, {"rev-parse", "HEAD"}).trimmed();
        QVERIFY(!Deploy::deployed(folder, head));
        QVERIFY(Deploy::addRecord({folder, head, "https://a.example.test", "x", "log", QDateTime::currentDateTime(), false}).isEmpty());
        QVERIFY(!Deploy::deployed(folder, head));
        QVERIFY(Deploy::addRecord({folder, head, "https://b.example.test", "x", "log", QDateTime::currentDateTime(), true}).isEmpty());
        QCOMPARE(Deploy::deployed(folder, head)->url, QStringLiteral("https://b.example.test"));
        QVERIFY(Deploy::logDirectory().startsWith(m_directory.path()));
        QVERIFY(Deploy::newLogPath(folder).startsWith(Deploy::logDirectory()));
        QCOMPARE(Deploy::firstUrl("Building…\nPreview: http://x\nProduction: https://site.example.test/app.\nhttps://later.test\n"),
                 QStringLiteral("https://site.example.test/app"));
        QCOMPARE(Deploy::lastLine("one\n\n  two  \n\n"), QStringLiteral("two"));
        // Each dry line once, then none.
        QStringList lines;
        for (QString line = Deploy::dryLine(); !line.isEmpty(); line = Deploy::dryLine())
            lines << line;
        QVERIFY(lines.size() >= 2);
        QCOMPARE(lines.removeDuplicates(), 0);
    }

    void theJobPushesThenDeploysWithTheProjectsEnvironment()
    {
        const QString folder = repository();
        write(folder + "/.env", "API_TOKEN=placeholder\nSHARED=plain-shared-value\n");
        write(folder + "/.env.production", "API_TOKEN=" + secret + "\n");
        write(folder + "/index.html", "<h1>Hello, again</h1>\n");
        git(folder, {"commit", "-q", "-am", "Second"});
        const QString dump = m_directory.filePath(QStringLiteral("child-env"));
        qputenv("ENV_DUMP", dump.toUtf8());
        const QString command = QStringLiteral("echo \"keys: $(env | cut -d= -f1 | grep -E '^(API_TOKEN|SHARED)$' | sort | tr '\\n' ' ')\"; "
                                               "printf '%s' \"$API_TOKEN\" > \"$ENV_DUMP\"; echo \"token=$API_TOKEN\"; echo \"home=$HOME\"; "
                                               "echo 'Production: https://site.example.test/abc.'");
        DeployJob job;
        QSignalSpy finished(&job, &DeployJob::finished);
        const QString head = git(folder, {"rev-parse", "HEAD"}).trimmed();
        job.start({folder, head, true, {}, true, {command, folder, "omastrator.json"}});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30'000);
        QVERIFY2(finished.front().front().toBool(), qPrintable(job.failure()));
        QCOMPARE(job.url(), QStringLiteral("https://site.example.test/abc"));
        // Pushed first, to the upstream.
        QCOMPARE(git(folder + ".git", {"log", "-1", "--format=%s", "main"}).trimmed(), QStringLiteral("Second"));
        // The child had the values; the log has only the keys.
        QCOMPARE(read(dump), secret);
        const QByteArray log = read(job.log());
        QVERIFY2(log.contains("keys: API_TOKEN SHARED"), log.constData());
        QVERIFY(log.contains("token=[API_TOKEN]"));
        QVERIFY(log.contains("Environment from .env, .env.production: API_TOKEN, SHARED"));
        QVERIFY(log.contains("home=" + qgetenv("HOME")));
        QVERIFY(!log.contains(secret) && !log.contains("plain-shared-value"));
        QVERIFY(job.log().startsWith(Deploy::logDirectory()));
        QCOMPARE(Deploy::deployed(folder, head)->url, QStringLiteral("https://site.example.test/abc"));

        // A failure is one redacted line, and recorded as not deployed.
        DeployJob failing;
        QSignalSpy failed(&failing, &DeployJob::finished);
        failing.start({folder, head, false, {}, true, {"echo building; echo \"denied for $API_TOKEN\" >&2; exit 3", folder, "omastrator.json"}});
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 30'000);
        QVERIFY(!failed.front().front().toBool());
        QCOMPARE(failing.failure(), QStringLiteral("denied for [API_TOKEN]"));
        QVERIFY(!read(failing.log()).contains(secret));
        QCOMPARE(Deploy::records(folder).back().ok, false);

        // With no remote there's nothing to push, which is said, not failed.
        const QString loose = repository(false);
        DeployJob save;
        QSignalSpy saved(&save, &DeployJob::finished);
        save.start({loose, QString(), true, {}, false, {}});
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 30'000);
        QVERIFY(saved.front().front().toBool());
        QVERIFY(save.pushNote().contains(QLatin1String("no remote")));
    }

    void cancelStopsTheCommand()
    {
        const QString folder = repository(false);
        DeployJob job;
        QSignalSpy finished(&job, &DeployJob::finished);
        job.start({folder, QString(), false, {}, true, {"sleep 30", folder, "omastrator.json"}});
        QTRY_COMPARE(job.stage(), DeployJob::Stage::deploying);
        job.cancel();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(job.failure(), QStringLiteral("Cancelled."));
    }

    void historyListsCommitsDeploysAndGitHubLinks()
    {
        QCOMPARE(History::githubUrl("git@github.com:owner/repo.git"), QStringLiteral("https://github.com/owner/repo"));
        QCOMPARE(History::githubUrl("https://github.com/owner/repo"), QStringLiteral("https://github.com/owner/repo"));
        QCOMPARE(History::githubUrl("ssh://git@github.com/owner/repo.git"), QStringLiteral("https://github.com/owner/repo"));
        QCOMPARE(History::githubUrl("https://gitlab.com/owner/repo.git"), QString());
        const QString folder = repository(false);
        write(folder + "/about.html", "<p>About</p>\n");
        git(folder, {"add", "-A"});
        git(folder, {"commit", "-q", "-m", "About page"});
        const QString head = git(folder, {"rev-parse", "HEAD"}).trimmed();
        Deploy::addRecord({folder, head, "https://site.example.test", "x", "log", QDateTime::currentDateTime(), true});
        std::vector<History::Entry> entries = History::list(folder);
        QCOMPARE(entries.size(), size_t(2));
        QCOMPARE(entries[0].subject, QStringLiteral("About page"));
        QCOMPARE(entries[0].author, QStringLiteral("Omastrator Tests"));
        QCOMPARE(entries[0].files, QStringList{"about.html"});
        QCOMPARE(entries[0].deploy->url, QStringLiteral("https://site.example.test"));
        QVERIFY(!entries[1].deploy);
        QVERIFY(entries[0].link.isEmpty());
        // On GitHub, each commit links there.
        git(folder, {"remote", "add", "origin", "git@github.com:tester/site.git"});
        entries = History::list(folder);
        QCOMPARE(entries[0].link, QStringLiteral("https://github.com/tester/site/commit/%1").arg(head));
        QString why;
        const auto target = History::pushTarget(folder, &why);
        QVERIFY(target && target->setUpstream);
        QCOMPARE(target->arguments(), (QStringList{"push", "-u", "origin", "HEAD:refs/heads/main"}));
    }

    void restoreBringsBackAVersionAroundUncommittedWork()
    {
        const QString folder = repository(false);
        const QString first = git(folder, {"rev-parse", "HEAD"}).trimmed();
        write(folder + "/index.html", "<h1>Hello</h1>\n<p>Two</p>\n");
        write(folder + "/new.html", "<p>New</p>\n");
        git(folder, {"add", "-A"});
        git(folder, {"commit", "-q", "-m", "Two"});
        write(folder + "/notes.txt", "mine\n");
        QString error;
        const auto changes = History::restore(folder, first, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changes.size(), size_t(2));
        QVERIFY(WriteBack::apply(changes).isEmpty());
        QCOMPARE(read(folder + "/index.html"), QByteArray("<h1>Hello</h1>\n"));
        QVERIFY(!QFileInfo::exists(folder + "/new.html"));
        QCOMPARE(read(folder + "/notes.txt"), QByteArray("mine\n"));
        History::restore(folder, "0000000", &error);
        QVERIFY(error.contains(QLatin1String("no commit")));
    }

    void gitHubGoesThroughGh()
    {
        QFile::remove(qEnvironmentVariable("FAKE_GH_LOGGED_IN"));
        GitHub::Auth auth = GitHub::status();
        QVERIFY(auth.installed && !auth.loggedIn);
        write(qEnvironmentVariable("FAKE_GH_LOGGED_IN"), "");
        auth = GitHub::status();
        QVERIFY(auth.loggedIn);
        QCOMPARE(auth.account, QStringLiteral("tester"));
        // Connecting is gh's own login, in a terminal; Omastrator never sees a token.
        QVERIFY(GitHub::connect().isEmpty());
        QTRY_COMPARE(read(qEnvironmentVariable("FAKE_TERMINAL_OUT")).trimmed(), (m_bin + "/gh auth login").toUtf8());
        QCOMPARE(GitHub::suggestedName("/x/My Site!"), QStringLiteral("My-Site-"));

        // A new repository is created private, from the folder, and pushed; the job pushes nothing else.
        const QString folder = repository(false);
        DeployJob job;
        QSignalSpy finished(&job, &DeployJob::finished);
        job.start({folder, QString(), true, QStringLiteral("made-here"), false, {}});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30'000);
        QVERIFY2(finished.front().front().toBool(), qPrintable(job.failure()));
        QVERIFY(read(qEnvironmentVariable("FAKE_GH_LOG")).contains("repo create made-here --private --source . --push"));
        QCOMPARE(History::remotes(folder), QStringList{"origin"});
        QCOMPARE(History::githubPage(folder), QStringLiteral("https://github.com/tester/made-here"));
        QCOMPARE(git(m_directory.filePath(QStringLiteral("github/made-here.git")), {"log", "-1", "--format=%s", "main"}).trimmed(), QStringLiteral("First"));
    }
};

QTEST_GUILESS_MAIN(DeployTests)
#include "DeployTests.moc"
