#include "Agent/Setup.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
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
    // Outside the HOME that tests snapshot: the state folder (backups) and the fake hyprctl's answer.
    QTemporaryDir m_state;
    QTemporaryDir m_fake;

    QString backupsFolder() const { return m_state.filePath(QStringLiteral("omastrator/setup-backups")); }
    QStringList backupNames() const { return QDir(backupsFolder()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name); }
    // The live binds hyprctl reports: (modmask, key, submap).
    void liveBinds(const QJsonArray &binds) { write(m_fake.filePath(QStringLiteral("binds.json")), QJsonDocument(binds).toJson()); }
    static QJsonObject bind(int modmask, const QString &key, const QString &submap = QString(), const QString &description = QString())
    {
        return {{"modmask", modmask}, {"key", key}, {"submap", submap}, {"description", description}, {"dispatcher", "exec"}, {"arg", "true"}, {"mouse", false}};
    }

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
        QVERIFY(m_home.isValid() && m_state.isValid() && m_fake.isValid());
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_STATE_HOME", m_state.path().toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        write(m_fake.filePath(QStringLiteral("hyprctl")), "#!/bin/sh\ncat \"$(dirname \"$0\")/binds.json\"\n");
        QFile::setPermissions(m_fake.filePath(QStringLiteral("hyprctl")), QFile::permissions(m_fake.filePath(QStringLiteral("hyprctl"))) | QFile::ExeOwner);
        qputenv("OMASTRATOR_HYPRCTL", m_fake.filePath(QStringLiteral("hyprctl")).toUtf8());
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
        QDir(m_state.path()).removeRecursively();
        QDir().mkpath(m_state.path());
        liveBinds({});
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

    void extensionFlagsKeepTheUsersOwn()
    {
        const QString ours = QStringLiteral("/opt/oma/ext");
        const QByteArray omarchy = "--ozone-platform=wayland\n--load-extension=/usr/share/a,/usr/share/b\n--password-store=basic\n";
        const QByteArray added = Setup::withExtension(omarchy, ours);
        QCOMPARE(added, QByteArray("--ozone-platform=wayland\n--load-extension=/usr/share/a,/usr/share/b,/opt/oma/ext\n--password-store=basic\n"));
        QCOMPARE(Setup::withExtension(added, ours), added);
        QCOMPARE(Setup::withoutExtension(added, ours), omarchy);
        // No list yet: a line of its own, taken out whole.
        const QByteArray plain = "--ozone-platform=wayland\n";
        QCOMPARE(Setup::withExtension(plain, ours), QByteArray("--ozone-platform=wayland\n--load-extension=/opt/oma/ext\n"));
        QCOMPARE(Setup::withoutExtension(Setup::withExtension(plain, ours), ours), plain);
        QCOMPARE(Setup::withExtension({}, ours), QByteArray("--load-extension=/opt/oma/ext\n"));
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
        // Design mode enters its submap only once Hyprland loads the keys that define it.
        QVERIFY(!Setup::designKeysLoaded(Setup::Environment::current()));
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Set up.")), qPrintable(out));
        QVERIFY(Setup::designKeysLoaded(Setup::Environment::current()));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.island/Island.qml"))));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.ai/TrayLight.qml"))));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator-ui/Status.qml"))));
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.lua"))).contains("hl.define_submap(\"omastrator-draw\""));
        // The escape hatch is bound outside every submap.
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.lua"))).contains("hl.bind(\"SUPER + ALT + Escape\", hl.dsp.exec_cmd(omastrator .. \" reset\")"));
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
        // Live in your own Chromium: the host its extension starts, and the extension loaded next to Omarchy's.
        const QJsonObject host = QJsonDocument::fromJson(read(config(QStringLiteral("chromium/NativeMessagingHosts/io.github.iretonsean.omastrator.json")))).object();
        QCOMPARE(host["allowed_origins"].toArray().first().toString(), QStringLiteral("chrome-extension://gmanolpmdkmgccoeiogpdhjifdkdjfap/"));
        QVERIFY(read(config(QStringLiteral("chromium-flags.conf"))).contains("--load-extension=" OMASTRATOR_SOURCE_DIR "/extras/chromium-extension\n"));

        const QStringList installed = snapshot(m_home.path());
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Everything is already set up.")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), installed);

        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Removed.")), qPrintable(out));
        QVERIFY(!Setup::designKeysLoaded(Setup::Environment::current()));
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

    // GNUInstallDirs puts bindir and datadir at "bin" and "share" under any prefix, /usr or
    // ~/.local alike, so a package's shell plugins are always found the same way relative to
    // the binary (docs/RELEASING.md, AGENTS.md's "Also on this branch: the /tmp leak" sibling).
    void installedShellSourceIsBinaryRelative()
    {
        QCOMPARE(Setup::installedShellSource(QStringLiteral("/usr/bin/omastrator")), QStringLiteral("/usr/share/omastrator/shell"));
        QCOMPARE(Setup::installedShellSource(m_home.filePath(QStringLiteral(".local/bin/omastrator"))),
                 m_home.filePath(QStringLiteral(".local/share/omastrator/shell")));
    }

    void locateShellFindsAPackageInstall()
    {
        QTemporaryDir prefix;
        QVERIFY(prefix.isValid());
        const QString binary = prefix.filePath(QStringLiteral("bin/omastrator"));
        write(binary, QByteArray());
        write(prefix.filePath(QStringLiteral("share/omastrator/shell/omastrator.island/manifest.json")), "{}");
        write(prefix.filePath(QStringLiteral("share/omastrator/extras/chromium-extension/manifest.json")), "{}");

        const QByteArray previous = qgetenv("OMASTRATOR_SHELL_DIR");
        qunsetenv("OMASTRATOR_SHELL_DIR");
        const Setup::ShellLocation location = Setup::locateShell(binary);
        qputenv("OMASTRATOR_SHELL_DIR", previous);

        QCOMPARE(location.source, prefix.filePath(QStringLiteral("share/omastrator/shell")));
        QCOMPARE(location.extension, prefix.filePath(QStringLiteral("share/omastrator/extras/chromium-extension")));
    }

    void locateShellPrefersTheOverride()
    {
        QTemporaryDir prefix, override;
        QVERIFY(prefix.isValid() && override.isValid());
        const QString binary = prefix.filePath(QStringLiteral("bin/omastrator"));
        write(binary, QByteArray());
        write(prefix.filePath(QStringLiteral("share/omastrator/shell/x")), QByteArray());

        const QByteArray previous = qgetenv("OMASTRATOR_SHELL_DIR");
        qputenv("OMASTRATOR_SHELL_DIR", override.path().toUtf8());
        const Setup::ShellLocation location = Setup::locateShell(binary);
        qputenv("OMASTRATOR_SHELL_DIR", previous);

        QCOMPARE(location.source, override.path());
    }

    void everyChangeIsBackedUpFirst()
    {
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QCOMPARE(backupNames().size(), 1);
        const QString name = backupNames().first();
        const QString folder = QDir(backupsFolder()).filePath(name);
        QVERIFY2(out.contains(folder), qPrintable(out));
        QVERIFY2(out.contains(QLatin1String("omastrator setup --restore ") + name), qPrintable(out));
        QVERIFY(QRegularExpression(QStringLiteral("^\\d{8}-\\d{6}$")).match(name).hasMatch());
        // The copies are the files as they were, and the manifest names their original paths.
        const QJsonObject manifest = QJsonDocument::fromJson(read(QDir(folder).filePath(QStringLiteral("manifest.json")))).object();
        QMap<QString, bool> existed;
        for (const QJsonValue &entry : manifest["entries"].toArray())
            existed[entry.toObject()["path"].toString()] = entry.toObject()["existed"].toBool();
        QVERIFY(existed.value(config(QStringLiteral("hypr/hyprland.lua"))));
        QVERIFY(existed.value(config(QStringLiteral("omarchy/shell.json"))));
        QVERIFY(existed.value(config(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc"))));
        QVERIFY(existed.contains(config(QStringLiteral("omastrator/hyprland.lua"))));
        QVERIFY(!existed.value(config(QStringLiteral("omastrator/hyprland.lua"))));
        QCOMPARE(read(QDir(folder).filePath(QStringLiteral("files") + config(QStringLiteral("hypr/hyprland.lua")))), userHypr);
        QCOMPARE(read(QDir(folder).filePath(QStringLiteral("files") + config(QStringLiteral("omarchy/shell.json")))), userShellJson);
        // Files setup didn't change aren't copied.
        QVERIFY(!existed.contains(config(QStringLiteral("omarchy/theme"))));

        // The preview names the folder before anything is asked.
        QVERIFY(setup({QStringLiteral("--remove"), QStringLiteral("--dry-run")}, QString(), &out) == 0);
        QVERIFY2(out.contains(QLatin1String("setup would copy every file it changes to ") + backupsFolder()), qPrintable(out));
    }

    void noBackupNoChange()
    {
        const QStringList before = snapshot(m_home.path());
        // A state folder that can't be made: a file where its parent should be.
        write(m_home.filePath(QStringLiteral("blocker")), "x");
        qputenv("XDG_STATE_HOME", m_home.filePath(QStringLiteral("blocker")).toUtf8());
        QString out;
        const QStringList withBlocker = snapshot(m_home.path());
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("Nothing was changed")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), withBlocker);
        QFile::remove(m_home.filePath(QStringLiteral("blocker")));
        qputenv("XDG_STATE_HOME", m_state.path().toUtf8());
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void onlyTheNewestFiveBackupsStay()
    {
        const Setup::Environment environment = Setup::Environment::current();
        const QString file = config(QStringLiteral("hypr/hyprland.lua"));
        for (int day = 1; day <= 7; ++day)
            QVERIFY(Setup::writeBackup(environment, QStringLiteral("2026010%1-000000").arg(day), QStringLiteral("setup"), {file}, nullptr).isEmpty());
        QDir().mkpath(QDir(backupsFolder()).filePath(QStringLiteral("mine")));
        Setup::pruneBackups(environment, QStringLiteral("20260101-000000"));
        // The newest five and the one just made; a folder that isn't a backup is left alone.
        QCOMPARE(backupNames(), (QStringList{QStringLiteral("20260101-000000"), QStringLiteral("20260103-000000"), QStringLiteral("20260104-000000"),
                                             QStringLiteral("20260105-000000"), QStringLiteral("20260106-000000"), QStringLiteral("20260107-000000"),
                                             QStringLiteral("mine")}));
        Setup::pruneBackups(environment, QStringLiteral("20260107-000000"));
        QCOMPARE(backupNames().size(), 6);
        QVERIFY(!backupNames().contains(QStringLiteral("20260101-000000")));
    }

    void aSetupKeepsFiveBackupsAtMost()
    {
        QString out;
        for (int round = 0; round < 7; ++round) {
            QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
            QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        }
        QCOMPARE(backupNames().size(), 5);
        // The last thing made is kept.
        QVERIFY2(out.contains(backupNames().last()), qPrintable(out));
    }

    void restorePutsEveryFileBack()
    {
        const QStringList before = snapshot(m_home.path());
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        write(config(QStringLiteral("unrelated.txt")), "mine");
        const QString name = backupNames().first();

        // It shows what will change, and asks.
        QCOMPARE(setup({QStringLiteral("--restore")}, QStringLiteral("n\n"), &out), 0);
        QVERIFY2(out.contains(name) && out.contains(QLatin1String("Put these files back?")) && out.contains(QLatin1String("Skipped.")), qPrintable(out));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--dry-run")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("Dry run: nothing changed.")));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));

        // Named, and with --yes: what setup wrote goes, what it edited comes back, what it never touched stays.
        QCOMPARE(setup({QStringLiteral("--restore"), name, QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Restored")), qPrintable(out));
        QCOMPARE(read(config(QStringLiteral("unrelated.txt"))), QByteArray("mine"));
        QFile::remove(config(QStringLiteral("unrelated.txt")));
        QCOMPARE(snapshot(m_home.path()), before);

        // Nothing left to do the second time.
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("nothing to restore")));
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void restoreWorksAfterAnInterruptedSetup()
    {
        const QStringList before = snapshot(m_home.path());
        const Setup::Environment environment = Setup::Environment::current();
        const QStringList paths = {config(QStringLiteral("hypr/hyprland.lua")), config(QStringLiteral("omastrator/hyprland.lua")),
                                   config(QStringLiteral("omarchy/shell.json"))};
        QVERIFY(Setup::writeBackup(environment, QStringLiteral("20260928-101500"), QStringLiteral("setup"), paths, nullptr).isEmpty());
        // Setup stopped after two writes, before it could record anything.
        write(paths[0], userHypr + "\n-- half written");
        write(paths[1], "partial");
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omastrator/setup.json"))));
        QString out;
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("20260928-1015"), QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void restoreAndListSayWhenThereIsNothing()
    {
        QString out;
        QCOMPARE(setup({QStringLiteral("--list-backups")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("No setup backups yet")), qPrintable(out));
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--yes")}, QString(), &out), 1);
        QVERIFY(out.contains(QLatin1String("no setup backup")));
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QCOMPARE(setup({QStringLiteral("--list-backups")}, QString(), &out), 0);
        QVERIFY2(out.contains(backupNames().first()) && out.contains(QLatin1String("--restore")), qPrintable(out));
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("2001"), QStringLiteral("--yes")}, QString(), &out), 1);
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--remove")}, QString(), &out), 1);
    }

    void keysTheUserHasAreSkipped()
    {
        const QStringList before = snapshot(m_home.path());
        // Live: Super+Alt+C is theirs; Super+Alt+L is bound inside their own submap, where it can't clash.
        liveBinds({bind(72, QStringLiteral("C")), bind(72, QStringLiteral("L"), QStringLiteral("resize")), bind(64, QStringLiteral("Q"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Super+Alt+C is already yours: skipped")), qPrintable(out));
        QVERIFY(!out.contains(QLatin1String("Super+Alt+L is already yours")));
        const QByteArray keys = read(config(QStringLiteral("omastrator/hyprland.lua")));
        QVERIFY(!keys.contains("hl.bind(\"SUPER + ALT + C\""));
        QVERIFY(keys.contains("hl.bind(\"SUPER + ALT + L\""));
        QVERIFY(keys.contains("hl.bind(\"SUPER + ALT + D\""));
        QVERIFY(keys.contains("hl.bind(\"SUPER + ALT + Escape\""));
        // Recorded, so setup stays exact.
        const QJsonObject record = QJsonDocument::fromJson(read(config(QStringLiteral("omastrator/setup.json")))).object();
        QCOMPARE(record["skippedKeys"].toArray().first().toString(), QStringLiteral("Super+Alt+C"));

        // A second run is quiet about the key and changes nothing.
        const QStringList installed = snapshot(m_home.path());
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Everything is already set up.")) && out.contains(QLatin1String("Super+Alt+C is already yours")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), installed);

        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void restoreAfterSkippedKeysIsExact()
    {
        const QStringList before = snapshot(m_home.path());
        liveBinds({bind(72, QStringLiteral("D"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("Super+Alt+D is already yours: skipped")));
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void keysInTheConfigAreSkippedToo()
    {
        // No live Hyprland to ask: the user's own config says what they use.
        write(config(QStringLiteral("hypr/hyprland.lua")),
              userHypr
                  + "hl.bind(\"SUPER + ALT + V\", hl.dsp.exec_cmd(\"mine\"))\n"
                    "-- hl.bind(\"SUPER + ALT + A\", hl.dsp.exec_cmd(\"commented out\"))\n"
                    "hl.define_submap(\"resize\", function()\n"
                    "  hl.bind(\"SUPER + ALT + L\", hl.dsp.exec_cmd(\"in a submap\"))\n"
                    "end)\n");
        write(config(QStringLiteral("hypr/bindings.conf")), "$mod = SUPER\nbindd = $mod ALT, C, Mine, exec, mine\nbindm = SUPER ALT, D, movewindow\n");
        QSet<QString> taken = Setup::takenKeys(Setup::Environment::current());
        QVERIFY(taken.contains(QStringLiteral("SUPER+ALT+V")));
        QVERIFY(taken.contains(QStringLiteral("SUPER+ALT+C")));
        QVERIFY(!taken.contains(QStringLiteral("SUPER+ALT+A")));
        QVERIFY(!taken.contains(QStringLiteral("SUPER+ALT+L")));
        QVERIFY(!taken.contains(QStringLiteral("SUPER+ALT+D")));
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Super+Alt+V is already yours: skipped")) && out.contains(QLatin1String("Super+Alt+C is already yours: skipped")), qPrintable(out));
        const QByteArray keys = read(config(QStringLiteral("omastrator/hyprland.lua")));
        QVERIFY(!keys.contains("hl.bind(\"SUPER + ALT + V\""));
        QVERIFY(keys.contains("hl.bind(\"SUPER + ALT + A\""));
    }

    void combosCompareTheWayHyprlandDoes()
    {
        QCOMPARE(Setup::normalizeCombo(QStringLiteral("SUPER + ALT + O")), QStringLiteral("SUPER+ALT+O"));
        QCOMPARE(Setup::normalizeCombo(QStringLiteral("ALT SUPER + o")), QStringLiteral("SUPER+ALT+O"));
        QCOMPARE(Setup::normalizeCombo(QStringLiteral("Super+Alt+Esc")), QStringLiteral("SUPER+ALT+ESCAPE"));
        QCOMPARE(Setup::displayCombo(QStringLiteral("SUPER+ALT+C")), QStringLiteral("Super+Alt+C"));
        QVERIFY(Setup::omastratorKeys({}).contains(QStringLiteral("SUPER+ALT+ESCAPE")));
    }

    void noKeysInstallsNoKeys()
    {
        const QByteArray hypr = read(config(QStringLiteral("hypr/hyprland.lua")));
        QString out;
        QCOMPARE(setup({QStringLiteral("--no-keys"), QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("omastrator design on")), qPrintable(out));
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QCOMPARE(read(config(QStringLiteral("hypr/hyprland.lua"))), hypr);
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.island/Island.qml"))));
        QVERIFY(read(config(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc"))).contains("BEGIN omastrator setup"));
        QCOMPARE(setup({QStringLiteral("--no-keys"), QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("Everything is already set up.")));
        QCOMPARE(setup({QStringLiteral("--remove"), QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY(!read(config(QStringLiteral("omarchy/shell.json"))).contains("omastrator"));
    }

    void helpNamesTheSafetyNet()
    {
        QString out;
        QCOMPARE(setup({QStringLiteral("--help")}, QString(), &out), 0);
        for (const char *text : {"setup-backups", "--restore", "--no-keys", "--list-backups", "never takes a"})
            QVERIFY2(out.contains(QLatin1String(text)), text);
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
