#include "UI/SettingsBundle.h"
#include "Document/EditorSession.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/PresetStore.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPoint>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 maximumFileBytes = 4 * 1024 * 1024;
const QStringList categoryOrder{QStringLiteral("Preferences"), QStringLiteral("Shortcuts"), QStringLiteral("Workspace"), QStringLiteral("Swatches"),
                                QStringLiteral("Presets")};

// Every QSettings key that travels. A key that isn't here is never exported and never imported.
struct Known {
    const char *key;
    bool prefix;
    const char *category;
    const char *label;
};
constexpr Known known[] = {
    {"keyboardIncrement", false, "Preferences", "Keyboard increment"},
    {"historyLimit", false, "Preferences", "History states"},
    {"layerNamingConvention", false, "Preferences", "Layer naming convention"},
    {"jpegExportQuality", false, "Preferences", "JPEG export quality"},
    {"agent/showTerminal", false, "Preferences", "Show the agent's terminal"},
    {"agent/timeoutSeconds", false, "Preferences", "Agent timeout (seconds)"},
    {"roast/heat", false, "Preferences", "Roast heat"},
    {"screenExport/scales", false, "Preferences", "Export for Screens scales"},
    {"screenExport/formats", false, "Preferences", "Export for Screens formats"},
    {"shareDeviceFormat", false, "Preferences", "Send to a device: format"},
    {"keyboardShortcuts.v1", false, "Shortcuts", "Remapped keys"},
    {"showsLayersPanel", false, "Workspace", "Layers panel shown"},
    {"showsPropertiesPanel", false, "Workspace", "Properties panel shown"},
    {"panelWidth", false, "Workspace", "Panel width"},
    {"panelSplitState", false, "Workspace", "Panel layout"},
    {"toolPreset", false, "Workspace", "Tool rail (Basic or Advanced)"},
    {"toolSlot/", true, "Workspace", "Tool on the rail"},
    {"referencePoint", false, "Workspace", "Transform reference point"},
    {"view/contextualTaskBar", false, "Workspace", "Contextual task bar"},
    {"view/pageWorkspaces", false, "Workspace", "Pages as Workspaces"},
    {"view/taskBarOffset", false, "Workspace", "Task bar position"},
    {"view/taskBarPinned", false, "Workspace", "Task bar pinned"},
    {"view/taskBarPinnedAt", false, "Workspace", "Task bar pinned position"},
    {"properties/constrainProportions", false, "Workspace", "Constrain proportions"},
    {"properties/scaleStrokes", false, "Workspace", "Scale strokes"},
    {"properties/scaleCorners", false, "Workspace", "Scale corners"},
    {"properties/framePresetGroup", false, "Workspace", "Frame presets group"},
    {"properties/characterShowsMore", false, "Workspace", "Character section: more options"},
    {"properties/collapsed/", true, "Workspace", "Properties section folded"},
    {"swatches", false, "Swatches", "Swatch library"},
};

const Known *lookup(const QString &key)
{
    for (const Known &entry : known) {
        const QString name = QLatin1String(entry.key);
        if (entry.prefix ? key.size() > name.size() && key.startsWith(name) : key == name)
            return &entry;
    }
    return nullptr;
}

QString labelFor(const QString &key)
{
    const Known *entry = lookup(key);
    if (!entry)
        return key;
    return entry->prefix ? QStringLiteral("%1: %2").arg(QLatin1String(entry->label), key.mid(int(qstrlen(entry->key)))) : QLatin1String(entry->label);
}

QString categoryFor(const QString &key)
{
    const Known *entry = lookup(key);
    return entry ? QLatin1String(entry->category) : QString();
}

// Test mode sends QSettings to ~/.qttest (or a temporary folder); anywhere else a test would
// read or rewrite the user's own settings.
void guardTestConfig()
{
    if (!QStandardPaths::isTestModeEnabled())
        return;
    const QString file = QDir::cleanPath(QSettings().fileName());
    if (!file.startsWith(QDir::cleanPath(QDir::tempPath()) + QLatin1Char('/')) && !file.contains(QLatin1String("/.qttest/")))
        qFatal("A test is using the real settings file for Settings that travel; enable QStandardPaths test mode before the application starts.");
}

