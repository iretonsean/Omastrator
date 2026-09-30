#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

// `omastrator --help` and `--version` run the real binary with no usable display and no agent socket.
class CliTests : public QObject {
    Q_OBJECT

    // Runs the binary with an unusable display and a socket path inside `home`; returns the exit code.
    static int run(const QTemporaryDir &home, const QStringList &args, QByteArray &output)
    {
        QProcess process;
        QProcessEnvironment environment;
        environment.insert(QStringLiteral("PATH"), qEnvironmentVariable("PATH"));
        environment.insert(QStringLiteral("HOME"), home.path());
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), home.path());
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), home.filePath(QStringLiteral("config")));
        environment.insert(QStringLiteral("OMASTRATOR_SOCKET"), home.filePath(QStringLiteral("omastrator.sock")));
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("/nonexistent"));
        environment.insert(QStringLiteral("DISPLAY"), QStringLiteral(":9999"));
        process.setProcessEnvironment(environment);
        process.start(QStringLiteral(OMASTRATOR_BINARY), args);
        if (!process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished();
            return -1;
        }
        output = process.readAllStandardOutput();
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
    }

    static void checkNoSocket(const QTemporaryDir &home)
    {
        QVERIFY(QDir(home.path()).entryList(QStringList{"*.sock"}, QDir::AllEntries | QDir::System).isEmpty());
    }

private slots:
    void helpListsEveryCommand_data()
    {
        QTest::addColumn<QString>("flag");
        QTest::newRow("long") << "--help";
        QTest::newRow("short") << "-h";
    }

    void helpListsEveryCommand()
    {
        QFETCH(QString, flag);
        QTemporaryDir home;
        QByteArray output;
        QCOMPARE(run(home, {flag}, output), 0);
        for (const char *each : {"omastrator [FILE...]", "agent", "--mcp", "status", "island", "setup", "design", "desk", "daemon", "--daemon", "reset",
                                 "browser-host", "--help", "--version"})
            QVERIFY2(output.contains(each), each);
        checkNoSocket(home);
    }

    void versionIsOneLine_data()
    {
        QTest::addColumn<QString>("flag");
        QTest::newRow("long") << "--version";
        QTest::newRow("short") << "-V";
    }

    void versionIsOneLine()
    {
        QFETCH(QString, flag);
        QTemporaryDir home;
        QByteArray output;
        QCOMPARE(run(home, {flag}, output), 0);
        QCOMPARE(output, QByteArray("omastrator " OMASTRATOR_VERSION "\n"));
        QCOMPARE(output, QByteArray("omastrator 0.1.0\n"));
        checkNoSocket(home);
    }
};

QTEST_GUILESS_MAIN(CliTests)
#include "CliTests.moc"
