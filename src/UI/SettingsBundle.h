#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <optional>
#include <vector>

// Settings that travel (Edit ▸ Export Settings… and Import Settings…): one JSON file with
// preferences, remapped keys, the workspace, swatches and size presets.
//
//   {"format": "omastrator-settings", "version": 1, "app": "0.1",
//    "settings": {"keyboardIncrement": {"t": "double", "v": 2}, ...},
//    "presets": {"documents": {...}, "frames": {...}}}
//
// Only keys on the allowlist in SettingsBundle.cpp are ever written or read, so nothing else
// in QSettings can leak into the file or be set by one. Left out on purpose: the Figma access
// token (figma.json), rclone's config and every cloud sign-in, the `cloud/` remembered
// remotes and folders, recent files and recent commands and colours, folders on this
// computer (design system project, Export for Screens), the device Send to a device last
// used, and anywhere.json, projects.json, setup.json and vocabulary.txt.
namespace SettingsBundle {
inline constexpr const char *formatName = "omastrator-settings";
inline constexpr int version = 1;

struct Bundle {
    // Allowlisted QSettings keys and their values.
    QMap<QString, QVariant> settings;
    // The documents and frames sections of presets.json; unset when it can't be read here.
    std::optional<QJsonObject> presets;
    // Keys an import leaves alone rather than resets: what the file held for them was refused.
    QStringList kept;
};

// One line of what an import will replace.
struct Change {
    QString category;
    QString label;
    // Empty `from`: not set here now. Empty `to`: goes back to its default.
    QString from;
    QString to;
};

struct Plan {
    Bundle incoming;
    std::vector<Change> changes;
    // What's left out or can't be applied, in plain sentences.
    QStringList notes;
    // presets.json is here but can't be read, so presets stay as they are.
    bool presetsBlocked = false;
    bool isEmpty() const { return changes.empty(); }
};

// What this computer would export now. Notes say what couldn't be included.
Bundle current(QStringList *notes = nullptr);
QByteArray toJson(const Bundle &bundle);
// Why the file can't be used, or empty. Keys off the allowlist are dropped from the result
// and counted in `ignored`; a shortcut set that doesn't hold together is dropped and noted.
Bundle parse(const QByteArray &json, QString *error, QStringList *notes = nullptr, int *ignored = nullptr);
// Whether `key` is one of the settings that travel.
bool travels(const QString &key);

// Writes the current settings to `path`. Returns why it failed, or empty.
QString exportTo(const QString &path, QStringList *notes = nullptr);
// Reads `path` and works out what importing it would change.
Plan planImport(const QString &path, QString *error);
Plan plan(const Bundle &incoming, int ignored = 0);
// Backs up what's here (a settings file you can import again, and a copy of presets.json),
// then replaces it. Returns why it failed, or empty; nothing is changed when the backup or
// the presets can't be written.
QString apply(const Plan &plan, QString *backupPath = nullptr);
// Where backups go: next to presets.json.
QString backupFolder();
}