QJsonObject tagged(const QString &type, const QJsonValue &value)
{
    return QJsonObject{{"t", type}, {"v", value}};
}

// Only the types QSettings gives back from its file; anything else is left out.
std::optional<QJsonObject> encode(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: return tagged(QStringLiteral("bool"), value.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: return tagged(QStringLiteral("int"), value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float: return tagged(QStringLiteral("double"), value.toDouble());
    case QMetaType::QString: return tagged(QStringLiteral("string"), value.toString());
    case QMetaType::QStringList: return tagged(QStringLiteral("strings"), QJsonArray::fromStringList(value.toStringList()));
    case QMetaType::QPoint: return tagged(QStringLiteral("point"), QJsonArray{value.toPoint().x(), value.toPoint().y()});
    case QMetaType::QByteArray: return tagged(QStringLiteral("bytes"), QString::fromLatin1(value.toByteArray().toBase64()));
    default: return std::nullopt;
    }
}

std::optional<QVariant> decode(const QJsonValue &entry)
{
    const QJsonObject object = entry.toObject();
    const QString type = object["t"].toString();
    const QJsonValue value = object["v"];
    if (type == QLatin1String("bool") && value.isBool())
        return value.toBool();
    if (type == QLatin1String("int") && value.isDouble()) {
        // Casting a huge or non-finite double to an integer is undefined, and doubles stop being exact past 2^53.
        constexpr double exactLimit = 9007199254740992.0;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < -exactLimit || number > exactLimit)
            return std::nullopt;
        return QVariant::fromValue<qlonglong>(qlonglong(number));
    }
    if (type == QLatin1String("double") && value.isDouble())
        return value.toDouble();
    if (type == QLatin1String("string") && value.isString())
        return value.toString();
    if (type == QLatin1String("strings") && value.isArray()) {
        QStringList list;
        for (const QJsonValue &item : value.toArray()) {
            if (!item.isString())
                return std::nullopt;
            list << item.toString();
        }
        return list;
    }
    if (type == QLatin1String("point") && value.isArray() && value.toArray().size() == 2)
        return QPoint(value.toArray()[0].toInt(), value.toArray()[1].toInt());
    if (type == QLatin1String("bytes") && value.isString()) {
        const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1());
        if (decoded)
            return *decoded;
    }
    return std::nullopt;
}

// Equal values give equal text, whichever way QSettings typed them.
QString canonical(const QVariant &value)
{
    if (value.typeId() == QMetaType::QByteArray)
        return QStringLiteral("bytes:") + QString::fromLatin1(value.toByteArray().toBase64());
    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().join(QLatin1Char('\n'));
    if (value.typeId() == QMetaType::QPoint)
        return QStringLiteral("%1,%2").arg(value.toPoint().x()).arg(value.toPoint().y());
    return value.toString();
}

QString shortList(const QStringList &names)
{
    constexpr int shown = 5;
    QStringList head = names.mid(0, shown);
    if (names.size() > shown)
        head << QStringLiteral("…");
    return head.join(QStringLiteral(", "));
}

// A short line for the confirm sheet; never empty.
QString summary(const QString &key, const QVariant &value)
{
    if (key == QLatin1String(ShortcutSettings::storageKey)) {
        const int count = int(ShortcutSettings::decode(value.toByteArray()).size());
        return count == 0 ? QStringLiteral("none remapped") : QStringLiteral("%1 remapped").arg(count);
    }
    if (key == QLatin1String("swatches")) {
        const QJsonArray groups = QJsonDocument::fromJson(value.toByteArray()).array();
        int colours = 0;
        for (const QJsonValue &group : groups)
            colours += group.toObject()["swatches"].toArray().size();
        return QStringLiteral("%1 group(s), %2 colour(s)").arg(groups.size()).arg(colours);
    }
    if (value.typeId() == QMetaType::QByteArray)
        return QStringLiteral("saved layout");
    if (value.typeId() == QMetaType::QPoint)
        return QStringLiteral("%1, %2").arg(value.toPoint().x()).arg(value.toPoint().y());
    if (value.typeId() == QMetaType::QStringList)
        return value.toStringList().isEmpty() ? QStringLiteral("(empty)") : value.toStringList().join(QStringLiteral(", "));
    const QString text = value.toString();
    return text.isEmpty() ? QStringLiteral("(empty)") : text;
}

