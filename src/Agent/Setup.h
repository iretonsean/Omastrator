#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <optional>
#include <vector>

// `omastrator setup` (docs/OS-SUITE.md): installs the island and tray light
// plugins, writes the Hyprland keys, the Omarchy menu entries and the
// dictation vocabulary, and offers the bar widget and the Hyprland source
// line. Every change to a file is shown as a diff and confirmed first, after
// a copy of each file it will change goes to setup-backups (`--restore` puts
// it back); `--remove` takes out exactly what setup added, as recorded in
// setup.json. Setup never takes a key the user already uses, and `--no-keys`
// takes none.
namespace Setup {
struct Environment {
    QString home;
    QString configHome;
    // $XDG_STATE_HOME, else ~/.local/state.
    QString stateHome;
    QString omarchyPath;
    // The repo's or package's shell/ folder the plugins are copied from.
    QString shellSource;
    // What the generated keys and menu entries run: "omastrator", or this binary's path when that isn't on PATH.
    QString command;
    QString binary;
    // Omastrator's Chromium extension (Live in your own browser), next to the shell folder; empty when missing.
    QString extension;

    // From $HOME, $XDG_CONFIG_HOME, $OMARCHY_PATH and $OMASTRATOR_SHELL_DIR.
    static Environment current();
    QString omastratorConfig() const;
    QString plugins() const;
    QString shellJson() const;
    QString menu() const;
    QString hyprDirectory() const;
    QString record() const;
    // Where every setup or remove keeps a copy of the files it changes: <stateHome>/omastrator/setup-backups.
    QString backups() const;
    // Where Omastrator's key file writes what went wrong while Hyprland loaded it: <stateHome>/omastrator/setup.log.
    QString setupLog() const;
    // Chromium's per-user native messaging host manifest, and the flags file its launcher reads.
    QString browserHostManifest() const;
    QString chromiumFlags() const;
};

// Where the shell plugins live next to a binary, and Omastrator's Chromium extension next to
// those (empty when either isn't found).
struct ShellLocation {
    QString source;
    QString extension;
};
// `binary`'s ../share/omastrator/shell: where a package for any prefix (/usr, ~/.local, …)
// puts the shell plugins, since GNUInstallDirs' bindir and datadir are always "bin" and
// "share" relative to the prefix. Doesn't check it exists.
QString installedShellSource(const QString &binary);
// $OMASTRATOR_SHELL_DIR if set, else `installedShellSource(binary)` if that exists, else (in a
// build from source) the repo's own shell/ folder.
ShellLocation locateShell(const QString &binary);

// One file setup writes, removes or edits. `before` and `after` are nullopt for a file that is absent.
struct Change {
    QString key;
    QString title;
    QString path;
    std::optional<QByteArray> before;
    std::optional<QByteArray> after;
    // Plugin files: name them in the diff rather than print them whole.
    bool summarize = false;
    bool changes() const { return before != after; }
};

enum class HyprFormat { lua, conf };
// Lua when ~/.config/hypr/hyprland.lua exists (Omarchy 4), else hyprlang.
HyprFormat hyprFormat(const Environment &environment);

// Design mode's keys (docs/ANYWHERE.md), in Lua's "SUPER + ALT + O" form. Omarchy's defaults leave both free;
// "keys" in ~/.config/omastrator/anywhere.json remaps them the next time setup runs.
struct DesignKeys {
    QString design = QStringLiteral("SUPER + ALT + O");
    QString desk = QStringLiteral("SUPER + ALT + W");
    static DesignKeys from(const Environment &environment);
};

// A key combination the way Hyprland matches it: modifiers in the order SUPER, CTRL, ALT, SHIFT, then the key,
// upper case ("SUPER+ALT+O"). Reads Lua's "SUPER + ALT + O".
QString normalizeCombo(const QString &lua);
// "SUPER+ALT+ESCAPE" as people write it: "Super+Alt+Escape".
QString displayCombo(const QString &normalized);
// Every global key setup would bind, normalised: the mode keys, dictation, design mode, the Desk and the reset.
QStringList omastratorKeys(const DesignKeys &keys);
// The global keys the user already binds (Hyprland's live binds, else their config), normalised. Omastrator's own binds don't count,
// nor do keys used only inside a submap: the keys setup binds inside its own submaps can't collide with anything.
QSet<QString> takenKeys(const Environment &environment);
struct KeyChoice {
    // Normalised keys setup leaves out of the file it writes, and the same for display.
    QStringList skip;
    QStringList skipped;
};
KeyChoice chooseKeys(const Environment &environment);

// The generated files. `skip` is normalised keys to leave unbound (see chooseKeys).
QByteArray hyprlandLua(const QString &command, const DesignKeys &keys = {}, const QStringList &skip = {});
QByteArray hyprlandConf(const QString &command, const DesignKeys &keys = {}, const QStringList &skip = {});
// The text setup appends to the user's Hyprland config with --apply.
QByteArray sourceBlock(HyprFormat format);
// The Omastrator entries of the Omarchy menu, between BEGIN and END markers.
QByteArray menuBlock(const QString &command);
// Adds or replaces the marked block in a JSONC menu file; sets `addedComma` if the entry before it needed one.
QByteArray withMenuBlock(const QByteArray &current, const QByteArray &block, bool *addedComma);
QByteArray withoutMenuBlock(const QByteArray &current, bool removeComma);

// Lines of `edited` that jq only re-encoded (a "\u2014" written as "—") take `original`'s bytes back.
QByteArray keepEscapes(const QByteArray &original, const QByteArray &edited);

// The extension's stable id, from the public key in its manifest.
inline constexpr const char *extensionId = "gmanolpmdkmgccoeiogpdhjifdkdjfap";
QByteArray browserHostManifest(const QString &binary);
// chromium-flags.conf with `folder` in its --load-extension list (a line of its own when there is none), and without it.
QByteArray withExtension(const QByteArray &flags, const QString &folder);
QByteArray withoutExtension(const QByteArray &flags, const QString &folder);
// Runs jq's `filter` on `input`: jq keeps the key order and layout the shell writes, and keepEscapes the escapes.
std::optional<QByteArray> jq(const QByteArray &input, const QString &filter, QString *error);
// `diff -u` style, computed here.
// Whether Hyprland loads setup's design keys: Omastrator's key file defines the design submap and Hyprland's
// config sources it. Entering a submap Hyprland doesn't define would leave the user with no keybindings.
bool designKeysLoaded(const Environment &environment);
// The same for any of Omastrator's submaps (omastrator-draw, -capture, -ai, -live, -heard, -design).
bool submapDefined(const Environment &environment, const QString &submap);
QString unifiedDiff(const QString &path, const std::optional<QByteArray> &before, const std::optional<QByteArray> &after);
// "grim (sudo pacman -S grim)" for each program Omastrator's desktop features need and can't find.
QStringList missingTools();
// `word` safe for a shell command line.
QString shellQuote(const QString &word);

// What setup would change now, and what --remove would. `noKeys` leaves out the Hyprland keys and the line that loads them;
// `skippedKeys` gets the keys left alone because the user already uses them ("Super+Alt+C").
std::vector<Change> installPlan(const Environment &environment, bool withBar, bool withSource, QStringList *notes, bool noKeys = false, QStringList *skippedKeys = nullptr);
std::vector<Change> removalPlan(const Environment &environment, QStringList *notes);

// A copy of the files one setup, remove or restore was about to change, in <backups>/<name>/.
struct BackupEntry {
    // The original path; `existed` is false for a file setup made, which a restore deletes again.
    QString path;
    bool existed = false;
    // The copy, relative to the backup's folder.
    QString stored;
};
struct Backup {
    QString name;
    QString folder;
    QString created;
    // "setup" or "remove".
    QString action;
    std::vector<BackupEntry> entries;
    // Folders that didn't exist yet, removed by a restore once empty.
    QStringList createdDirectories;
};
// A folder name for a backup made now, not yet taken: "20260928-101500", then "20260928-101500-02".
QString newBackupName(const Environment &environment);
// Copies each existing file of `paths` and writes the manifest last. Returns why it couldn't, having removed what it made; nothing else changes.
QString writeBackup(const Environment &environment, const QString &name, const QString &action, const QStringList &paths, Backup *made);
// Deletes the oldest backups beyond `keep`, never those in `except` (the one just made). Only folders named like a backup are touched.
void pruneBackups(const Environment &environment, const QStringList &except, int keep = 5);
// Every backup with a readable manifest, newest first.
std::vector<Backup> listBackups(const Environment &environment);
// `name` is a folder name, a unique start of one, or a path; empty is the newest.
std::optional<Backup> findBackup(const Environment &environment, const QString &name);
// What --restore would change: each file that differs from the backup's copy.
std::vector<Change> restorePlan(const Environment &environment, const Backup &backup, QString *error);
// The keys Hyprland has bound in its default submap that aren't Omastrator's ("Super+1", "Super+Return"); nullopt when it can't be asked.
// "Omastrator's" is one rule everywhere (see LiveBind in Setup+Keys.cpp): a bind saying so, or any bind on a combo one of those uses.
std::optional<QSet<QString>> liveUserBinds();
// What Hyprland has now compared with what was expected. Asks again for `waitMs` before calling a key lost or missing.
struct BindCheck {
    // False when Hyprland didn't answer: nothing else is known.
    bool answered = false;
    // Of `before`, the user's keys that are gone ("Super+1").
    QStringList lost;
    // Of `ownKeys` (normalised), Omastrator's keys that aren't bound ("Super+Alt+V").
    QStringList missing;
};
BindCheck checkBinds(const QSet<QString> &before, const QStringList &ownKeys, int waitMs = 2000);

// Whether `config` (the user's Hyprland config) already has the line that loads Omastrator's key file, in this version's form or an older one.
bool hasSourceLine(const QByteArray &config, HyprFormat format);
// The check around the writes of a setup that loads Omastrator's keys into Hyprland (docs/OS-SUITE.md, "Reload check").
struct KeyCheck {
    bool active = false;
    // The user's binds after a reload with nothing of ours changed; nullopt when that reload or the question failed.
    std::optional<QSet<QString>> before;
    // Omastrator's own global keys the key file should bind, normalised.
    QStringList ownKeys;
};
// Called before anything is written. Active only when the keys or the source step was accepted, our file will be sourced, and Hyprland answers.
// Reloads once, so runtime-only binds (an autostart script's `hyprctl keyword bind`) are already gone from the baseline.
KeyCheck startKeyCheck(const Environment &environment, bool keysAccepted, bool sourceAccepted, const QStringList &skippedKeys, QTextStream &out);
enum class KeyOutcome {
    // Reloaded, and every key the user had and every key of Omastrator's is there (or there was nothing to check).
    fine,
    // Files kept, but Hyprland isn't using them or a key is missing; `summary` says what to tell after "N steps applied".
    problem,
    // The user's keys were gone, so setup put every file back and said what happened.
    restored,
};
// Called after the writes: reloads, checks, and puts the backup back if the user's keys are gone. Never says a key is back without looking.
KeyOutcome finishKeyCheck(const KeyCheck &check, const Environment &environment, const QString &backupName, QTextStream &in, QTextStream &out, QTextStream &err,
                          QString *summary);
// Tells the shell to rescan its plugins and reload; adds a note when it isn't running.
void reloadOmarchyShell(QStringList *notes);
int runListBackups(const Environment &environment, QTextStream &out);
int runRestore(const Environment &environment, const QString &name, bool yes, bool dryRun, QTextStream &in, QTextStream &out, QTextStream &err);

// `omastrator setup [--yes] [--apply] [--no-keys] [--remove] [--restore [BACKUP]] [--list-backups] [--dry-run]`. Answers come from `in`.
int runCli(const QStringList &args, QTextStream &in, QTextStream &out, QTextStream &err);
}
