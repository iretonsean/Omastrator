#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <optional>
#include <vector>

// `omastrator setup` (docs/OS-SUITE.md): installs the island and tray light
// plugins, writes the Hyprland keys, the Omarchy menu entries and the
// dictation vocabulary, and offers the bar widget and the Hyprland source
// line. Every change to a file is shown as a diff and confirmed first;
// `--remove` takes out exactly what setup added, as recorded in setup.json.
namespace Setup {
struct Environment {
    QString home;
    QString configHome;
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
    // Chromium's per-user native messaging host manifest, and the flags file its launcher reads.
    QString browserHostManifest() const;
    QString chromiumFlags() const;
};

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

// The generated files.
QByteArray hyprlandLua(const QString &command, const DesignKeys &keys = {});
QByteArray hyprlandConf(const QString &command, const DesignKeys &keys = {});
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

// What setup would change now, and what --remove would.
std::vector<Change> installPlan(const Environment &environment, bool withBar, bool withSource, QStringList *notes);
std::vector<Change> removalPlan(const Environment &environment, QStringList *notes);

// `omastrator setup [--yes] [--apply] [--remove] [--dry-run]`. Answers come from `in`.
int runCli(const QStringList &args, QTextStream &in, QTextStream &out, QTextStream &err);
}
