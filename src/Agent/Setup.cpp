#include "Agent/Setup.h"
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
        return record;
    }
    bool isEmpty() const { return files.isEmpty() && directories.isEmpty() && !island && !bar && !menu && sourcePath.isEmpty(); }
    QByteArray toJson() const
    {
        return QJsonDocument(QJsonObject{
                                 {"version", 1},
                                 {"files", QJsonArray::fromStringList(files)},
                                 {"directories", QJsonArray::fromStringList(directories)},
                                 {"shellJson", QJsonObject{{"island", island}, {"bar", bar}, {"created", shellJsonCreated}}},
                                 {"menu", QJsonObject{{"block", menu}, {"comma", menuComma}, {"created", menuCreated}}},
                                 {"hyprSource", QJsonObject{{"path", sourcePath}, {"text", QString::fromUtf8(sourceText)}}},
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
Environment Environment::current()
{
    Environment environment;
    environment.home = qEnvironmentVariable("HOME", QDir::homePath());
    environment.configHome = configHomeFor(environment.home);
    environment.omarchyPath = qEnvironmentVariable("OMARCHY_PATH", QStringLiteral("/usr/share/omarchy"));
    environment.binary = QCoreApplication::applicationFilePath();
    const QString overridden = qEnvironmentVariable("OMASTRATOR_SHELL_DIR");
    const QString installed = QDir(QFileInfo(environment.binary).absolutePath()).filePath(QStringLiteral("../share/omastrator/shell"));
    if (!overridden.isEmpty())
        environment.shellSource = overridden;
    else if (QFileInfo::exists(installed))
        environment.shellSource = QDir::cleanPath(installed);
#ifdef OMASTRATOR_SHELL_SOURCE
    else if (QFileInfo::exists(QStringLiteral(OMASTRATOR_SHELL_SOURCE)))
        environment.shellSource = QStringLiteral(OMASTRATOR_SHELL_SOURCE);
#endif
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

HyprFormat hyprFormat(const Environment &environment)
{
    return QFileInfo::exists(QDir(environment.hyprDirectory()).filePath(QStringLiteral("hyprland.lua"))) ? HyprFormat::lua : HyprFormat::conf;
}

std::vector<Change> installPlan(const Environment &environment, bool withBar, bool withSource, QStringList *notes)
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
    const QString keys = QDir(environment.omastratorConfig()).filePath(format == HyprFormat::lua ? QStringLiteral("hyprland.lua") : QStringLiteral("hyprland.conf"));
    plan.push_back({QStringLiteral("keys"), QStringLiteral("Write the island's Hyprland keys (Omastrator's own file)"), keys, readFile(keys),
                    format == HyprFormat::lua ? hyprlandLua(environment.command, DesignKeys::from(environment))
                                              : hyprlandConf(environment.command, DesignKeys::from(environment))});
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
    if (withSource) {
        if (!config || !config->contains(source.trimmed()))
            plan.push_back({QStringLiteral("source"), QStringLiteral("Load the island's keys from your Hyprland config"), hypr, config,
                            config.value_or(QByteArray()) + source});
    } else if (!config || !config->contains(source.trimmed())) {
        notes->append(QStringLiteral("To use the island's keys, add this to %1 (or run `omastrator setup --apply`):%2")
                          .arg(hypr, QString::fromUtf8(source).chopped(1)));
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

int runCli(const QStringList &args, QTextStream &in, QTextStream &out, QTextStream &err)
{
    for (const QString &arg : args) {
        if (!QStringList{"--yes", "-y", "--apply", "--remove", "--dry-run", "--help", "-h"}.contains(arg)) {
            err << QStringLiteral("Unknown option %1. Run `omastrator setup --help`.\n").arg(arg);
            return 1;
        }
    }
    if (args.contains(QStringLiteral("--help")) || args.contains(QStringLiteral("-h"))) {
        out << "Usage: omastrator setup [--yes] [--apply] [--dry-run]\n"
               "       omastrator setup --remove [--yes] [--dry-run]\n\n"
               "Installs the island and tray light in omarchy-shell, writes the island's\n"
               "Hyprland keys and the Omarchy menu entries, and offers the tray light for\n"
               "the bar. Each change is shown as a diff and asked about first.\n\n"
               "  --yes      Accept every change without asking.\n"
               "  --apply    Also add the line that loads the keys to your Hyprland config.\n"
               "  --dry-run  Show what would change; change nothing.\n"
               "  --remove   Take out exactly what setup added.\n";
        return 0;
    }
    const bool yes = args.contains(QStringLiteral("--yes")) || args.contains(QStringLiteral("-y"));
    const bool dryRun = args.contains(QStringLiteral("--dry-run"));
    const bool removing = args.contains(QStringLiteral("--remove"));
    const Environment environment = Environment::current();

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
    const bool withSource = args.contains(QStringLiteral("--apply"));
    std::vector<Change> plan = removing ? removalPlan(environment, &notes) : installPlan(environment, withBar || dryRun, withSource, &notes);

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
    if (order.empty()) {
        out << (removing ? "Setup hasn't added anything here, so there is nothing to remove.\n" : "Everything is already set up.\n");
        for (const QString &note : notes)
            out << '\n' << note << '\n';
        return 0;
    }

    Record record = Record::read(environment.record());
    bool reloadShell = false;
    int applied = 0;
    for (const QString &key : order) {
        const std::vector<Change *> &step = steps[key];
        out << "\n== " << step.front()->title << " ==\n";
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
                    err << failure << '\n';
                    return 1;
                }
            } else {
                QFile::remove(change->path);
            }
            if (!removing && !change->before && after && !record.files.contains(change->path)
                && (key == QLatin1String("plugins") || key == QLatin1String("keys") || key == QLatin1String("vocabulary") || key == QLatin1String("binary")))
                record.files << change->path;
        }
        if (removing) {
            if (key == QLatin1String("files"))
                record.files.clear();
            if (key == QLatin1String("shell"))
                record.island = record.bar = false;
            if (key == QLatin1String("menu"))
                record.menu = false;
            if (key == QLatin1String("source"))
                record.sourcePath.clear();
        } else {
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
    if (reloadShell && !dryRun) {
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
            notes << QStringLiteral("omarchy-shell isn't running; the island appears the next time it starts.");
        }
    }
    for (const QString &note : notes)
        out << '\n' << note << '\n';
    out << '\n' << (removing ? "Removed." : "Set up.") << ' ' << applied << (applied == 1 ? " step" : " steps") << (dryRun ? " would be applied.\n" : " applied.\n");
    return 0;
}
}
