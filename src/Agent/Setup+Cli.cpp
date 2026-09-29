#include "Agent/Setup.h"
#include "Agent/SetupInternal.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <map>

using namespace SetupInternal;

namespace {
QString writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}

bool ask(QTextStream &in, QTextStream &out, const QString &question)
{
    out << question << " [y/N] ";
    out.flush();
    const QString answer = in.readLine().trimmed().toLower();
    return answer == QLatin1String("y") || answer == QLatin1String("yes");
}
}

namespace Setup {
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
               "  --apply         Also add the line that loads the keys to your Hyprland config, then\n"
               "                  reload Hyprland (before and after) and check your own keys and ours are all\n"
               "                  bound; if one of yours is gone, restore the backup and check again.\n"
               "  --no-keys       Install without any global keys: the app, menu and plugins only.\n"
               "  --dry-run       Show what would change; change nothing.\n"
               "  --remove        Take out exactly what setup added.\n"
               "  --restore       Put back the files from the newest backup, or from BACKUP (a\n"
               "                  name from --list-backups), after showing what will change.\n"
               "                  The files it replaces are backed up first.\n"
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

    const bool accepts = !removing && !dryRun;
    const auto has = [&](const char *key) { return std::find(accepted.begin(), accepted.end(), QLatin1String(key)) != accepted.end(); };
    const KeyCheck keyCheck = accepts ? startKeyCheck(environment, has("keys"), has("source"), skippedKeys, out, err) : KeyCheck();
    if (keyCheck.stopped)
        return 1;

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
        pruneBackups(environment, {backupName});
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
    QString problem;
    if (keyCheck.active) {
        const KeyOutcome outcome = finishKeyCheck(keyCheck, environment, backupName, in, out, err, &problem);
        if (outcome == KeyOutcome::restored)
            return 1;
    }
    if (reloadShell && !dryRun)
        reloadOmarchyShell(&notes);
    for (const QString &note : notes)
        out << '\n' << note << '\n';
    if (!problem.isEmpty()) {
        err << '\n' << problem << ". " << applied << (applied == 1 ? " step" : " steps") << " applied; the backup is kept: omastrator setup --restore " << backupName << '\n';
        return 1;
    }
    out << '\n' << (removing ? "Removed." : "Set up.") << ' ' << applied << (applied == 1 ? " step" : " steps") << (dryRun ? " would be applied.\n" : " applied.\n");
    if (!accepted.empty())
        out << "To put every file back as it was: omastrator setup --restore " << backupName << '\n';
    return 0;
}
}
