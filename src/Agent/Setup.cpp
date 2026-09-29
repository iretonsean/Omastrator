#include "Agent/Setup.h"
#include "Agent/BrowserHost.h"
#include "Agent/Vocabulary.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <map>

namespace {
const QStringList pluginFolders{QStringLiteral("omastrator.island"), QStringLiteral("omastrator.ai"), QStringLiteral("omastrator-ui")};
const QString islandFilter = QStringLiteral(
    "if ((.plugins // []) | map(.id) | index(\"omastrator.island\")) then . else .plugins = ((.plugins // []) + [{\"id\": \"omastrator.island\"}]) end");
const QString barFilter = QStringLiteral(
    "if ([.bar.layout[]?[]?.id] | index(\"omastrator.ai\")) then . else .bar.layout.right = ([{\"id\": \"omastrator.ai\"}] + (.bar.layout.right // [])) end");
const QString islandRemoval = QStringLiteral("if .plugins then .plugins |= map(select(.id != \"omastrator.island\")) else . end");
const QString barRemoval = QStringLiteral("if .bar.layout then .bar.layout |= map_values(map(select(.id != \"omastrator.ai\"))) else . end");

std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}

QString configHomeFor(const QString &home)
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    return given.isEmpty() ? QDir(home).filePath(QStringLiteral(".config")) : given;
}

// What setup.json says setup added.
struct Record {
    QStringList files;
    // Folders setup made, removed again once empty.
    QStringList directories;
    bool island = false;
    bool bar = false;
    bool shellJsonCreated = false;
    bool menu = false;
    bool menuComma = false;
    bool menuCreated = false;
    QString sourcePath;
    QByteArray sourceText;
    // chromium-flags.conf: the extension folder setup added to it, and whether setup made the file.
    QString flagsPath;
    QString flagsExtension;
    bool flagsCreated = false;
    // Keys setup left unbound because the user already uses them, so --remove and --restore know what was never taken.
    QStringList skippedKeys;

    static Record read(const QString &path)
    {
        Record record;
        const QJsonObject json = QJsonDocument::fromJson(readFile(path).value_or(QByteArray())).object();
        for (const QJsonValue &file : json["files"].toArray())
            record.files << file.toString();
        for (const QJsonValue &directory : json["directories"].toArray())
            record.directories << directory.toString();
        record.island = json["shellJson"]["island"].toBool();
        record.bar = json["shellJson"]["bar"].toBool();
        record.shellJsonCreated = json["shellJson"]["created"].toBool();
        record.menu = json["menu"]["block"].toBool();
        record.menuComma = json["menu"]["comma"].toBool();
        record.menuCreated = json["menu"]["created"].toBool();
        record.sourcePath = json["hyprSource"]["path"].toString();
        record.sourceText = json["hyprSource"]["text"].toString().toUtf8();
        record.flagsPath = json["chromiumFlags"]["path"].toString();
        record.flagsExtension = json["chromiumFlags"]["extension"].toString();
        record.flagsCreated = json["chromiumFlags"]["created"].toBool();
        for (const QJsonValue &key : json["skippedKeys"].toArray())
            record.skippedKeys << key.toString();
        return record;
    }
    bool isEmpty() const
    {
        return files.isEmpty() && directories.isEmpty() && !island && !bar && !menu && sourcePath.isEmpty() && flagsPath.isEmpty();
    }
    QByteArray toJson() const
    {
        return QJsonDocument(QJsonObject{
                                 {"version", 1},
                                 {"files", QJsonArray::fromStringList(files)},
                                 {"directories", QJsonArray::fromStringList(directories)},
                                 {"shellJson", QJsonObject{{"island", island}, {"bar", bar}, {"created", shellJsonCreated}}},
                                 {"menu", QJsonObject{{"block", menu}, {"comma", menuComma}, {"created", menuCreated}}},
                                 {"hyprSource", QJsonObject{{"path", sourcePath}, {"text", QString::fromUtf8(sourceText)}}},
                                 {"chromiumFlags", QJsonObject{{"path", flagsPath}, {"extension", flagsExtension}, {"created", flagsCreated}}},
                                 {"skippedKeys", QJsonArray::fromStringList(skippedKeys)},
                             })
            .toJson(QJsonDocument::Indented);
    }
};

