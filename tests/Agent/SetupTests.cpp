#include "Agent/Setup.h"
#include "SetupKeyFixtures.h"
#include <QCoreApplication>
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

// What Hyprland reports for the binds of a key file it loaded (the way `hyprctl binds -j` does), so a test can feed a generated file's
// own binds back after a reload. Lua reports every bind as "__lua" with a number, so only a description says whose it is; hyprlang
// reports the dispatcher and argument as written.
QJsonArray keyFileBinds(const QByteArray &text, bool lua, bool releaseWithoutDescription = false)
{
    static const QMap<QString, int> mods = {{QStringLiteral("SUPER"), 64}, {QStringLiteral("CTRL"), 4}, {QStringLiteral("ALT"), 8}, {QStringLiteral("SHIFT"), 1}};
    QJsonArray binds;
    int number = 80;
    QString submap;
    auto add = [&](const QStringList &modWords, const QString &key, const QString &inSubmap, const QString &description, const QString &dispatcher, const QString &arg,
                   bool release) {
        int modmask = 0;
        for (const QString &word : modWords)
            modmask |= mods.value(word.trimmed().toUpper());
        binds.append(QJsonObject{{"modmask", modmask}, {"key", key}, {"submap", inSubmap}, {"description", release && releaseWithoutDescription ? QString() : description},
                                 {"dispatcher", dispatcher}, {"arg", arg}, {"release", release}, {"mouse", false}});
    };
    if (lua) {
        QString statement;
        static const QRegularExpression opens(QStringLiteral("^(?:hl\\.define_submap|submap)\\(\"([^\"]+)\""));
        static const QRegularExpression starts(QStringLiteral("^\\s*(?:hl\\.)?bind\\(\""));
        for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
            if (statement.isEmpty()) {
                if (const auto open = opens.match(line); open.hasMatch())
                    submap = open.captured(1);
                else if (line == QLatin1String("end)"))
                    submap.clear();
                if (!starts.match(line).hasMatch())
                    continue;
            }
            statement += line + QLatin1Char('\n');
            if (!line.trimmed().endsWith(QLatin1String("})")))
                continue;
            const qsizetype first = statement.indexOf(QLatin1Char('"')) + 1;
            const QStringList combo = statement.mid(first, statement.indexOf(QLatin1Char('"'), first) - first).split(QStringLiteral(" + "));
            const auto description = QRegularExpression(QStringLiteral("description = \"([^\"]*)\"")).match(statement);
            const bool universal = statement.contains(QLatin1String("submap_universal = true"));
            add(combo.mid(0, combo.size() - 1), combo.last(), universal ? QString() : submap, description.captured(1), QStringLiteral("__lua"), QString::number(number++),
                statement.contains(QLatin1String("release = true")));
            statement.clear();
        }
        return binds;
    }
    static const QRegularExpression submapLine(QStringLiteral("^submap\\s*=\\s*(\\S+)"));
    static const QRegularExpression bindLine(QStringLiteral("^bind([a-z]*)\\s*=\\s*(.*)$"));
    for (const QString &line : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        if (const auto open = submapLine.match(line); open.hasMatch()) {
            submap = open.captured(1) == QLatin1String("reset") ? QString() : open.captured(1);
            continue;
        }
        const auto match = bindLine.match(line);
        if (!match.hasMatch())
            continue;
        const QString flags = match.captured(1);
        const bool described = flags.contains(QLatin1Char('d'));
        const QStringList parts = match.captured(2).split(QLatin1Char(','));
        const int fixed = described ? 5 : 4;
        if (parts.size() < fixed)
            continue;
        const QString arg = parts.mid(fixed - 1).join(QLatin1Char(',')).trimmed();
        add(parts[0].split(QLatin1Char(' '), Qt::SkipEmptyParts), parts[1].trimmed(), flags.contains(QLatin1Char('u')) ? QString() : submap,
            described ? parts[2].trimmed() : QString(), parts[fixed - 2].trimmed(), arg, flags.contains(QLatin1Char('r')));
    }
    return binds;
}

QJsonArray readArray(const QString &path)
{
    return QJsonDocument::fromJson(read(path)).array();
}