QString presetSummary(const QJsonObject &section)
{
    QStringList names;
    for (const QJsonValue &entry : section["saved"].toArray())
        names << entry.toObject()["name"].toString();
    const int hidden = section["hidden"].toArray().size();
    QString text = names.isEmpty() ? QStringLiteral("none saved") : QStringLiteral("%1 saved (%2)").arg(names.size()).arg(shortList(names));
    if (hidden > 0)
        text += QStringLiteral(", %1 hidden").arg(hidden);
    return text;
}

QString presetLabel(const QString &section)
{
    return section == QLatin1String(PresetStore::documents) ? QStringLiteral("Document presets") : QStringLiteral("Frame presets");
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("Could not write %1: %2").arg(path, file.errorString());
    return {};
}
}

namespace SettingsBundle {
bool travels(const QString &key)
{
    return lookup(key) != nullptr;
}

Bundle current(QStringList *notes)
{
    guardTestConfig();
    Bundle bundle;
    QSettings settings;
    for (const QString &key : settings.allKeys()) {
        if (travels(key) && encode(settings.value(key)))
            bundle.settings.insert(key, settings.value(key));
    }
    bool valid = true;
    const QJsonObject presets = PresetStore::exportSections(&valid);
    if (valid)
        bundle.presets = presets;
    else if (notes)
        *notes << QStringLiteral("Size presets aren't included: %1 can't be read.").arg(PresetStore::path());
    return bundle;
}

QByteArray toJson(const Bundle &bundle)
{
    QJsonObject settings;
    for (auto entry = bundle.settings.constBegin(); entry != bundle.settings.constEnd(); ++entry) {
        if (const std::optional<QJsonObject> encoded = encode(entry.value()))
            settings.insert(entry.key(), *encoded);
    }
    QJsonObject root{{"format", QLatin1String(formatName)},
                     {"version", version},
                     {"app", QCoreApplication::applicationVersion()},
                     {"settings", settings}};
    if (bundle.presets)
        root.insert(QStringLiteral("presets"), *bundle.presets);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

Bundle parse(const QByteArray &json, QString *error, QStringList *notes, int *ignored)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return Bundle{};
    };
    QJsonParseError problem;
    const QJsonDocument document = QJsonDocument::fromJson(json, &problem);
    if (problem.error != QJsonParseError::NoError || !document.isObject() || document.object()["format"].toString() != QLatin1String(formatName))
        return fail(QStringLiteral("This isn't an Omastrator settings file."));
    const QJsonObject root = document.object();
    if (root["version"].toInt() > version)
        return fail(QStringLiteral("This file was made by a newer Omastrator. Update Omastrator to read it."));
    if (root["version"].toInt() < 1 || !root["settings"].isObject())
        return fail(QStringLiteral("This isn't an Omastrator settings file."));

    Bundle bundle;
    int skipped = 0, unreadable = 0;
    const QJsonObject settings = root["settings"].toObject();
    for (auto entry = settings.begin(); entry != settings.end(); ++entry) {
        if (!travels(entry.key())) {
            ++skipped;
            continue;
        }
        if (const std::optional<QVariant> value = decode(entry.value()))
            bundle.settings.insert(entry.key(), *value);
        else
            ++unreadable;
    }
    if (const auto shortcuts = bundle.settings.find(QLatin1String(ShortcutSettings::storageKey)); shortcuts != bundle.settings.end()) {
        if (const std::optional<QString> wrong = ShortcutSettings::problem(ShortcutSettings::decode(shortcuts->toByteArray()))) {
            if (notes)
                *notes << QStringLiteral("The remapped keys in this file don't hold together (%1), so your keys stay as they are.").arg(*wrong);
            bundle.settings.erase(shortcuts);
            bundle.kept << QLatin1String(ShortcutSettings::storageKey);
        }
    }
    if (root["presets"].isObject())
        bundle.presets = PresetStore::normalised(root["presets"].toObject());
    if (unreadable > 0 && notes)
        *notes << QStringLiteral("%1 setting(s) in the file couldn't be read and were skipped.").arg(unreadable);
    if (ignored)
        *ignored = skipped;
    return bundle;
}

