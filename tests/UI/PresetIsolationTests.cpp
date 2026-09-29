#include "UI/PresetStore.h"
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// A test that reaches the user's own presets.json would read or overwrite it. In test mode
// PresetStore stops the process unless the config folder is under the temporary directory;
// these runs prove it against a sentinel HOME, which lives beside the executable because the
// system temporary directory is what counts as safe.
class PresetIsolationTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    // Runs only as the child of the two tests below.
    void probe()
    {
        if (qEnvironmentVariableIsEmpty("OMASTRATOR_PRESET_PROBE"))
            QSKIP("Only run by the tests that start this one.");
        QVERIFY(PresetStore::read(PresetStore::documents).saved.empty());
        QVERIFY(PresetStore::write(PresetStore::documents, {{{"Probe", QSizeF(10, 10), LengthUnit::px}}, {}}).isEmpty());
    }
    void aTestUsingTheRealConfigIsStopped();
    void aTemporaryConfigIsUsedAndTheRealOneIsNotTouched();

private:
    static QByteArray sentinel() { return R"({"documents": {"saved": [{"name": "SENTINEL", "width": 1, "height": 1}]}})"; }
    // Starts this executable's probe with a fake HOME holding a sentinel presets.json.
    static QProcess *runProbe(QTemporaryDir &home, const QString &xdg)
    {
        QDir().mkpath(home.filePath(".config/omastrator"));
        QFile file(home.filePath(".config/omastrator/presets.json"));
        if (!file.open(QIODevice::WriteOnly))
            return nullptr;
        file.write(sentinel());
        file.close();
        auto *child = new QProcess;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("HOME", home.path());
        env.insert("OMASTRATOR_PRESET_PROBE", "1");
        env.insert("QT_QPA_PLATFORM", "offscreen");
        if (xdg.isEmpty())
            env.remove("XDG_CONFIG_HOME");
        else
            env.insert("XDG_CONFIG_HOME", xdg);
        child->setProcessEnvironment(env);
        child->start(QCoreApplication::applicationFilePath(), {"probe"});
        child->waitForFinished(60000);
        return child;
    }
};

void PresetIsolationTests::aTestUsingTheRealConfigIsStopped()
{
    QTemporaryDir home(QCoreApplication::applicationDirPath() + "/preset-isolation-XXXXXX");
    QVERIFY(home.isValid());
    for (const QString &xdg : {QString(), home.filePath(".config")}) {
        std::unique_ptr<QProcess> child(runProbe(home, xdg));
        QVERIFY(child);
        QVERIFY2(child->exitStatus() == QProcess::CrashExit || child->exitCode() != 0, "the probe reached the real config and carried on");
        // Qt Test reports a fatal message on stdout, a plain run on stderr.
        QVERIFY((child->readAllStandardOutput() + child->readAllStandardError()).contains("isn't temporary"));
        QFile file(home.filePath(".config/omastrator/presets.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), sentinel());
    }
}

void PresetIsolationTests::aTemporaryConfigIsUsedAndTheRealOneIsNotTouched()
{
    QTemporaryDir home(QCoreApplication::applicationDirPath() + "/preset-isolation-XXXXXX"), config;
    QVERIFY(home.isValid() && config.isValid());
    std::unique_ptr<QProcess> child(runProbe(home, config.path()));
    QVERIFY(child);
    QVERIFY2(child->exitStatus() == QProcess::NormalExit && child->exitCode() == 0, child->readAllStandardOutput() + child->readAllStandardError());
    QVERIFY(QFileInfo::exists(config.filePath("omastrator/presets.json")));
    QFile file(home.filePath(".config/omastrator/presets.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), sentinel());
}

QTEST_MAIN(PresetIsolationTests)
#include "PresetIsolationTests.moc"
