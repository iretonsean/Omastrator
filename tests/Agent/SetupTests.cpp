#include "Agent/Setup.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Phase 3 of docs/OS-SUITE.md: `omastrator setup` and `--remove` in a temporary HOME.
namespace {
const QByteArray userShellJson = "{\n  \"version\": 1,\n  \"bar\": {\n    \"layout\": {\n      \"right\": [\n        {\n          \"id\": \"omarchy.audio\",\n          \"format\": \"HH\\n\\u2014\\nmm\"\n        }\n      ]\n    }\n  },\n  \"plugins\": [\n    {\n      \"id\": \"someone.else\"\n    }\n  ]\n}\n";
const QByteArray userMenu = "{\n  // My own entries.\n  \"personal\": {\"icon\": \"x\", \"label\": \"Personal\"},\n  \"personal.notes\": {\"label\": \"Notes\", \"action\": \"true\"}\n}\n";
const QByteArray userHypr = "-- My Hyprland.\nrequire(\"hypr.bindings\")\n";

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

// Every path under `root` with its content's hash, folders too.
QStringList snapshot(const QString &root)
{
    QStringList entries;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        entries << QDir(root).relativeFilePath(path)
                       + (info.isDir() ? QStringLiteral("/") : QLatin1Char(' ') + QString::fromLatin1(QCryptographicHash::hash(read(path), QCryptographicHash::Sha256).toHex()));
    }
    entries.sort();
    return entries;
}
}

class SetupTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;

    QString config(const QString &relative) const { return m_home.filePath(QStringLiteral(".config/") + relative); }

    int setup(const QStringList &args, const QString &answers = QString(), QString *out = nullptr)
    {
        QString input = answers, output, errors;
        QTextStream in(&input), outStream(&output), errStream(&errors);
        const int code = Setup::runCli(args, in, outStream, errStream);
        outStream.flush();
        errStream.flush();
        if (out)
            *out = output + errors;
        return code;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", config(QString()).toUtf8());
        qputenv("OMASTRATOR_SHELL_DIR", OMASTRATOR_SOURCE_DIR "/shell");
        qputenv("OMARCHY_PATH", m_home.filePath(QStringLiteral("omarchy")).toUtf8());
        // Nothing here may touch the running shell.
        qputenv("OMASTRATOR_OMARCHY_SHELL", "/bin/true");
        if (QStandardPaths::findExecutable(QStringLiteral("jq")).isEmpty())
            QSKIP("Setup edits shell.json with jq, which isn't installed.");
    }

    void init()
    {
        QDir(config(QString())).removeRecursively();
        write(config(QStringLiteral("omarchy/shell.json")), userShellJson);
        write(config(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc")), userMenu);
        write(config(QStringLiteral("hypr/hyprland.lua")), userHypr);
    }

    void menuBlockKeepsTheUsersEntries()
    {
        const QByteArray block = Setup::menuBlock(QStringLiteral("omastrator"));
        bool comma = false;
        const QByteArray added = Setup::withMenuBlock(userMenu, block, &comma);
        QVERIFY(comma);
        QVERIFY(added.startsWith("{\n  // My own entries.\n  \"personal\""));
        QVERIFY(added.contains("\"action\": \"true\"},\n  // BEGIN omastrator setup"));
        QVERIFY(added.endsWith("// END omastrator setup\n}\n"));
        // Once there, it is replaced, never doubled.
        bool again = false;
        QCOMPARE(Setup::withMenuBlock(added, block, &again), added);
        QCOMPARE(Setup::withoutMenuBlock(added, true), userMenu);
        // A trailing comma or comment before the brace needs nothing added.
        const QByteArray trailing = "{\n  \"a\": {\"label\": \"A\"},\n  // last\n}\n";
        QCOMPARE(Setup::withoutMenuBlock(Setup::withMenuBlock(trailing, block, &comma), comma), trailing);
        QVERIFY(!comma);
        QCOMPARE(Setup::withMenuBlock(QByteArray(), block, &comma), "{\n" + block + "}\n");
        // Every entry runs the command it was given, quoted when it must be.
        QVERIFY(Setup::menuBlock(QStringLiteral("/opt/my apps/omastrator")).contains("\"action\": \"'/opt/my apps/omastrator' island new\""));
        // Labels keep their ellipsis as UTF-8.
        QVERIFY(block.contains("\"label\": \"Generate\xE2\x80\xA6\""));
        // Without the comments, the block is plain JSON the menu can read.
        QByteArray json = "{\n" + block + "\"end\": {}\n}\n";
        json.replace(block.left(block.indexOf('\n') + 1), "").replace("  // END omastrator setup\n", "");
        const QJsonObject entries = QJsonDocument::fromJson(json).object();
        QCOMPARE(entries["omastrator.capture.fill"].toObject()["action"].toString(), QStringLiteral("omastrator island capture color fill"));
        QVERIFY(entries.contains("omastrator.mode.draw") && entries.contains("omastrator.connect"));
    }

    void escapesSurviveJq()
    {
        // The shell may have written an em dash as an escape; jq would write it raw.
        const auto edited = Setup::jq(userShellJson, QStringLiteral("."), nullptr);
        QVERIFY(edited);
        QCOMPARE(*edited, userShellJson);
        QCOMPARE(Setup::keepEscapes("\"a\\u2014\"\n", "\"a—\"\n"), QByteArray("\"a\\u2014\"\n"));
        // An escaped backslash is not an escape.
        QCOMPARE(Setup::keepEscapes("\"a\\\\u2014\"\n", "\"b\"\n"), QByteArray("\"b\"\n"));
    }

    void diffsReadLikeDiff()
    {
        const QString diff = Setup::unifiedDiff(QStringLiteral("f"), QByteArray("a\nb\nc\n"), QByteArray("a\nB\nc\nd\n"));
        QCOMPARE(diff, QStringLiteral("--- f\n+++ f\n@@ -1,3 +1,4 @@\n a\n-b\n+B\n c\n+d\n"));
        QVERIFY(Setup::unifiedDiff(QStringLiteral("f"), std::nullopt, QByteArray("x\n")).startsWith(QStringLiteral("--- /dev/null\n+++ f\n@@ -0,0 +1,1 @@")));
        QVERIFY(Setup::unifiedDiff(QStringLiteral("f"), QByteArray("x\n"), std::nullopt).startsWith(QStringLiteral("--- f\n+++ /dev/null\n")));
    }

    void setupAndRemoveAreIdempotent()
    {
        const QStringList before = snapshot(m_home.path());
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Set up.")), qPrintable(out));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.island/Island.qml"))));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.ai/TrayLight.qml"))));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator-ui/Status.qml"))));
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.lua"))).contains("hl.define_submap(\"omastrator-draw\""));
        // Labels are UTF-8 once, not read as Latin-1 and encoded again.
        QVERIFY(QString::fromUtf8(read(config(QStringLiteral("omastrator/hyprland.lua")))).contains(QStringLiteral("\"Open a page\u2026\"")));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/vocabulary.txt"))));
        const QJsonObject shell = QJsonDocument::fromJson(read(config(QStringLiteral("omarchy/shell.json")))).object();
        QCOMPARE(shell["plugins"].toArray().size(), 2);
        const QJsonArray plugins = shell["plugins"].toArray();
        QCOMPARE(plugins[0].toObject()["id"].toString(), QStringLiteral("someone.else"));
        QCOMPARE(plugins[1].toObject()["id"].toString(), QStringLiteral("omastrator.island"));
        QCOMPARE(shell["bar"].toObject()["layout"].toObject()["right"].toArray()[0].toObject()["id"].toString(), QStringLiteral("omastrator.ai"));
        QVERIFY(read(config(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc"))).contains("\"omastrator.connect\""));
        QVERIFY(read(config(QStringLiteral("hypr/hyprland.lua"))).startsWith(userHypr + "\n-- Omastrator's island keys"));

        const QStringList installed = snapshot(m_home.path());
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Everything is already set up.")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), installed);

        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Removed.")), qPrintable(out));
        // Exactly what was there before, byte for byte, folders included.
        QCOMPARE(snapshot(m_home.path()), before);
        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("nothing to remove")));
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void eachChangeIsShownAndAskedFirst()
    {
        const QStringList before = snapshot(m_home.path());
        QString out;
        // No to the bar light, then no to every step.
        QCOMPARE(setup({}, QStringLiteral("n\nn\nn\nn\nn\nn\nn\nn\n"), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);
        QVERIFY(out.contains(QLatin1String("Add the Omastrator AI light to the bar? [y/N]")));
        QVERIFY(out.contains(QLatin1String("+  \"omastrator\": {")));
        QVERIFY(out.contains(QLatin1String("+      \"id\": \"omastrator.island\"")));
        QVERIFY(out.contains(QLatin1String("Make this change? [y/N] Skipped.")));
        // The source line is only printed without --apply.
        QVERIFY(out.contains(QLatin1String("pcall(dofile")));
        QVERIFY(!read(config(QStringLiteral("hypr/hyprland.lua"))).contains("omastrator"));

        // Only a closed input: nothing is assumed.
        QCOMPARE(setup({}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);

        // Yes to the menu alone (the fifth step, after plugins, keys, vocabulary and the binary).
        const bool binaryStep = out.contains(QLatin1String("Tell the plugins where Omastrator is"));
        QCOMPARE(setup({}, binaryStep ? QStringLiteral("n\nn\nn\nn\nn\nn\ny\n") : QStringLiteral("n\nn\nn\nn\nn\ny\n"), &out), 0);
        QVERIFY(read(config(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc"))).contains("BEGIN omastrator setup"));
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.island"))));
        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void dryRunChangesNothing()
    {
        const QStringList before = snapshot(m_home.path());
        QString out;
        QCOMPARE(setup({QStringLiteral("--dry-run"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("Dry run: nothing changed.")));
        QVERIFY(out.contains(QLatin1String("omastrator.ai")));
        QCOMPARE(snapshot(m_home.path()), before);
        QCOMPARE(setup({QStringLiteral("--frob")}, QString(), &out), 1);
    }

    void withoutAShellJsonTheDefaultIsTheBase()
    {
        QFile::remove(config(QStringLiteral("omarchy/shell.json")));
        write(m_home.filePath(QStringLiteral("omarchy/config/omarchy/shell.json")), "{\"version\": 1, \"plugins\": []}\n");
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY(read(config(QStringLiteral("omarchy/shell.json"))).contains("omastrator.island"));
        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        // The user's file stays, without Omastrator in it.
        QVERIFY(!read(config(QStringLiteral("omarchy/shell.json"))).contains("omastrator"));
    }

    // Design mode's keys (docs/ANYWHERE.md) sit where Omarchy's defaults have none, and anywhere.json remaps them.
    void designKeysAreFreeAndRemappable()
    {
        const QByteArray lua = Setup::hyprlandLua(QStringLiteral("omastrator"));
        QVERIFY(lua.contains("hl.bind(\"SUPER + ALT + O\", function()"));
        QVERIFY(lua.contains("hl.dispatch(hl.dsp.submap(\"omastrator-design\"))"));
        QVERIFY(lua.contains("hl.bind(\"SUPER + ALT + W\", hl.dsp.exec_cmd(omastrator .. \" desk toggle\")"));
        QVERIFY(lua.contains("hl.bind(\"Alt_L\", design(\"alt on\")"));
        QVERIFY(lua.contains("omastrator .. \" --daemon\""));
        const QByteArray conf = Setup::hyprlandConf(QStringLiteral("omastrator"));
        QVERIFY(conf.contains("bindd = SUPER ALT, O, Omastrator: design mode, exec, omastrator design on\n"));
        QVERIFY(conf.contains("bindd = SUPER ALT, W, Omastrator: the Desk, exec, omastrator desk toggle\n"));
        QVERIFY(conf.contains("bindr = ALT, Alt_L, exec, omastrator design alt off\n"));
        QVERIFY(conf.contains("exec-once = omastrator --daemon\n"));
        // Neither key is one of Omarchy's own, where its defaults are installed.
        const QString bindings = QStringLiteral("/usr/share/omarchy/default/hypr/bindings");
        for (const QString &name : QDir(bindings).entryList({QStringLiteral("*.lua")}, QDir::Files)) {
            const QByteArray text = read(QDir(bindings).filePath(name));
            QVERIFY2(!text.contains("\"SUPER + ALT + O\"") && !text.contains("\"SUPER + ALT + W\""), qPrintable(name));
        }

        Setup::Environment environment = Setup::Environment::current();
        QCOMPARE(Setup::DesignKeys::from(environment).design, QStringLiteral("SUPER + ALT + O"));
        QDir().mkpath(environment.omastratorConfig());
        write(QDir(environment.omastratorConfig()).filePath(QStringLiteral("anywhere.json")),
              "{\"keys\": {\"design\": \"SUPER + ALT + I\", \"desk\": \"SUPER + ALT + \\\"; rm -rf ~\"}}");
        const Setup::DesignKeys keys = Setup::DesignKeys::from(environment);
        QCOMPARE(keys.design, QStringLiteral("SUPER + ALT + I"));
        // Anything but a key combination is ignored.
        QCOMPARE(keys.desk, QStringLiteral("SUPER + ALT + W"));
        QVERIFY(Setup::hyprlandConf(QStringLiteral("omastrator"), keys).contains("bindd = SUPER ALT, I, Omastrator: design mode"));
        QFile::remove(QDir(environment.omastratorConfig()).filePath(QStringLiteral("anywhere.json")));
    }

    void hyprlandAcceptsTheKeys()
    {
        const QString hyprland = QStandardPaths::findExecutable(QStringLiteral("Hyprland"));
        if (hyprland.isEmpty())
            QSKIP("Hyprland isn't installed, so its config checker can't run.");
        for (const QByteArray &text : {Setup::hyprlandLua(QStringLiteral("/opt/omastrator \"x\"/omastrator")), Setup::hyprlandConf(QStringLiteral("omastrator"))}) {
            const QString path = m_home.filePath(text.startsWith("--") ? QStringLiteral("keys.lua") : QStringLiteral("keys.conf"));
            write(path, text);
            QProcess check;
            check.start(hyprland, {QStringLiteral("--verify-config"), QStringLiteral("-c"), path});
            QVERIFY(check.waitForFinished(30'000));
            const QString output = QString::fromUtf8(check.readAll());
            QVERIFY2(output.contains(QLatin1String("config ok")), qPrintable(output));
        }
    }
};

QTEST_GUILESS_MAIN(SetupTests)
#include "SetupTests.moc"