QString exportTo(const QString &path, QStringList *notes)
{
    return writeFile(path, toJson(current(notes)));
}

Plan plan(const Bundle &incoming, int ignored)
{
    Plan result;
    result.incoming = incoming;
    QStringList notes;
    const Bundle now = current(&notes);
    result.notes = notes;
    if (ignored > 0)
        result.notes << QStringLiteral("%1 entr%2 in the file aren't settings that travel between computers and are ignored.")
                            .arg(ignored).arg(ignored == 1 ? QStringLiteral("y") : QStringLiteral("ies"));

    QSet<QString> keys;
    for (auto key = now.settings.constBegin(); key != now.settings.constEnd(); ++key)
        keys.insert(key.key());
    for (auto key = incoming.settings.constBegin(); key != incoming.settings.constEnd(); ++key)
        keys.insert(key.key());
    for (const QString &key : keys) {
        if (incoming.kept.contains(key))
            continue;
        const bool here = now.settings.contains(key), there = incoming.settings.contains(key);
        if (here && there && canonical(now.settings[key]) == canonical(incoming.settings[key]))
            continue;
        result.changes.push_back({categoryFor(key), labelFor(key), here ? summary(key, now.settings[key]) : QString(),
                                  there ? summary(key, incoming.settings[key]) : QString()});
    }

    if (incoming.presets) {
        if (!now.presets) {
            result.presetsBlocked = true;
            result.notes << QStringLiteral("Size presets stay as they are: %1 can't be read, and Omastrator won't replace a file it can't read.")
                                .arg(PresetStore::path());
        } else {
            for (const char *name : {PresetStore::documents, PresetStore::frames}) {
                const QString section = QLatin1String(name);
                if (!incoming.presets->contains(section) || (*now.presets)[section] == (*incoming.presets)[section])
                    continue;
                result.changes.push_back({QStringLiteral("Presets"), presetLabel(section), presetSummary((*now.presets)[section].toObject()),
                                          presetSummary((*incoming.presets)[section].toObject())});
            }
        }
    }
    std::stable_sort(result.changes.begin(), result.changes.end(), [](const Change &a, const Change &b) {
        const auto rank = [](const Change &change) { return categoryOrder.indexOf(change.category); };
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        return a.label < b.label;
    });
    return result;
}

Plan planImport(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Couldn't open %1: %2").arg(path, file.errorString());
        return {};
    }
    if (file.size() > maximumFileBytes) {
        if (error)
            *error = QStringLiteral("This file is too big to be a settings file.");
        return {};
    }
    QString why;
    QStringList notes;
    int ignored = 0;
    const Bundle incoming = parse(file.readAll(), &why, &notes, &ignored);
    if (!why.isEmpty()) {
        if (error)
            *error = why;
        return {};
    }
    Plan result = plan(incoming, ignored);
    result.notes = notes + result.notes;
    return result;
}

QString backupFolder()
{
    return QFileInfo(PresetStore::path()).absolutePath() + QStringLiteral("/backups");
}

namespace {
constexpr int backupsKept = 10;

// Qt writes the settings file by replacing it in its folder, so both must be writable.
bool settingsFileWritable(const QString &fileName)
{
    const QFileInfo file(fileName);
    if (file.exists() && !file.isWritable())
        return false;
    QDir folder = file.absoluteDir();
    while (!folder.exists() && folder.cdUp()) {
    }
    return QFileInfo(folder.absolutePath()).isWritable();
}

// Keeps the newest few backups of one kind ("settings" or "presets"), and never removes `keep`.
void pruneBackups(const QString &folder, const QString &kind, const QString &keep)
{
    static const QRegularExpression pattern(QStringLiteral("^(settings|presets)-(\\d{8}-\\d{6})(?:-(\\d+))?\\.json$"));
    struct Entry {
        QString stamp;
        int number;
        QString name;
    };
    std::vector<Entry> entries;
    for (const QString &name : QDir(folder).entryList(QDir::Files)) {
        const QRegularExpressionMatch match = pattern.match(name);
        if (match.hasMatch() && match.captured(1) == kind)
            entries.push_back({match.captured(2), match.captured(3).isEmpty() ? 1 : match.captured(3).toInt(), name});
    }
    // Names sort wrongly as text ("-2.json" before ".json"), so order by the stamp and then the number.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.stamp != b.stamp ? a.stamp > b.stamp : a.number > b.number;
    });
    for (size_t index = backupsKept; index < entries.size(); ++index) {
        if (entries[index].name != QFileInfo(keep).fileName())
            QFile::remove(QDir(folder).filePath(entries[index].name));
    }
}