// shell.json as the shell reads it: the user's file, else Omarchy's default.
QByteArray shellJsonBase(const Setup::Environment &environment, bool *exists)
{
    const auto user = readFile(environment.shellJson());
    *exists = user.has_value();
    if (user)
        return *user;
    return readFile(QDir(environment.omarchyPath).filePath(QStringLiteral("config/omarchy/shell.json"))).value_or("{\"version\": 1}\n");
}

QString hyprConfig(const Setup::Environment &environment, Setup::HyprFormat format)
{
    return QDir(environment.hyprDirectory()).filePath(format == Setup::HyprFormat::lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf"));
}

bool ask(QTextStream &in, QTextStream &out, const QString &question)
{
    out << question << " [y/N] ";
    out.flush();
    const QString answer = in.readLine().trimmed().toLower();
    return answer == QLatin1String("y") || answer == QLatin1String("yes");
}

QString omarchyShell()
{
    const QString overridden = qEnvironmentVariable("OMASTRATOR_OMARCHY_SHELL");
    return overridden.isEmpty() ? QStringLiteral("omarchy-shell") : overridden;
}
}

namespace Setup {
// Where a package for any prefix (/usr, ~/.local, …) puts the shell plugins next to `binary`,
// independent of the prefix: GNUInstallDirs' bindir and datadir are always "bin" and "share"
// relative to it, so `binary`'s ../share/omastrator/shell is always right once installed.
QString installedShellSource(const QString &binary)
{
    return QDir::cleanPath(QDir(QFileInfo(binary).absolutePath()).filePath(QStringLiteral("../share/omastrator/shell")));
}

ShellLocation locateShell(const QString &binary)
{
    ShellLocation location;
    const QString overridden = qEnvironmentVariable("OMASTRATOR_SHELL_DIR");
    const QString installed = installedShellSource(binary);
    if (!overridden.isEmpty())
        location.source = overridden;
    else if (QFileInfo::exists(installed))
        location.source = installed;
#ifdef OMASTRATOR_SHELL_SOURCE
    else if (QFileInfo::exists(QStringLiteral(OMASTRATOR_SHELL_SOURCE)))
        location.source = QStringLiteral(OMASTRATOR_SHELL_SOURCE);
#endif
    // Installed as share/omastrator/extras next to share/omastrator/shell; in the repo, extras/ next to shell/.
    if (!location.source.isEmpty()) {
        const QString extension = QDir::cleanPath(QDir(location.source).filePath(QStringLiteral("../extras/chromium-extension")));
        if (QFileInfo::exists(QDir(extension).filePath(QStringLiteral("manifest.json"))))
            location.extension = extension;
    }
    return location;
}

Environment Environment::current()
{
    Environment environment;
    environment.home = qEnvironmentVariable("HOME", QDir::homePath());
    environment.configHome = configHomeFor(environment.home);
    const QString state = qEnvironmentVariable("XDG_STATE_HOME");
    environment.stateHome = state.isEmpty() ? QDir(environment.home).filePath(QStringLiteral(".local/state")) : state;
    environment.omarchyPath = qEnvironmentVariable("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
    environment.binary = QCoreApplication::applicationFilePath();
    const ShellLocation shell = locateShell(environment.binary);
    environment.shellSource = shell.source;
    environment.extension = shell.extension;
    // The keys and menu run `omastrator` when that is this binary; otherwise its full path.
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("omastrator"));
    const bool same = !onPath.isEmpty() && QFileInfo(onPath).canonicalFilePath() == QFileInfo(environment.binary).canonicalFilePath();
    environment.command = same ? QStringLiteral("omastrator") : environment.binary;
    return environment;
}

QString Environment::omastratorConfig() const { return QDir(configHome).filePath(QStringLiteral("omastrator")); }
QString Environment::plugins() const { return QDir(configHome).filePath(QStringLiteral("omarchy/plugins")); }
QString Environment::shellJson() const { return QDir(configHome).filePath(QStringLiteral("omarchy/shell.json")); }
QString Environment::menu() const { return QDir(configHome).filePath(QStringLiteral("omarchy/extensions/omarchy-menu.jsonc")); }
QString Environment::hyprDirectory() const { return QDir(configHome).filePath(QStringLiteral("hypr")); }
QString Environment::record() const { return QDir(omastratorConfig()).filePath(QStringLiteral("setup.json")); }
QString Environment::backups() const { return QDir(stateHome).filePath(QStringLiteral("omastrator/setup-backups")); }
QString Environment::browserHostManifest() const
{
    return QDir(configHome).filePath(QStringLiteral("chromium/NativeMessagingHosts/%1.json").arg(QLatin1String(BrowserHost::name)));
}
QString Environment::chromiumFlags() const { return QDir(configHome).filePath(QStringLiteral("chromium-flags.conf")); }

QByteArray browserHostManifest(const QString &binary)
{
    // Chromium runs `path` with the extension's origin as its argument, which the CLI takes as `browser-host`.
    return QJsonDocument(QJsonObject{{"name", QLatin1String(BrowserHost::name)},
                                     {"description", QStringLiteral("Omastrator: Live in your own Chromium")},
                                     {"path", binary},
                                     {"type", QStringLiteral("stdio")},
                                     {"allowed_origins", QJsonArray{QStringLiteral("chrome-extension://%1/").arg(QLatin1String(extensionId))}}})
        .toJson(QJsonDocument::Indented);
}

QByteArray withExtension(const QByteArray &flags, const QString &folder)
{
    const QByteArray flag = "--load-extension=";
    QList<QByteArray> lines = flags.split('\n');
    for (QByteArray &line : lines) {
        if (!line.trimmed().startsWith(flag))
            continue;
        const QList<QByteArray> folders = line.trimmed().mid(flag.size()).split(',');
        if (folders.contains(folder.toUtf8()))
            return flags;
        line = line.trimmed() + (folders.size() == 1 && folders.front().isEmpty() ? "" : ",") + folder.toUtf8();
        return lines.join('\n');
    }
    return flags + (flags.isEmpty() || flags.endsWith('\n') ? "" : "\n") + flag + folder.toUtf8() + '\n';
}

QByteArray withoutExtension(const QByteArray &flags, const QString &folder)
{
    const QByteArray flag = "--load-extension=";
    QList<QByteArray> lines = flags.split('\n');
    for (qsizetype at = 0; at < lines.size(); ++at) {
        if (!lines[at].trimmed().startsWith(flag))
            continue;
        QList<QByteArray> folders = lines[at].trimmed().mid(flag.size()).split(',');
        if (!folders.removeOne(folder.toUtf8()))
            continue;
        if (folders.isEmpty())
            lines.removeAt(at);
        else
            lines[at] = flag + folders.join(',');
        return lines.join('\n');
    }
    return flags;
}

HyprFormat hyprFormat(const Environment &environment)
{
    return QFileInfo::exists(QDir(environment.hyprDirectory()).filePath(QStringLiteral("hyprland.lua"))) ? HyprFormat::lua : HyprFormat::conf;
}

bool submapDefined(const Environment &environment, const QString &submap)
{
    const bool lua = hyprFormat(environment) == HyprFormat::lua;
    const QString name = lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf");
    const QByteArray keys = readFile(QDir(environment.omastratorConfig()).filePath(name)).value_or(QByteArray());
    const QByteArray hypr = readFile(QDir(environment.hyprDirectory()).filePath(name)).value_or(QByteArray());
    const QByteArray defined = lua ? "define_submap(\"" + submap.toUtf8() + "\"" : "submap = " + submap.toUtf8() + "\n";
    return keys.contains(defined) && hypr.contains("omastrator/" + name.toUtf8());
}

bool designKeysLoaded(const Environment &environment)
{
    return submapDefined(environment, QStringLiteral("omastrator-design"));
}

std::vector<Change> installPlan(const Environment &environment, bool withBar, bool withSource, QStringList *notes, bool noKeys, QStringList *skippedKeys)
{
    std::vector<Change> plan;
    if (environment.shellSource.isEmpty()) {
        notes->append(QStringLiteral("The plugins' source folder wasn't found. Set OMASTRATOR_SHELL_DIR to Omastrator's shell/ folder."));
    } else {
        for (const QString &folder : pluginFolders) {
            const QDir source(QDir(environment.shellSource).filePath(folder));
            QDirIterator files(source.path(), QDir::Files, QDirIterator::Subdirectories);
            QStringList paths;
            while (files.hasNext())
                paths << files.next();
            paths.sort();
            for (const QString &path : paths) {
                const QString target = QDir(environment.plugins()).filePath(folder + QLatin1Char('/') + source.relativeFilePath(path));
                plan.push_back({QStringLiteral("plugins"), QStringLiteral("Install the island and tray light plugins in omarchy-shell"), target,
                                readFile(target), readFile(path), true});
            }
        }
    }

    const HyprFormat format = hyprFormat(environment);
    if (!noKeys) {
        const QString keys = QDir(environment.omastratorConfig()).filePath(format == HyprFormat::lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf"));
        const KeyChoice choice = chooseKeys(environment);
        const DesignKeys designKeys = DesignKeys::from(environment);
        plan.push_back({QStringLiteral("keys"), QStringLiteral("Write the island's Hyprland keys (Omastrator's own file)"), keys, readFile(keys),
                        format == HyprFormat::lua ? hyprlandLua(environment.command, designKeys, choice.skip)
                                                  : hyprlandConf(environment.command, designKeys, choice.skip)});
        if (skippedKeys)
            *skippedKeys = choice.skipped;
    }
    // The vocabulary is the user's to edit once written.
    const QString vocabulary = QDir(environment.omastratorConfig()).filePath(QStringLiteral("vocabulary.txt"));
    if (!QFileInfo::exists(vocabulary))
        plan.push_back({QStringLiteral("vocabulary"), QStringLiteral("Write the default dictation vocabulary"), vocabulary, std::nullopt,
                        Vocabulary::defaultText().toUtf8()});
    const QString binary = QDir(environment.omastratorConfig()).filePath(QStringLiteral("shell.json"));
    if (environment.command != QLatin1String("omastrator"))
        plan.push_back({QStringLiteral("binary"), QStringLiteral("Tell the plugins where Omastrator is (it isn't on PATH)"), binary, readFile(binary),
                        QJsonDocument(QJsonObject{{"binary", environment.binary}}).toJson(QJsonDocument::Indented)});

    bool exists = false;
    const QByteArray shell = shellJsonBase(environment, &exists);
    QString error;
    if (const auto island = jq(shell, islandFilter, &error)) {
        plan.push_back({QStringLiteral("island"), QStringLiteral("Turn on the island in ~/.config/omarchy/shell.json"), environment.shellJson(),
                        exists ? std::optional(shell) : std::nullopt, *island});
        if (withBar) {
            if (const auto bar = jq(*island, barFilter, &error))
                plan.push_back({QStringLiteral("bar"), QStringLiteral("Add the tray light to the bar's right section in ~/.config/omarchy/shell.json"),
                                environment.shellJson(), *island, *bar});
        }
    }
    if (!error.isEmpty())
        notes->append(error);

    const auto menu = readFile(environment.menu());
    bool comma = false;
    plan.push_back({QStringLiteral("menu"), QStringLiteral("Add an Omastrator group to the Omarchy menu"), environment.menu(), menu,
                    withMenuBlock(menu.value_or(QByteArray()), menuBlock(environment.command), &comma)});

    const QString hypr = hyprConfig(environment, format);
    const auto config = readFile(hypr);
    const QByteArray source = sourceBlock(format);
    // With no keys file there is nothing for the line to load.
    if (!noKeys && withSource) {
        if (!config || !config->contains(source.trimmed()))
            plan.push_back({QStringLiteral("source"), QStringLiteral("Load the island's keys from your Hyprland config"), hypr, config,
                            config.value_or(QByteArray()) + source});
    } else if (!noKeys && (!config || !config->contains(source.trimmed()))) {
        notes->append(QStringLiteral("To use the island's keys, add this to %1 (or run `omastrator setup --apply`):%2")
                          .arg(hypr, QString::fromUtf8(source).chopped(1)));
    }

    // Live in your own Chromium: the host its extension talks to, and the extension loaded the way Omarchy loads its own.
    const QString host = environment.browserHostManifest();
    plan.push_back({QStringLiteral("browserHost"), QStringLiteral("Let Omastrator's Chromium extension reach Omastrator (a native messaging host)"),
                    host, readFile(host), browserHostManifest(environment.binary)});
    if (environment.extension.isEmpty()) {
        notes->append(QStringLiteral("Omastrator's Chromium extension wasn't found next to the shell folder, so Live can't join your own Chromium."));
    } else {
        const auto flags = readFile(environment.chromiumFlags());
        plan.push_back({QStringLiteral("extension"), QStringLiteral("Load Omastrator's extension in Chromium (restart Chromium after)"),
                        environment.chromiumFlags(), flags, withExtension(flags.value_or(QByteArray()), environment.extension)});
    }
    return plan;
}

std::vector<Change> removalPlan(const Environment &environment, QStringList *notes)
{
    std::vector<Change> plan;
    const Record record = Record::read(environment.record());
    if (record.isEmpty())
        return plan;
    for (const QString &path : record.files) {
        if (const auto content = readFile(path))
            plan.push_back({QStringLiteral("files"), QStringLiteral("Delete the files setup wrote"), path, content, std::nullopt,
                            path.startsWith(environment.plugins())});
    }
    QString error;
    if (record.island || record.bar) {
        if (const auto shell = readFile(environment.shellJson())) {
            std::optional<QByteArray> edited = shell;
            if (record.island && edited)
                edited = jq(*edited, islandRemoval, &error);
            if (record.bar && edited)
                edited = jq(*edited, barRemoval, &error);
            if (edited)
                plan.push_back({QStringLiteral("shell"), QStringLiteral("Take the island and tray light out of ~/.config/omarchy/shell.json"),
                                environment.shellJson(), shell, *edited});
        }
    }
    if (!error.isEmpty())
        notes->append(error);
    if (record.menu) {
        if (const auto menu = readFile(environment.menu())) {
            QByteArray edited = withoutMenuBlock(*menu, record.menuComma);
            const bool empty = QByteArray(edited).replace('{', "").replace('}', "").trimmed().isEmpty();
            plan.push_back({QStringLiteral("menu"), QStringLiteral("Take the Omastrator group out of the Omarchy menu"), environment.menu(), menu,
                            record.menuCreated && empty ? std::nullopt : std::optional(edited)});
        }
    }
    if (!record.flagsPath.isEmpty()) {
        if (const auto flags = readFile(record.flagsPath)) {
            const QByteArray edited = withoutExtension(*flags, record.flagsExtension);
            plan.push_back({QStringLiteral("flags"), QStringLiteral("Stop loading Omastrator's extension in Chromium"), record.flagsPath, flags,
                            record.flagsCreated && edited.trimmed().isEmpty() ? std::nullopt : std::optional(edited)});
        }
    }
    if (!record.sourcePath.isEmpty()) {
        if (const auto config = readFile(record.sourcePath); config && config->contains(record.sourceText)) {
            QByteArray edited = *config;
            edited.remove(edited.lastIndexOf(record.sourceText), record.sourceText.size());
            plan.push_back({QStringLiteral("source"), QStringLiteral("Take the island's keys out of your Hyprland config"), record.sourcePath, config,
                            edited});
        }
    }
    return plan;
}

void reloadOmarchyShell(QStringList *notes)
{
    // The shell picks up new plugin files itself; enabling needs a rescan and a reload.
    const QString shell = omarchyShell();
    if (!QStandardPaths::findExecutable(shell).isEmpty() || QFileInfo(shell).isExecutable()) {
        for (const QString &call : {QStringLiteral("rescanPlugins"), QStringLiteral("reloadConfig")}) {
            QProcess process;
            process.setStandardOutputFile(QProcess::nullDevice());
            process.start(shell, {QStringLiteral("shell"), call});
            process.waitForFinished(10'000);
        }
    } else {
        *notes << QStringLiteral("omarchy-shell isn't running; the island appears the next time it starts.");
    }
}

int runCli(const QStringList &args, QTextStream &in, QTextStream &out, QTextStream &err)
{
    bool restoring = false;
    QString restoreName;
    QStringList flags;
    for (qsizetype at = 0; at < args.size(); ++at) {
        const QString &arg = args[at];
        if (arg == QLatin1String("--restore")) {
            restoring = true;
            // A backup's name follows --restore: the newest when it is left out.
            if (at + 1 < args.size() && !args[at + 1].startsWith(QLatin1Char('-')))
                restoreName = args[++at];
        } else if (QStringList{"--yes", "-y", "--apply", "--remove", "--dry-run", "--help", "-h", "--no-keys", "--list-backups"}.contains(arg)) {
            flags << arg;
        } else {
            err << QStringLiteral("Unknown option %1. Run `omastrator setup --help`.\n").arg(arg);
            return 1;
        }
    }
    if (flags.contains(QStringLiteral("--help")) || flags.contains(QStringLiteral("-h"))) {
        out << "Usage: omastrator setup [--yes] [--apply] [--no-keys] [--dry-run]\n"
               "       omastrator setup --remove [--yes] [--dry-run]\n"
               "       omastrator setup --restore [BACKUP] [--yes] [--dry-run]\n"
               "       omastrator setup --list-backups\n\n"
               "Installs the island and tray light in omarchy-shell, writes the island's\n"
               "Hyprland keys and the Omarchy menu entries, and offers the tray light for\n"
               "the bar. Each change is shown as a diff and asked about first.\n\n"
               "Before it changes anything, setup copies every file it will change to\n"
               "~/.local/state/omastrator/setup-backups/<date-time>/ (the newest 5 are\n"
               "kept). If that copy can't be made, nothing changes. Setup never takes a\n"
               "key you already use: it skips that key and says so.\n\n"
               "  --yes           Accept every change without asking.\n"
               "  --apply         Also add the line that loads the keys to your Hyprland config.\n"
               "  --no-keys       Install without any global keys: the app, menu and plugins only.\n"
               "  --dry-run       Show what would change; change nothing.\n"
               "  --remove        Take out exactly what setup added.\n"
               "  --restore       Put back the files from the newest backup, or from BACKUP (a\n"
               "                  name from --list-backups), after showing what will change.\n"
               "  --list-backups  List the backups, newest first.\n";
        return 0;
    }
    const bool yes = flags.contains(QStringLiteral("--yes")) || flags.contains(QStringLiteral("-y"));
    const bool dryRun = flags.contains(QStringLiteral("--dry-run"));
    const bool removing = flags.contains(QStringLiteral("--remove"));
    const bool noKeys = flags.contains(QStringLiteral("--no-keys"));
    const bool listing = flags.contains(QStringLiteral("--list-backups"));
    const bool withSource = flags.contains(QStringLiteral("--apply"));
    if ((restoring || listing) && (removing || withSource || noKeys || (restoring && listing))) {
        err << QStringLiteral("--restore and --list-backups can't be combined with the other options. Run `omastrator setup --help`.\n");
        return 1;
    }
    const Environment environment = Environment::current();
    if (listing)
        return runListBackups(environment, out);
    if (restoring)
        return runRestore(environment, restoreName, yes, dryRun, in, out, err);

    QStringList notes;
    if (!removing) {
        const QStringList missing = missingTools();
        if (!missing.isEmpty()) {
            out << "Some desktop features need programs that aren't installed:\n";
            for (const QString &tool : missing)
                out << "  " << tool << '\n';
            out << "Setup doesn't install them; the commands above do.\n\n";
        }
    }
    // The tray light is offered, not assumed.
    bool withBar = yes;
    if (!removing && !yes && !dryRun) {
        bool exists = false;
        QString error;
        const auto has = jq(shellJsonBase(environment, &exists), QStringLiteral("[.bar.layout[]?[]?.id] | index(\"omastrator.ai\") != null"), &error);
        if (!has || has->trimmed() != "true")
            withBar = ask(in, out, QStringLiteral("Add the Omastrator AI light to the bar?"));
    }
    QStringList skippedKeys;
    std::vector<Change> plan = removing ? removalPlan(environment, &notes) : installPlan(environment, withBar || dryRun, withSource, &notes, noKeys, &skippedKeys);

    // One question per step; a step's files go together.
    std::vector<QString> order;
    std::map<QString, std::vector<Change *>> steps;
    for (Change &change : plan) {
        if (!change.changes())
            continue;
        if (!steps.count(change.key))
            order.push_back(change.key);
        steps[change.key].push_back(&change);
    }
    const auto skippedNote = [](const QString &key) { return QStringLiteral("%1 is already yours: skipped").arg(key); };
    if (order.empty()) {
        out << (removing ? "Setup hasn't added anything here, so there is nothing to remove.\n" : "Everything is already set up.\n");
        for (const QString &key : skippedKeys)
            out << skippedNote(key) << '\n';
        for (const QString &note : notes)
            out << '\n' << note << '\n';
        return 0;
    }

    Record record = Record::read(environment.record());
    const QString backupName = newBackupName(environment);
    const QString backupFolder = QDir(environment.backups()).filePath(backupName);
    if (noKeys) {
        out << "No keys: Hyprland and its keys are left alone. Reach design mode from the Omarchy menu (Omastrator, Island Mode, Design) or with\n"
               "`omastrator design on`, and start the background app with `omastrator daemon start`.\n";
        if (!record.files.contains(QDir(environment.omastratorConfig()).filePath(QStringLiteral("hyprland.lua")))
            && !record.files.contains(QDir(environment.omastratorConfig()).filePath(QStringLiteral("hyprland.conf"))))
            out << '\n';
        else
            out << "Keys from an earlier setup stay as they are; `omastrator setup --remove` takes them out.\n\n";
    }
    out << (dryRun ? "Before changing anything, setup would copy every file it changes to " : "Before changing anything, setup copies every file it changes to ")
        << backupFolder << ".\n";

    std::vector<QString> accepted;
    int applied = 0;
    for (const QString &key : order) {
        const std::vector<Change *> &step = steps[key];
        out << "\n== " << step.front()->title << " ==\n";
        if (key == QLatin1String("keys")) {
            for (const QString &skipped : skippedKeys)
                out << "  " << skippedNote(skipped) << '\n';
        }
        for (const Change *change : step) {
            if (change->summarize)
                out << (change->before ? (change->after ? "  update " : "  delete ") : "  new    ") << change->path << '\n';
            else
                out << unifiedDiff(change->path, change->before, change->after);
        }
        if (dryRun) {
            ++applied;
            continue;
        }
        if (!yes && !ask(in, out, removing ? QStringLiteral("Undo this?") : QStringLiteral("Make this change?"))) {
            out << "Skipped.\n";
            continue;
        }
        accepted.push_back(key);
    }

    if (!accepted.empty()) {
        // Every file the accepted steps write, and setup's own record: copied before the first one is touched.
        QStringList paths{environment.record()};
        for (const QString &key : accepted) {
            for (const Change *change : steps[key])
                paths << change->path;
        }
        paths.removeDuplicates();
        Backup made;
        if (const QString failure = writeBackup(environment, backupName, removing ? QStringLiteral("remove") : QStringLiteral("setup"), paths, &made);
            !failure.isEmpty()) {
            err << QStringLiteral("Nothing was changed: setup couldn't make its backup. %1\n").arg(failure);
            return 1;
        }
        pruneBackups(environment, backupName);
        out << "\nBacked up to " << backupFolder << ".\n";
    }

    bool reloadShell = false;
    for (const QString &key : accepted) {
        const std::vector<Change *> &step = steps[key];
        for (const Change *change : step) {
            // JSON edits apply to the file as it is now, so a skipped step above doesn't undo this one.
            std::optional<QByteArray> after = change->after;
            if (!removing && (key == QLatin1String("island") || key == QLatin1String("bar"))) {
                bool exists = false;
                QString error;
                after = jq(shellJsonBase(environment, &exists), key == QLatin1String("island") ? islandFilter : barFilter, &error);
                if (!after) {
                    err << error << '\n';
                    continue;
                }
                record.shellJsonCreated = record.shellJsonCreated || !exists;
            }
            if (after) {
                for (QString folder = QFileInfo(change->path).absolutePath(); !QFileInfo::exists(folder); folder = QFileInfo(folder).absolutePath()) {
                    if (!record.directories.contains(folder))
                        record.directories << folder;
                }
                if (const QString failure = writeFile(change->path, *after); !failure.isEmpty()) {
                    err << failure << '\n'
                        << QStringLiteral("Setup stopped part way. `omastrator setup --restore` puts back the files it copied to %1.\n").arg(backupFolder);
                    return 1;
                }
            } else {
                QFile::remove(change->path);
            }
            if (!removing && !change->before && after && !record.files.contains(change->path)
                && (key == QLatin1String("plugins") || key == QLatin1String("keys") || key == QLatin1String("vocabulary") || key == QLatin1String("binary")
                    || key == QLatin1String("browserHost")))
                record.files << change->path;
        }
        if (removing) {
            if (key == QLatin1String("files"))
                record.files.clear(), record.skippedKeys.clear();
            if (key == QLatin1String("shell"))
                record.island = record.bar = false;
            if (key == QLatin1String("menu"))
                record.menu = false;
            if (key == QLatin1String("source"))
                record.sourcePath.clear();
            if (key == QLatin1String("flags"))
                record.flagsPath.clear(), record.flagsExtension.clear(), record.flagsCreated = false;
        } else {
            if (key == QLatin1String("keys"))
                record.skippedKeys = skippedKeys;
            if (key == QLatin1String("island"))
                record.island = true;
            if (key == QLatin1String("bar"))
                record.bar = true;
            if (key == QLatin1String("menu")) {
                record.menuCreated = record.menuCreated || !step.front()->before;
                bool comma = false;
                withMenuBlock(step.front()->before.value_or(QByteArray()), menuBlock(environment.command), &comma);
                record.menuComma = record.menuComma || comma;
                record.menu = true;
            }
            if (key == QLatin1String("source")) {
                record.sourcePath = step.front()->path;
                record.sourceText = sourceBlock(hyprFormat(environment));
            }
            if (key == QLatin1String("extension")) {
                record.flagsCreated = record.flagsCreated || !step.front()->before;
                record.flagsPath = step.front()->path;
                record.flagsExtension = environment.extension;
            }
        }
        reloadShell = reloadShell || key == QLatin1String("plugins") || key == QLatin1String("island") || key == QLatin1String("bar")
                      || key == QLatin1String("shell") || key == QLatin1String("files");
        ++applied;
    }
    if (dryRun) {
        out << "\nDry run: nothing changed.\n";
    } else if (applied > 0) {
        // The record is setup's own bookkeeping: kept while anything it added remains.
        Record rest = record;
        rest.directories.clear();
        if (removing && rest.isEmpty()) {
            QFile::remove(environment.record());
            // Deepest first, and only once empty: anything the user put there stays.
            std::sort(record.directories.begin(), record.directories.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
            QStringList kept;
            for (const QString &folder : std::as_const(record.directories)) {
                if (QFileInfo::exists(folder) && !QDir().rmdir(folder))
                    kept << folder;
            }
            record.directories = kept;
        }
        if (!record.isEmpty()) {
            for (QString folder = QFileInfo(environment.record()).absolutePath(); !QFileInfo::exists(folder); folder = QFileInfo(folder).absolutePath())
                record.directories << folder;
            writeFile(environment.record(), record.toJson());
        }
    }
    if (reloadShell && !dryRun)
        reloadOmarchyShell(&notes);
    for (const QString &note : notes)
        out << '\n' << note << '\n';
    out << '\n' << (removing ? "Removed." : "Set up.") << ' ' << applied << (applied == 1 ? " step" : " steps") << (dryRun ? " would be applied.\n" : " applied.\n");
    if (!accepted.empty())
        out << "To put every file back as it was: omastrator setup --restore " << backupName << '\n';
    return 0;
}
}