// `hyprctl` for the tests, the test binary itself in a mode of its own: `-j binds` answers binds.json, and `reload` recomputes it the
// way a Hyprland reload does: the user's config binds (user.json), plus Omastrator's key file when the config sources it. Runtime-only binds are gone.
// Knobs, all files in the fake's folder: vanish.json (combos of user binds our loaded file kills), stuck.json (user binds a pending edit
// kills from the second reload on), drop-own.json (combos of ours Hyprland doesn't bind), reload-fails, dead-after-reload (no answer once reloaded).
int fakeHyprctl(const QString &dir, const QStringList &args)
{
    const auto file = [&](const char *name) { return QDir(dir).filePath(QLatin1String(name)); };
    const auto counter = [&] { return read(file("reloads")).toInt(); };
    if (!args.isEmpty() && args.first() == QLatin1String("reload")) {
        write(file("reloads"), QByteArray::number(counter() + 1));
        QFile calls(file("calls.log"));
        if (calls.open(QIODevice::Append))
            calls.write("reload\n");
        if (QFileInfo::exists(file("reload-fails"))) {
            fputs("reload failed: the fake says no\n", stderr);
            return 1;
        }
        const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
        const bool lua = QFileInfo::exists(QDir(config).filePath(QStringLiteral("hypr/hyprland.lua")));
        const Setup::HyprFormat format = lua ? Setup::HyprFormat::lua : Setup::HyprFormat::conf;
        const QString name = lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf");
        const QByteArray keys = read(QDir(config).filePath(QStringLiteral("omastrator/") + name));
        const bool sourced = !keys.isEmpty() && Setup::hasSourceLine(read(QDir(config).filePath(QStringLiteral("hypr/") + name)), format);
        QJsonArray binds = readArray(file("user.json"));
        auto matches = [](const QJsonObject &bind, const QJsonValue &wanted) {
            return bind["modmask"].toInt() == wanted["modmask"].toInt() && bind["key"].toString().toLower() == wanted["key"].toString().toLower();
        };
        auto without = [&](const QJsonArray &from, const QJsonArray &drop) {
            QJsonArray kept;
            for (const QJsonValue &value : from) {
                bool dropped = false;
                for (const QJsonValue &wanted : drop)
                    dropped = dropped || (matches(value.toObject(), wanted) && value["submap"].toString().isEmpty());
                if (!dropped)
                    kept.append(value);
            }
            return kept;
        };
        if (sourced) {
            for (const QJsonValue &bind : keyFileBinds(keys, lua))
                binds.append(bind);
            binds = without(binds, readArray(file("vanish.json")));
        }
        if (counter() >= 2)
            binds = without(binds, readArray(file("stuck.json")));
        if (sourced) {
            QJsonArray kept;
            for (const QJsonValue &bind : binds) {
                bool dropped = false;
                for (const QJsonValue &wanted : readArray(file("drop-own.json")))
                    dropped = dropped || (matches(bind.toObject(), wanted) && bind["description"].toString().startsWith(QLatin1String("Omastrator")));
                if (!dropped)
                    kept.append(bind);
            }
            binds = kept;
        }
        write(file("binds.json"), QJsonDocument(binds).toJson());
        return 0;
    }
    if (QFileInfo::exists(file("dead-after-reload")) && counter() >= 2)
        return 1;
    fwrite(read(file("binds.json")).constData(), 1, read(file("binds.json")).size(), stdout);
    return 0;
}
}

class SetupTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;
    // Outside the HOME that tests snapshot: the state folder (backups) and the fake hyprctl's answer.
    QTemporaryDir m_state;
    QTemporaryDir m_fake;
    // Every XDG folder the child processes could inherit from the builder's shell.
    QTemporaryDir m_data;
    QTemporaryDir m_cache;

    QString backupsFolder() const { return m_state.filePath(QStringLiteral("omastrator/setup-backups")); }
    QStringList backupNames() const { return QDir(backupsFolder()).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name); }
    // The live binds hyprctl reports: (modmask, key, submap).
    // The user's own binds: what Hyprland has now and what a reload gives back (their config).
    void liveBinds(const QJsonArray &binds)
    {
        write(m_fake.filePath(QStringLiteral("binds.json")), QJsonDocument(binds).toJson());
        write(m_fake.filePath(QStringLiteral("user.json")), QJsonDocument(binds).toJson());
    }
    // Binds that exist only because something ran `hyprctl keyword bind`: the first reload drops them.
    void runtimeBind(const QJsonObject &extra)
    {
        QJsonArray binds;
        for (const QJsonValue &value : QJsonDocument::fromJson(read(m_fake.filePath(QStringLiteral("binds.json")))).array())
            binds.append(value);
        binds.append(extra);
        write(m_fake.filePath(QStringLiteral("binds.json")), QJsonDocument(binds).toJson());
    }
    int reloads() const { return read(m_fake.filePath(QStringLiteral("reloads"))).toInt(); }
    void fakeKnob(const char *name, const QJsonArray &value = {}) { write(m_fake.filePath(QLatin1String(name)), QJsonDocument(value).toJson()); }
    // Hyprland has loaded whatever is on disk (an upgrade starts from a desktop that is already running our old file).
    void hyprlandLoadsTheConfig()
    {
        QCOMPARE(fakeHyprctl(m_fake.path(), {QStringLiteral("reload")}), 0);
        QFile::remove(m_fake.filePath(QStringLiteral("reloads")));
        QFile::remove(m_fake.filePath(QStringLiteral("calls.log")));
    }
    QJsonArray liveNow() { return readArray(m_fake.filePath(QStringLiteral("binds.json"))); }
    static bool hasBind(const QJsonArray &binds, int modmask, const QString &key, const QString &submap = QString())
    {
        for (const QJsonValue &value : binds) {
            const QJsonObject bind = value.toObject();
            if (bind["modmask"].toInt() == modmask && bind["key"].toString().toLower() == key.toLower() && bind["submap"].toString() == submap)
                return true;
        }
        return false;
    }
    static QJsonObject bind(int modmask, const QString &key, const QString &submap = QString(), const QString &description = QString())
    {
        return {{"modmask", modmask}, {"key", key}, {"submap", submap}, {"description", description}, {"dispatcher", "exec"}, {"arg", "true"}, {"mouse", false}};
    }

    static QJsonArray luaBinds(const QByteArray &lua, bool releaseWithoutDescription = false) { return keyFileBinds(lua, true, releaseWithoutDescription); }

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
        QVERIFY(m_home.isValid() && m_state.isValid() && m_fake.isValid() && m_data.isValid() && m_cache.isValid());
        // First, before anything can run: a child (jq, Hyprland --verify-config) reads only these.
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", config(QString()).toUtf8());
        qputenv("XDG_STATE_HOME", m_state.path().toUtf8());
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
        qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        write(m_fake.filePath(QStringLiteral("hyprctl")),
              "#!/bin/sh\nexec \"" + QCoreApplication::applicationFilePath().toUtf8() + "\" --fake-hyprctl \"" + m_fake.path().toUtf8() + "\" \"$@\"\n");
        QFile::setPermissions(m_fake.filePath(QStringLiteral("hyprctl")), QFile::permissions(m_fake.filePath(QStringLiteral("hyprctl"))) | QFile::ExeOwner);
        qputenv("OMASTRATOR_HYPRCTL", m_fake.filePath(QStringLiteral("hyprctl")).toUtf8());
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
        QDir(m_home.filePath(QStringLiteral("omarchy"))).removeRecursively();
        QDir(m_state.path()).removeRecursively();
        QDir().mkpath(m_state.path());
        liveBinds({});
        for (const char *name : {"calls.log", "reloads", "vanish.json", "stuck.json", "drop-own.json", "reload-fails", "dead-after-reload"})
            QFile::remove(m_fake.filePath(QLatin1String(name)));
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
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.lua"))).contains("\nsubmap(\"omastrator-draw\""));
        // The escape hatch is bound outside every submap and works inside them.
        const QByteArray keyFile = read(config(QStringLiteral("omastrator/hyprland.lua")));
        QVERIFY(keyFile.contains("bind(\"SUPER + ALT + Escape\", function()"));
        QVERIFY(keyFile.contains("submap_universal = true"));
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
        QVERIFY(lua.contains("bind(\"SUPER + ALT + O\", function()"));
        QVERIFY(lua.contains("run(hl.dsp.submap(\"omastrator-design\"))"));
        QVERIFY(lua.contains("bind(\"SUPER + ALT + W\", hl.dsp.exec_cmd(omastrator .. \" desk toggle\")"));
        QVERIFY(lua.contains("bind(\"Alt_L\", design(\"alt on\")"));
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
        Setup::pruneBackups(environment, {QStringLiteral("20260101-000000")});
        // The newest five and the one just made; a folder that isn't a backup is left alone.
        QCOMPARE(backupNames(), (QStringList{QStringLiteral("20260101-000000"), QStringLiteral("20260103-000000"), QStringLiteral("20260104-000000"),
                                             QStringLiteral("20260105-000000"), QStringLiteral("20260106-000000"), QStringLiteral("20260107-000000"),
                                             QStringLiteral("mine")}));
        Setup::pruneBackups(environment, {QStringLiteral("20260107-000000")});
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
        QCOMPARE(setup({QStringLiteral("--restore"), name, QStringLiteral("--yes")}, QString(), &out), 0);
        QVERIFY(out.contains(QLatin1String("nothing to restore")));
        QCOMPARE(snapshot(m_home.path()), before);
    }

    void restoreBacksUpWhatItReplaces()
    {
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        const QString setupBackup = backupNames().first();
        // A fix made by hand after setup: exactly what a restore used to lose.
        const QString keysFile = config(QStringLiteral("omastrator/hyprland.lua"));
        write(keysFile, read(keysFile) + "-- my own fix\n");
        write(config(QStringLiteral("hypr/hyprland.lua")), userHypr + "-- and this\n");
        const QStringList fixed = snapshot(m_home.path());

        QCOMPARE(setup({QStringLiteral("--restore"), setupBackup, QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(backupNames().size(), 2);
        const QString restoreBackup = backupNames().last();
        QVERIFY2(out.contains(QLatin1String("Backed up the current files to ") + QDir(backupsFolder()).filePath(restoreBackup)), qPrintable(out));
        const QJsonObject manifest = QJsonDocument::fromJson(read(QDir(backupsFolder()).filePath(restoreBackup + QStringLiteral("/manifest.json")))).object();
        QCOMPARE(manifest["action"].toString(), QStringLiteral("restore"));
        QCOMPARE(read(QDir(backupsFolder()).filePath(restoreBackup + QStringLiteral("/files") + config(QStringLiteral("hypr/hyprland.lua")))), userHypr + "-- and this\n");
        QVERIFY(!QFileInfo::exists(keysFile));

        // The newest backup is now the restore, so restoring again undoes it.
        QCOMPARE(setup({QStringLiteral("--restore"), QStringLiteral("--yes")}, QString(), &out), 0);
        QCOMPARE(snapshot(m_home.path()), fixed);
        QCOMPARE(read(config(QStringLiteral("hypr/hyprland.lua"))), userHypr + "-- and this\n");
    }

    void restoreChangesNothingWithoutItsBackup()
    {
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        const QString folder = QDir(backupsFolder()).filePath(backupNames().first());
        const QStringList installed = snapshot(m_home.path());
        // The named backup is read by path; the state folder its new copy would go to can't be made.
        write(m_home.filePath(QStringLiteral("blocker")), "x");
        qputenv("XDG_STATE_HOME", m_home.filePath(QStringLiteral("blocker")).toUtf8());
        const QStringList withBlocker = snapshot(m_home.path());
        const int code = setup({QStringLiteral("--restore"), folder, QStringLiteral("--yes")}, QString(), &out);
        QFile::remove(m_home.filePath(QStringLiteral("blocker")));
        qputenv("XDG_STATE_HOME", m_state.path().toUtf8());
        QCOMPARE(code, 1);
        QVERIFY2(out.contains(QLatin1String("Nothing was changed")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), installed);
        QCOMPARE(withBlocker.size(), installed.size() + 1);
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
        QVERIFY(!keys.contains("bind(\"SUPER + ALT + C\""));
        QVERIFY(keys.contains("bind(\"SUPER + ALT + L\""));
        QVERIFY(keys.contains("bind(\"SUPER + ALT + D\""));
        QVERIFY(keys.contains("bind(\"SUPER + ALT + Escape\""));
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
        QVERIFY(!keys.contains("bind(\"SUPER + ALT + V\""));
        QVERIFY(keys.contains("bind(\"SUPER + ALT + A\""));
    }

    void aRerunKeepsOurOwnKeysOnLuaHyprland()
    {
        // A release bind alone, with nothing of ours beside it, is someone else's.
        liveBinds({QJsonObject{{"modmask", 72}, {"key", "V"}, {"submap", ""}, {"description", ""}, {"dispatcher", "__lua"}, {"arg", "81"}, {"release", true}, {"mouse", false}}});
        QVERIFY(Setup::takenKeys(Setup::Environment::current()).contains(QStringLiteral("SUPER+ALT+V")));
        liveBinds({});

        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        const QByteArray keys = read(config(QStringLiteral("omastrator/hyprland.lua")));
        QVERIFY(keys.contains("dictate (release)"));
        const QStringList installed = snapshot(m_home.path());

        // Hyprland now reports those keys back, in Lua's own style; setup must not mistake them for the user's.
        for (const bool oldKeyFile : {false, true}) {
            liveBinds(luaBinds(keys, oldKeyFile));
            const QSet<QString> taken = Setup::takenKeys(Setup::Environment::current());
            for (const QString &combo : Setup::omastratorKeys(Setup::DesignKeys::from(Setup::Environment::current())))
                QVERIFY2(!taken.contains(combo), qPrintable(combo + (oldKeyFile ? QStringLiteral(" (release without a description)") : QString())));
            QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
            QVERIFY2(out.contains(QLatin1String("Everything is already set up.")) && !out.contains(QLatin1String("already yours")), qPrintable(out));
            QCOMPARE(snapshot(m_home.path()), installed);
        }
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.lua"))).contains("SUPER + ALT + V\", island(\"dictate start\")"));
    }

    void omarchyLuaBindsAreReadFromTheConfig()
    {
        write(config(QStringLiteral("hypr/bindings.lua")),
              "o.bind(\"SUPER + ALT + C\", \"Mine\", \"true\")\n"
              "  o.bind_toggle('SUPER + ALT + A', \"Toggle\", \"true\")\n"
              "-- o.bind(\"SUPER + ALT + D\", \"Commented out\", \"true\")\n"
              "hl.unbind(\"SUPER + ALT + L\")\n"
              "o.unbind(\"SUPER + ALT + Escape\")\n"
              "o.bind(\"SUPER + ALT + \" .. key, \"Computed\", \"true\")\n"
              "function o.bind(keys, description, dispatcher, options)\n");
        write(m_home.filePath(QStringLiteral("omarchy/default/hypr/bindings/utilities.lua")),
              "o.bind(\"SUPER + CTRL + V\", \"Clipboard manager\", \"omarchy-shell shell toggle omarchy.clipboard\")\n"
              "o.bind(\"SUPER + ALT + V\", \"An Omarchy default\", hl.dsp.window.close(), { locked = true })\n"
              "hl.bind(\"SUPER + ALT + \" .. key, hl.dsp.focus({ workspace = 1 }))\n");
        const QSet<QString> taken = Setup::takenKeys(Setup::Environment::current());
        for (const char *combo : {"SUPER+ALT+C", "SUPER+ALT+A", "SUPER+ALT+V", "SUPER+CTRL+V"})
            QVERIFY2(taken.contains(QLatin1String(combo)), combo);
        for (const char *combo : {"SUPER+ALT+D", "SUPER+ALT+L", "SUPER+ALT+ESCAPE"})
            QVERIFY2(!taken.contains(QLatin1String(combo)), combo);
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes")}, QString(), &out), 0);
        for (const char *key : {"Super+Alt+C", "Super+Alt+A", "Super+Alt+V"})
            QVERIFY2(out.contains(QLatin1String(key) + QLatin1String(" is already yours: skipped")), qPrintable(out));
    }

    void hyprlangVariablesAreSharedAcrossFiles()
    {
        // bindings.conf is read before hyprland.conf, the file that defines what it uses.
        write(config(QStringLiteral("hypr/hyprland.conf")), "$mainMod = SUPER\n$modAlt = ALT\n$mod = CTRL\nsource = ~/.config/hypr/bindings.conf\n");
        write(config(QStringLiteral("hypr/bindings.conf")), "bindd = $mainMod ALT, C, Mine, exec, mine\nbind = $mainMod $modAlt, A, exec, mine\nbind = $mod, L, exec, mine\n$mainMod = SUPER\n");
        const QSet<QString> taken = Setup::takenKeys(Setup::Environment::current());
        QVERIFY(taken.contains(QStringLiteral("SUPER+ALT+C")));
        // $mod is CTRL, but must not be found inside $modAlt.
        QVERIFY(taken.contains(QStringLiteral("SUPER+ALT+A")));
        QVERIFY(!taken.contains(QStringLiteral("SUPER+CTRL+ALT+A")));
        QVERIFY(taken.contains(QStringLiteral("CTRL+L")));
        QVERIFY(!taken.contains(QStringLiteral("ALT+C")));
    }

    void theTestsNeverSeeTheRealXdgFolders()
    {
        const QHash<QByteArray, QString> expected = {{"HOME", m_home.path()},          {"XDG_CONFIG_HOME", config(QString())}, {"XDG_STATE_HOME", m_state.path()},
                                                     {"XDG_DATA_HOME", m_data.path()}, {"XDG_CACHE_HOME", m_cache.path()}};
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            QCOMPARE(qEnvironmentVariable(it.key().constData()), it.value());
            QVERIFY2(it.value().startsWith(QDir::tempPath()), it.key().constData());
            // A child process gets the same, which is how jq and Hyprland's checker would see them.
            QProcess child;
            child.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("printf %s \"$") + QString::fromLatin1(it.key()) + QLatin1Char('"')});
            QVERIFY(child.waitForFinished(10'000));
            QCOMPARE(QString::fromUtf8(child.readAllStandardOutput()), it.value());
        }
        QVERIFY(qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"));
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

    // Runs Hyprland's own config checker on `path`; the probe file it may write is PROBE_OUT.
    QString verifyConfig(const QString &path, const QString &probe = QString())
    {
        QProcess check;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PROBE_OUT"), probe);
        check.setProcessEnvironment(environment);
        check.start(QStandardPaths::findExecutable(QStringLiteral("Hyprland")), {QStringLiteral("--verify-config"), QStringLiteral("-c"), path});
        if (!check.waitForFinished(30'000))
            return QStringLiteral("Hyprland's checker didn't finish.");
        return QString::fromUtf8(check.readAll());
    }

    // The tester's breakage: every bind after Omastrator's stopped working. A key file that fails half way, in any part, must leave
    // the submap closed so the user's and Omarchy's binds after it land in the default submap.
    void aFailingBindInTheKeyFileLeavesNoSubmapOpen_data()
    {
        QTest::addColumn<QString>("failing");
        QTest::newRow("Alt_L and Escape") << "keys == 'Alt_L' or keys == 'Escape'";
        QTest::newRow("every key of ours") << "keys ~= 'SUPER + 1'";
    }

    void aFailingBindInTheKeyFileLeavesNoSubmapOpen()
    {
        QFETCH(QString, failing);
        if (QStandardPaths::findExecutable(QStringLiteral("Hyprland")).isEmpty())
            QSKIP("Hyprland isn't installed, so its config checker can't run.");
        QDir().mkpath(m_state.filePath(QStringLiteral("omastrator")));
        QFile::remove(m_state.filePath(QStringLiteral("omastrator/setup.log")));
        write(config(QStringLiteral("omastrator/hyprland.lua")), Setup::hyprlandLua(QStringLiteral("omastrator")));
        const QString probe = m_home.filePath(QStringLiteral("probe.out")), path = m_home.filePath(QStringLiteral("hyprland-probe.lua"));
        QFile::remove(probe);
        // hl.bind fails for the keys chosen, as an unknown key name or a changed API would; then the user's config carries on.
        write(path, "local out = io.open(os.getenv('PROBE_OUT'), 'w')\n"
                    "local real = hl.bind\n"
                    "hl.bind = function(keys, action, options)\n"
                    "  if " + failing.toUtf8() + " then error('probe: no such key ' .. keys) end\n"
                    "  local bound = real(keys, action, options)\n"
                    "  out:write('bind ', keys, ' submap=[', tostring(bound.submap), ']\\n')\n"
                    "  return bound\n"
                    "end\n"
                    + Setup::sourceBlock(Setup::HyprFormat::lua) +
                    "local users = real('SUPER + 1', hl.dsp.focus({ workspace = 1 }))\n"
                    "out:write('users ', 'SUPER + 1', ' submap=[', tostring(users.submap), '] current=[', hl.get_current_submap(), ']\\n')\n"
                    "out:close()\n");
        const QString output = verifyConfig(path, probe);
        const QString seen = QString::fromUtf8(read(probe));
        // Hyprland itself finds nothing wrong with the file, and the user's bind is in the default submap, outside ours.
        QVERIFY2(seen.contains(QLatin1String("users SUPER + 1 submap=[] current=[]")), qPrintable(seen + output));
        QVERIFY2(!output.contains(QLatin1String("Lua error")), qPrintable(output));
        // The failure is reported, not swallowed.
        const QString log = QString::fromUtf8(read(m_state.filePath(QStringLiteral("omastrator/setup.log"))));
        QVERIFY2(log.contains(QLatin1String("probe: no such key")) && log.contains(QLatin1String("Omastrator: couldn't bind")), qPrintable(log));
        if (failing.contains(QLatin1String("Alt_L"))) {
            // The rest of the submap still loads.
            QVERIFY2(seen.contains(QLatin1String("bind ALT + Alt_L submap=[omastrator-design]")), qPrintable(seen));
            QVERIFY(seen.contains(QLatin1String("bind SUPER + ALT + Escape submap=[]")));
        }
    }

    // Both formats, with the user's bind after our source line, are accepted by Hyprland; the key names Alt_L, ALT + Alt_L and Escape are valid in both.
    void hyprlandLoadsTheUsersBindsAfterOurFile()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("Hyprland")).isEmpty())
            QSKIP("Hyprland isn't installed, so its config checker can't run.");
        write(config(QStringLiteral("omastrator/hyprland.lua")), Setup::hyprlandLua(QStringLiteral("omastrator")));
        write(config(QStringLiteral("omastrator/hyprland.conf")), Setup::hyprlandConf(QStringLiteral("omastrator")));
        const QByteArray lua = read(config(QStringLiteral("omastrator/hyprland.lua"))), conf = read(config(QStringLiteral("omastrator/hyprland.conf")));
        QVERIFY(lua.contains("\"Alt_L\"") && lua.contains("\"ALT + Alt_L\"") && lua.contains("\"Escape\""));
        QVERIFY(conf.contains("bind = , Alt_L,") && conf.contains("bindr = ALT, Alt_L,") && conf.contains(", escape,"));
        const QString luaPath = m_home.filePath(QStringLiteral("user.lua")), confPath = m_home.filePath(QStringLiteral("user.conf"));
        write(luaPath, Setup::sourceBlock(Setup::HyprFormat::lua) + "hl.bind('SUPER + 1', hl.dsp.focus({ workspace = 1 }))\n");
        // The user's own file ended inside a submap of theirs: ours still starts from the default one.
        write(confPath, "submap = mine\n" + Setup::sourceBlock(Setup::HyprFormat::conf) + "submap = reset\nbind = SUPER, 1, workspace, 1\n");
        for (const QString &path : {luaPath, confPath}) {
            const QString output = verifyConfig(path);
            QVERIFY2(output.contains(QLatin1String("config ok")), qPrintable(path + "\n" + output));
        }
    }

    // The generated Lua run against a stand-in for Hyprland's API: a failing dispatch can't stop a mode from handing the keyboard back.
    void leavingAModeClosesItsSubmapFirst()
    {
        const QString lua = QStandardPaths::findExecutable(QStringLiteral("lua"));
        if (lua.isEmpty())
            QSKIP("lua isn't installed.");
        QDir().mkpath(m_state.filePath(QStringLiteral("omastrator")));
        QFile::remove(m_state.filePath(QStringLiteral("omastrator/setup.log")));
        write(config(QStringLiteral("omastrator/hyprland.lua")), Setup::hyprlandLua(QStringLiteral("omastrator")));
        const QString script = m_home.filePath(QStringLiteral("stub.lua"));
        write(script, "local binds, calls, current = {}, {}, ''\n"
                      "hl = {\n"
                      "  dsp = { exec_cmd = function(c) return { 'exec', c } end, submap = function(n) return { 'submap', n } end },\n"
                      "  dispatch = function(d)\n"
                      "    if d[1] == 'exec' then error('the command failed') end\n"
                      "    current = d[2] == 'reset' and '' or d[2]\n"
                      "    calls[#calls + 1] = 'submap ' .. d[2]\n"
                      "  end,\n"
                      "  bind = function(keys, action, options) binds[current .. '|' .. keys] = { action = action, options = options } end,\n"
                      "  define_submap = function(name, body) current = name body() current = '' end,\n"
                      "  on = function() end, exec_cmd = function() end,\n"
                      "  notification = { create = function() end },\n"
                      "}\n"
                      "dofile(os.getenv('XDG_CONFIG_HOME') .. '/omastrator/hyprland.lua')\n"
                      "local function press(id) calls = {} binds[id].action() return table.concat(calls, ',') end\n"
                      "print('enter ' .. press('|SUPER + ALT + D'))\n"
                      "print('leave ' .. press('omastrator-draw|Escape'))\n"
                      "print('design ' .. press('omastrator-design|Escape'))\n"
                      "hl.dispatch(hl.dsp.submap('omastrator-live'))\n"
                      "print('hatch ' .. press('|SUPER + ALT + Escape') .. ' universal=' .. tostring(binds['|SUPER + ALT + Escape'].options.submap_universal))\n");
        QProcess run;
        run.start(lua, {script});
        QVERIFY(run.waitForFinished(30'000));
        const QString output = QString::fromUtf8(run.readAll());
        QVERIFY2(run.exitCode() == 0, qPrintable(output));
        // Entering still takes the submap when the island command fails; every way out hands the keyboard back even though the command fails.
        QVERIFY2(output.contains(QLatin1String("enter submap omastrator-draw\n")), qPrintable(output));
        QVERIFY2(output.contains(QLatin1String("leave submap reset\n")), qPrintable(output));
        QVERIFY2(output.contains(QLatin1String("design submap reset\n")), qPrintable(output));
        QVERIFY2(output.contains(QLatin1String("hatch submap reset universal=true")), qPrintable(output));
        QVERIFY(QString::fromUtf8(read(m_state.filePath(QStringLiteral("omastrator/setup.log")))).contains(QLatin1String("the command failed")));
    }

    void theConfKeysStartAndEndInTheDefaultSubmap()
    {
        const QByteArray conf = Setup::hyprlandConf(QStringLiteral("omastrator"));
        QVERIFY(conf.contains("\nsubmap = reset\n") && conf.indexOf("submap = reset") < conf.indexOf("bindd = SUPER ALT"));
        QVERIFY(conf.trimmed().endsWith("--daemon"));
        // Each submap is closed before the next line, and the reset key works inside them.
        QCOMPARE(conf.count("\nsubmap = omastrator-"), conf.count("\nsubmap = reset\n") - 1);
        QVERIFY(conf.contains("binddu = SUPER ALT, escape, Omastrator: reset, exec,"));
        QVERIFY(conf.contains("binddu = SUPER ALT, escape, Omastrator: reset, submap, reset\n"));
    }

    void setupPutsFilesBackWhenTheUsersBindsDisappear()
    {
        const QJsonArray theirs{bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Return")), bind(64, QStringLiteral("Q"))};
        liveBinds(theirs);
        // Once Hyprland loads the new key file, the workspace and terminal keys are gone.
        fakeKnob("vanish.json", {bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Return"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("no longer has these keys: Super+1, Super+Return")), qPrintable(out));
        QVERIFY2(out.contains(QLatin1String("Your keys are back")), qPrintable(out));
        QCOMPARE(read(config(QStringLiteral("hypr/hyprland.lua"))), userHypr);
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omarchy/plugins/omastrator.island"))));
        // A baseline reload, the reload after the writes, and the reload after the restore; and Hyprland really has the keys again.
        QCOMPARE(reloads(), 3);
        QVERIFY(hasBind(liveNow(), 64, QStringLiteral("1")) && hasBind(liveNow(), 64, QStringLiteral("Return")) && hasBind(liveNow(), 64, QStringLiteral("Q")));
    }

    void aRestoreThatDoesNotBringTheKeysBackSaysSo()
    {
        liveBinds({bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Q"))});
        // A pending edit of the user's own, which the second reload activates: the keys are gone with or without Omastrator's file.
        fakeKnob("stuck.json", {bind(64, QStringLiteral("1"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("no longer has these keys: Super+1")), qPrintable(out));
        QVERIFY2(out.contains(QLatin1String("Still missing with every file put back")) && out.contains(QLatin1String("hyprctl configerrors")), qPrintable(out));
        QVERIFY2(!out.contains(QLatin1String("Your keys are back")), qPrintable(out));
        // The files are back as they were, and the backup stays for the user.
        QCOMPARE(read(config(QStringLiteral("hypr/hyprland.lua"))), userHypr);
        QVERIFY(!QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QCOMPARE(backupNames().size(), 2);
    }

    void setupKeepsItsFilesWhenTheUsersBindsStay()
    {
        const QJsonArray binds{bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Return"))};
        liveBinds(binds);
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(!out.contains(QLatin1String("no longer has")) && out.contains(QLatin1String("Set up.")), qPrintable(out));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QCOMPARE(reloads(), 2);
        // Fed back through the fake Hyprland: theirs are there beside every key of ours, including the hatch inside a submap.
        const QJsonArray live = liveNow();
        QVERIFY(hasBind(live, 64, QStringLiteral("1")) && hasBind(live, 64, QStringLiteral("Return")));
        for (const char *key : {"D", "C", "A", "L", "V", "Escape"})
            QVERIFY2(hasBind(live, 72, QLatin1String(key)), key);
        QVERIFY(hasBind(live, 72, QStringLiteral("O")) && hasBind(live, 72, QStringLiteral("W")));
    }

    void aReloadThatFailsIsNotReportedAsSuccess()
    {
        liveBinds({bind(64, QStringLiteral("1"))});
        fakeKnob("reload-fails");
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("didn't reload")) && out.contains(QLatin1String("reload failed: the fake says no")), qPrintable(out));
        QVERIFY2(!out.contains(QLatin1String("Set up.")) && out.contains(QLatin1String("not reloaded")), qPrintable(out));
        // Nothing was thrown away: the files stay and the backup is named.
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QVERIFY(out.contains(QLatin1String("--restore ") + backupNames().first()));
    }

    void aHyprlandThatStopsAnsweringLeavesTheBackup()
    {
        liveBinds({bind(64, QStringLiteral("1"))});
        write(m_fake.filePath(QStringLiteral("dead-after-reload")), "");
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("didn't answer")) && !out.contains(QLatin1String("Set up.")), qPrintable(out));
        QVERIFY(QFileInfo::exists(config(QStringLiteral("omastrator/hyprland.lua"))));
        QVERIFY(out.contains(QLatin1String("--restore ") + backupNames().first()));
    }

    void keysHyprlandDidNotBindAreReported()
    {
        liveBinds({bind(64, QStringLiteral("1"))});
        fakeKnob("drop-own.json", {bind(72, QStringLiteral("Escape"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 1);
        QVERIFY2(out.contains(QLatin1String("didn't bind these of Omastrator's keys: Super+Alt+Escape")), qPrintable(out));
        QVERIFY2(!out.contains(QLatin1String("Set up.")) && out.contains(QLatin1String("--restore")), qPrintable(out));
        QVERIFY(hasBind(liveNow(), 64, QStringLiteral("1")));
    }

    void runtimeOnlyBindsAreNotMistakenForLostOnes()
    {
        // `hyprctl keyword bind` from an autostart script: a reload drops it whatever we do, so it isn't ours to lose.
        liveBinds({bind(64, QStringLiteral("1"))});
        runtimeBind(bind(64, QStringLiteral("F12")));
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(!out.contains(QLatin1String("no longer has")), qPrintable(out));
        QVERIFY(!hasBind(liveNow(), 64, QStringLiteral("F12")));
    }

    void anUpgradeFromThePreviousLuaKeyFileKeepsEveryKey()
    {
        // Setup as it was last version: the old file, its source line, and a Hyprland that has loaded both.
        write(config(QStringLiteral("omastrator/hyprland.lua")), OldKeyFiles::lua);
        write(config(QStringLiteral("hypr/hyprland.lua")), userHypr + OldKeyFiles::luaSource);
        liveBinds({bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Return"))});
        hyprlandLoadsTheConfig();
        QVERIFY(hasBind(liveNow(), 72, QStringLiteral("V")));
        QVERIFY2(Setup::designKeysLoaded(Setup::Environment::current()), "the old key file still defines the design submap");
        QVERIFY(Setup::submapDefined(Setup::Environment::current(), QStringLiteral("omastrator-draw")));

        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        // The old file's undescribed release bind on Super+Alt+V is not the user's, so nothing is skipped and nothing is called lost.
        QVERIFY2(!out.contains(QLatin1String("already yours")) && !out.contains(QLatin1String("no longer has")) && out.contains(QLatin1String("Set up.")), qPrintable(out));
        const QByteArray keys = read(config(QStringLiteral("omastrator/hyprland.lua")));
        QVERIFY(keys.contains("submap_universal = true"));
        QVERIFY(keys.contains("SUPER + ALT + V\", island(\"dictate start\")"));
        QVERIFY(keys.contains("SUPER + ALT + D\""));
        QCOMPARE(reloads(), 2);
        const QJsonArray live = liveNow();
        QVERIFY(hasBind(live, 64, QStringLiteral("1")) && hasBind(live, 72, QStringLiteral("V")) && hasBind(live, 72, QStringLiteral("D")));
        // The old file's source line was recognised, so it isn't added a second time.
        QCOMPARE(read(config(QStringLiteral("hypr/hyprland.lua"))).count("omastrator/hyprland.lua"), 1);
    }

    void anUpgradeFromThePreviousHyprlangKeyFileKeepsEveryKey()
    {
        QFile::remove(config(QStringLiteral("hypr/hyprland.lua")));
        write(config(QStringLiteral("hypr/hyprland.conf")), "source = ~/.config/hypr/bindings.conf\n\nsource = ~/.config/omastrator/hyprland.conf\n");
        write(config(QStringLiteral("omastrator/hyprland.conf")), OldKeyFiles::conf);
        liveBinds({bind(64, QStringLiteral("1")), bind(64, QStringLiteral("Return"))});
        hyprlandLoadsTheConfig();
        QVERIFY(hasBind(liveNow(), 72, QStringLiteral("escape")));
        QVERIFY(Setup::designKeysLoaded(Setup::Environment::current()));

        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(!out.contains(QLatin1String("already yours")) && !out.contains(QLatin1String("no longer has")) && out.contains(QLatin1String("Set up.")), qPrintable(out));
        QVERIFY(read(config(QStringLiteral("omastrator/hyprland.conf"))).contains("binddu = SUPER ALT, escape, Omastrator: reset, submap, reset"));
        // Both halves of the hatch report a description, so either says whose it is.
        int hatch = 0;
        for (const QJsonValue &value : liveNow()) {
            const QJsonObject live = value.toObject();
            if (live["modmask"].toInt() == 72 && live["key"].toString() == QLatin1String("escape")) {
                QCOMPARE(live["description"].toString(), QStringLiteral("Omastrator: reset"));
                ++hatch;
            }
        }
        QCOMPARE(hatch, 2);
        QVERIFY(hasBind(liveNow(), 64, QStringLiteral("1")));
    }

    void aRerunWithApplyChangesAndReloadsNothing()
    {
        liveBinds({bind(64, QStringLiteral("1"))});
        QString out;
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        const QStringList installed = snapshot(m_home.path());
        const int backups = backupNames().size();
        const int before = reloads();
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply")}, QString(), &out), 0);
        QVERIFY2(out.contains(QLatin1String("Everything is already set up.")) && !out.contains(QLatin1String("no longer has")), qPrintable(out));
        QCOMPARE(snapshot(m_home.path()), installed);
        QCOMPARE(backupNames().size(), backups);
        QCOMPARE(reloads(), before);
    }

    void theSourceLineAndTheKeyFileReportTheirOwnFailures()
    {
        QVERIFY(Setup::hasSourceLine(Setup::sourceBlock(Setup::HyprFormat::lua), Setup::HyprFormat::lua));
        QVERIFY(Setup::hasSourceLine(OldKeyFiles::luaSource, Setup::HyprFormat::lua));
        QVERIFY(Setup::hasSourceLine(Setup::sourceBlock(Setup::HyprFormat::conf), Setup::HyprFormat::conf));
        QVERIFY(!Setup::hasSourceLine("-- hyprland.lua\nrequire(\"hypr.bindings\")\n", Setup::HyprFormat::lua));
        QVERIFY(!Setup::hasSourceLine("source = ~/.config/hypr/bindings.conf\n", Setup::HyprFormat::conf));
        // A key file that fails to load is shown, not silent; and one notification covers the whole file.
        QVERIFY(Setup::sourceBlock(Setup::HyprFormat::lua).contains("if not ok then pcall(hl.notification.create"));
        const QByteArray lua = Setup::hyprlandLua(QStringLiteral("omastrator"));
        QVERIFY(lua.contains("if loading then failures[#failures + 1] = message else notify(message) end"));
        QVERIFY(lua.contains("if #failures > 0 then"));
        QCOMPARE(lua.count("hl.notification.create"), 1);
        QVERIFY(lua.contains("65536"));
    }

    void noKeysNeverReloadsHyprland()
    {
        liveBinds({bind(64, QStringLiteral("1"))});
        QCOMPARE(setup({QStringLiteral("--yes"), QStringLiteral("--apply"), QStringLiteral("--no-keys")}), 0);
        QVERIFY(!QFileInfo::exists(m_fake.filePath(QStringLiteral("calls.log"))));
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

int main(int argc, char *argv[])
{
    if (argc > 2 && QByteArray(argv[1]) == "--fake-hyprctl") {
        QStringList args;
        for (int i = 3; i < argc; ++i)
            args << QString::fromLocal8Bit(argv[i]);
        return fakeHyprctl(QString::fromLocal8Bit(argv[2]), args);
    }
    QCoreApplication app(argc, argv);
    SetupTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "SetupTests.moc"