// Reads the file as it is on disk: QSettings shares one cache per path, so a failed write still reads back as saved.
QString verifyOnDisk(const QString &fileName, const Bundle &before, const Plan &plan)
{
    const QString failure = QStringLiteral("Could not write %1.").arg(fileName);
    QTemporaryDir copyDir;
    const QString copy = copyDir.filePath(QStringLiteral("settings.conf"));
    if (!copyDir.isValid() || !QFile::copy(fileName, copy))
        return failure;
    const QSettings onDisk(copy, QSettings::IniFormat);
    for (auto key = plan.incoming.settings.constBegin(); key != plan.incoming.settings.constEnd(); ++key) {
        if (travels(key.key()) && canonical(onDisk.value(key.key())) != canonical(key.value()))
            return failure;
    }
    for (auto key = before.settings.constBegin(); key != before.settings.constEnd(); ++key) {
        if (!plan.incoming.settings.contains(key.key()) && !plan.incoming.kept.contains(key.key()) && onDisk.contains(key.key()))
            return failure;
    }
    return {};
}
}

QString apply(const Plan &plan, QString *backupPath)
{
    guardTestConfig();
    const Bundle now = current();

    if (!settingsFileWritable(QSettings().fileName()))
        return QStringLiteral("Could not write %1, so nothing was changed.").arg(QSettings().fileName());

    // Nothing changes until the backup is safe.
    const QString folder = backupFolder();
    if (!QDir().mkpath(folder))
        return QStringLiteral("Could not create %1.").arg(folder);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name = QStringLiteral("settings-%1").arg(stamp);
    for (int number = 2; QFileInfo::exists(QDir(folder).filePath(name + QStringLiteral(".json"))); ++number)
        name = QStringLiteral("settings-%1-%2").arg(stamp).arg(number);
    const QString backup = QDir(folder).filePath(name + QStringLiteral(".json"));
    if (const QString failure = writeFile(backup, toJson(now)); !failure.isEmpty())
        return failure;
    if (QFileInfo::exists(PresetStore::path()) && !plan.presetsBlocked) {
        const QString presetsCopy = QDir(folder).filePath(QStringLiteral("presets-%1.json").arg(name.mid(int(qstrlen("settings-")))));
        if (!QFile::copy(PresetStore::path(), presetsCopy))
            return QStringLiteral("Could not back up %1, so nothing was changed.").arg(PresetStore::path());
        pruneBackups(folder, QStringLiteral("presets"), presetsCopy);
    }
    pruneBackups(folder, QStringLiteral("settings"), backup);
    if (backupPath)
        *backupPath = backup;

    // Presets first: if they can't be written, the settings haven't been touched.
    if (plan.incoming.presets && !plan.presetsBlocked) {
        if (const QString failure = PresetStore::replaceSections(*plan.incoming.presets); !failure.isEmpty())
            return failure;
    }

    {
        QSettings settings;
        for (auto key = now.settings.constBegin(); key != now.settings.constEnd(); ++key) {
            if (!plan.incoming.settings.contains(key.key()) && !plan.incoming.kept.contains(key.key()))
                settings.remove(key.key());
        }
        for (auto key = plan.incoming.settings.constBegin(); key != plan.incoming.settings.constEnd(); ++key) {
            if (travels(key.key()))
                settings.setValue(key.key(), key.value());
        }
        settings.sync();
    }
    // sync()'s status can report an error for a file that was written, so check the file itself.
    if (const QString failure = verifyOnDisk(QSettings().fileName(), now, plan); !failure.isEmpty())
        return failure;

    ShortcutSettings::shared().reload();
    EditorSession::reloadHistoryLimit();
    return {};
}
}
