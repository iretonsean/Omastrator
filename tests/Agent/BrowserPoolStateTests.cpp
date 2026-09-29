#include "Agent/BrowserPoolState.h"
#include "Agent/DesignCli.h"
#include "FakeHyprctl.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

// Reset ends a Browser View browser left by a crash (docs/BROWSER-VIEW.md), and only one that is ours.
class BrowserPoolStateTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    std::unique_ptr<FakeHyprctl> m_hyprctl;

    // Stands in for Chromium: a shell waiting on its input, whose command line carries the profile argument.
    std::unique_ptr<QProcess> fakeBrowser(const QString &profile)
    {
        auto process = std::make_unique<QProcess>();
        process->start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("read line"), QStringLiteral("chromium"),
                                                   QStringLiteral("--user-data-dir=") + profile});
        process->waitForStarted();
        return process;
    }

    // The shell keeps its own command line while it waits.
    static bool namesProfile(qint64 pid, const QString &profile)
    {
        QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
        return file.open(QIODevice::ReadOnly) && file.readAll().contains(("--user-data-dir=" + profile).toUtf8());
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("none.sock")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        m_hyprctl = std::make_unique<FakeHyprctl>(m_directory.path());
        qputenv("OMASTRATOR_HYPRCTL", m_hyprctl->path().toUtf8());
    }

    void init() { BrowserPoolState::clear(); }

    void theFileRoundTripsAndClears()
    {
        QVERIFY(BrowserPoolState::read().isEmpty());
        QVERIFY(BrowserPoolState::write({4242, QStringLiteral("/a/profile")}).isEmpty());
        const BrowserPoolState::State state = BrowserPoolState::read();
        QCOMPARE(state.pid, 4242);
        QCOMPARE(state.profile, QStringLiteral("/a/profile"));
        BrowserPoolState::clear();
        QVERIFY(BrowserPoolState::read().isEmpty());
    }

    void resetEndsABrowserThatNamesOurProfile()
    {
        const QString profile = m_directory.filePath(QStringLiteral("profile"));
        auto browser = fakeBrowser(profile);
        QTRY_VERIFY(namesProfile(browser->processId(), profile));
        QVERIFY(BrowserPoolState::write({browser->processId(), profile}).isEmpty());
        QString out, err;
        QTextStream outStream(&out), errStream(&err);
        QCOMPARE(DesignCli::runReset(outStream, errStream), 0);
        outStream.flush();
        QVERIFY(out.contains(QStringLiteral("Stopped Omastrator's browser.")));
        QVERIFY(browser->waitForFinished(5000));
        QVERIFY(!QFileInfo::exists(BrowserPoolState::path()));
    }

    void aReusedPidIsLeftAlone()
    {
        auto stranger = fakeBrowser(m_directory.filePath(QStringLiteral("someone-elses")));
        QTRY_VERIFY(namesProfile(stranger->processId(), m_directory.filePath(QStringLiteral("someone-elses"))));
        QVERIFY(BrowserPoolState::write({stranger->processId(), m_directory.filePath(QStringLiteral("profile"))}).isEmpty());
        QVERIFY(!BrowserPoolState::endLeftover());
        QCOMPARE(stranger->state(), QProcess::Running);
        // The file goes either way, so a later reset doesn't look again.
        QVERIFY(!QFileInfo::exists(BrowserPoolState::path()));
        stranger->kill();
        stranger->waitForFinished();
    }

    void nothingNotedEndsNothing() { QVERIFY(!BrowserPoolState::endLeftover()); }
};

QTEST_GUILESS_MAIN(BrowserPoolStateTests)
#include "BrowserPoolStateTests.moc"
