#include "Agent/Setup.h"
#include "Agent/SetupInternal.h"
#include "Agent/BrowserHost.h"
#include "Agent/Hyprland.h"
#include "Agent/Vocabulary.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>

namespace SetupInternal {
const QString islandFilter = QStringLiteral(
    "if ((.plugins // []) | map(.id) | index(\"omastrator.island\")) then . else .plugins = ((.plugins // []) + [{\"id\": \"omastrator.island\"}]) end");
const QString barFilter = QStringLiteral(
    "if ([.bar.layout[]?[]?.id] | index(\"omastrator.ai\")) then . else .bar.layout.right = ([{\"id\": \"omastrator.ai\"}] + (.bar.layout.right // [])) end");

std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return file.readAll();
}

// shell.json as the shell reads it: the user's file, else Omarchy's default.
QByteArray shellJsonBase(const Setup::Environment &environment, bool *exists)
{
    const auto user = readFile(environment.shellJson());
    *exists = user.has_value();
    if (user)
        return *user;
    return readFile(QDir(environment.omarchyPath).filePath(QStringLiteral("config/omarchy/shell.json"))).value_or("{\"version\": 1}\n");
}
}
using namespace SetupInternal;

namespace {
const QStringList pluginFolders{QStringLiteral("omastrator.island"), QStringLiteral("omastrator.ai"), QStringLiteral("omastrator-ui")};
const QString islandRemoval = QStringLiteral("if .plugins then .plugins |= map(select(.id != \"omastrator.island\")) else . end");
const QString barRemoval = QStringLiteral("if .bar.layout then .bar.layout |= map_values(map(select(.id != \"omastrator.ai\"))) else . end");

QString configHomeFor(const QString &home)
{
    const QString given = qEnvironmentVariable("XDG_CONFIG_HOME");
    return given.isEmpty() ? QDir(home).filePath(QStringLiteral(".config")) : given;
}

QString hyprConfig(const Setup::Environment &environment, Setup::HyprFormat format)
{
    return QDir(environment.hyprDirectory()).filePath(format == Setup::HyprFormat::lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf"));
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
QString Environment::setupLog() const { return QDir(stateHome).filePath(QStringLiteral("omastrator/setup.log")); }
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
    // The Lua key file has defined its submaps two ways (`submap(` now, `hl.define_submap(` before), and both still load.
    const QByteArray quoted = "(\"" + submap.toUtf8() + "\"";
    const bool defined = lua ? keys.contains("\nsubmap" + quoted) || keys.contains("define_submap" + quoted) : keys.contains("submap = " + submap.toUtf8() + "\n");
    return defined && hasSourceLine(hypr, hyprFormat(environment));
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
        if (!config || !hasSourceLine(*config, format))
            plan.push_back({QStringLiteral("source"), QStringLiteral("Load the island's keys from your Hyprland config"), hypr, config,
                            config.value_or(QByteArray()) + source});
    } else if (!noKeys && (!config || !hasSourceLine(*config, format))) {
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
}
