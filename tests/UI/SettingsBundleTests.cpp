#include "../Cloud/FakeCloud.h"
#include "WidgetCleanup.h"
#include "TemporaryConfig.h"
#include "UI/CloudBrowser.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/PresetStore.h"
#include "UI/CommandPalette.h"
#include "UI/Menus.h"
#include "UI/ProjectWorkspace.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SettingsBundle.h"
#include "UI/SettingsTransfer.h"
#include <QApplication>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QtTest>

// Export Settings and Import Settings: what travels, what never does, and the one confirm.
namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// Every travelling setting as plain text, whichever type QSettings gave it.
QMap<QString, QString> flat(const SettingsBundle::Bundle &bundle)
{
    QMap<QString, QString> out;
    for (auto entry = bundle.settings.constBegin(); entry != bundle.settings.constEnd(); ++entry) {
        const QVariant &value = entry.value();
        if (value.typeId() == QMetaType::QByteArray)
            out.insert(entry.key(), QString::fromLatin1(value.toByteArray().toBase64()));
        else if (value.typeId() == QMetaType::QStringList)
            out.insert(entry.key(), value.toStringList().join(QLatin1Char('|')));
        else if (value.typeId() == QMetaType::QPoint)
            out.insert(entry.key(), QStringLiteral("%1,%2").arg(value.toPoint().x()).arg(value.toPoint().y()));
        else
            out.insert(entry.key(), value.toString());
    }
    return out;
}

QByteArray chords(const QString &firstID, const QString &secondID, const QString &firstKey, const QString &secondKey)
{
    QJsonObject object;
    object.insert(firstID, QJsonObject{{"key", firstKey}, {"modifiers", 0}});
    if (!secondID.isEmpty())
        object.insert(secondID, QJsonObject{{"key", secondKey}, {"modifiers", 0}});
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray swatches()
{
    return QJsonDocument(QJsonArray{QJsonObject{{"name", "Brand"},
                                                {"swatches", QJsonArray{QJsonObject{{"name", "Ink"}, {"hex", "#112233"}},
                                                                        QJsonObject{{"name", "Paper"}, {"hex", "#f4efe6"}}}}}})
        .toJson(QJsonDocument::Compact);
}

template <typename T = QWidget>
T *shown(const QString &name)
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == name && widget->isVisible())
            return qobject_cast<T *>(widget);
    }
    return nullptr;
}
}

class SettingsBundleTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void roundTripGivesTheSameKeysWorkspaceAndPresets();
    void nothingSecretOrLocalIsExported();
    void keysOffTheAllowlistAreIgnored();
    void theConfirmListsWhatWillBeReplacedAndCancelChangesNothing();
    void aBackupIsKeptAndCanBeImportedBack();
    void unreadablePresetsAreNeverOverwritten();
    void remappedKeysThatClashAreKept();
    void anUnwritableSettingsFileStopsTheImportBeforeAnythingChanges();
    void hugeOrNonFiniteIntegersAreUnreadable();
    void onlyTheNewestTenBackupsOfEachKindAreKept();
    void newerAndForeignFilesAreRefused();
    void identicalSettingsOnlyShowANotice();
    void cloudExportAndImportGoThroughRclone();
    void theEditMenuAndCommandPaletteOfferBoth();

private:
    void fillSettings();
    void fillPresets();
    void forgetEverything();
    SettingsBundle::Bundle now() { return SettingsBundle::current(); }
    QString m_dir;
    QString m_shortcutA, m_shortcutB;
    QTemporaryDir m_files;
};

void SettingsBundleTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
    // QSettings lives in this run's own temporary config, apart from the real one.
    QVERIFY(QSettings().fileName().startsWith(qEnvironmentVariable("XDG_CONFIG_HOME") + QLatin1Char('/')));
    QVERIFY(ShortcutDefinition::all().size() > 2);
    m_shortcutA = ShortcutDefinition::all().at(0).id();
    m_shortcutB = ShortcutDefinition::all().at(1).id();
}

void SettingsBundleTests::forgetEverything()
{
    QSettings().clear();
    QSettings().sync();
    QFile::remove(PresetStore::path());
    QDir(SettingsBundle::backupFolder()).removeRecursively();
}

void SettingsBundleTests::init()
{
    useTemporaryConfig();
    // A workspace looks for rclone; without the fake it must find nothing, never the real config.
    qputenv("OMASTRATOR_RCLONE", (m_files.path() + QStringLiteral("/no-rclone")).toUtf8());
    forgetEverything();
}

void SettingsBundleTests::cleanup()
{
    // Only this run's temporary file: a test that made it read-only must not leave it so.
    const QString settingsFile = QSettings().fileName();
    if (settingsFile.startsWith(qEnvironmentVariable("XDG_CONFIG_HOME") + QLatin1Char('/')) && QFileInfo::exists(settingsFile))
        QFile::setPermissions(settingsFile, QFile::ReadOwner | QFile::WriteOwner);
    SettingsConfirmDialog::setResponder({});
    deleteTopLevelWidgets([](QWidget *widget) { return qobject_cast<QDialog *>(widget) != nullptr; });
}

void SettingsBundleTests::fillSettings()
{
    QSettings settings;
    settings.setValue("keyboardIncrement", 2.5);
    settings.setValue("historyLimit", 250);
    settings.setValue("layerNamingConvention", "Kebab");
    settings.setValue("jpegExportQuality", 72);
    settings.setValue("agent/showTerminal", true);
    settings.setValue("screenExport/scales", QStringList{"1", "2"});
    settings.setValue("keyboardShortcuts.v1", chords(m_shortcutA, m_shortcutB, QStringLiteral("§"), QStringLiteral("±")));
    settings.setValue("showsLayersPanel", false);
    settings.setValue("panelWidth", 310);
    settings.setValue("panelSplitState", QByteArray("\x01\x02\xffsplit", 8));
    settings.setValue("toolPreset", "Advanced");
    settings.setValue("toolSlot/selection", "direct");
    settings.setValue("view/taskBarOffset", QPoint(12, -40));
    settings.setValue("properties/collapsed/Appearance", true);
    settings.setValue("swatches", swatches());
    settings.sync();
}

void SettingsBundleTests::fillPresets()
{
    PresetStore::Section documents;
    documents.saved.push_back({QStringLiteral("Poster"), QSizeF(1417.32, 1984.25), LengthUnit::mm});
    documents.hidden = {QStringLiteral("A3")};
    QVERIFY(PresetStore::write(PresetStore::documents, documents).isEmpty());
    PresetStore::Section frames;
    frames.saved.push_back({QStringLiteral("Watch"), QSizeF(396, 484), LengthUnit::px});
    QVERIFY(PresetStore::write(PresetStore::frames, frames).isEmpty());
}

void SettingsBundleTests::roundTripGivesTheSameKeysWorkspaceAndPresets()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("round.json"));
    QStringList notes;
    QVERIFY(SettingsBundle::exportTo(file, &notes).isEmpty());
    QVERIFY(notes.isEmpty());
    const auto before = flat(now());
    const QJsonObject presetsBefore = *now().presets;
    QVERIFY(before.contains("keyboardShortcuts.v1"));
    QVERIFY(before.contains("panelSplitState"));
    QCOMPARE(before.value("view/taskBarOffset"), QString("12,-40"));
    QVERIFY(presetsBefore["documents"].toObject()["saved"].toArray().size() == 1);

    // Another computer: nothing set, no presets.
    forgetEverything();
    QVERIFY(flat(now()).isEmpty());

    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(file, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!plan.isEmpty());
    QVERIFY(plan.notes.isEmpty());
    QVERIFY(SettingsBundle::apply(plan).isEmpty());

    QCOMPARE(flat(now()), before);
    QCOMPARE(*now().presets, presetsBefore);
    // The reader of the keys sees them at once, and so does the editor's history depth.
    QCOMPARE(EditorSession::historyLimit(), 250);
    const ShortcutChord chord = ShortcutSettings::shared().chord(ShortcutDefinition::all().at(0));
    QCOMPARE(chord.key, QString("§"));
    // Presets come back through the store, in the order they were saved.
    const PresetStore::Section documents = PresetStore::read(PresetStore::documents);
    QCOMPARE(documents.saved.size(), size_t(1));
    QCOMPARE(documents.saved.front().name, QString("Poster"));
    QCOMPARE(documents.hidden, QStringList{"A3"});

    // A second export from the second computer reads the same as the first.
    const QString again = m_files.filePath(QStringLiteral("again.json"));
    QVERIFY(SettingsBundle::exportTo(again).isEmpty());
    QCOMPARE(SettingsBundle::parse(readFile(again), nullptr).presets.value(), presetsBefore);
    QCOMPARE(flat(SettingsBundle::parse(readFile(again), nullptr)), before);
}

void SettingsBundleTests::nothingSecretOrLocalIsExported()
{
    fillSettings();
    fillPresets();
    const QString secret = QStringLiteral("SECRET-TOKEN-c0ffee");
    QSettings settings;
    for (const char *key : {"cloud/lastRemote", "cloud/lastFolder", "cloud/remoteTypes", "recentFiles", "commandPalette/recent", "colors/recent",
                            "designSystem/projectFolder", "screenExport/folder", "shareDeviceAddress", "shareDeviceName", "figmaToken",
                            "cloud/token", "somethingNew"})
        settings.setValue(QLatin1String(key), secret);
    settings.sync();
    // The Figma token and rclone's config live in files of their own, which must stay unread.
    writeFile(qEnvironmentVariable("XDG_CONFIG_HOME") + QStringLiteral("/omastrator/figma.json"), "{\"token\": \"" + secret.toUtf8() + "\"}");
    writeFile(qEnvironmentVariable("XDG_CONFIG_HOME") + QStringLiteral("/rclone/rclone.conf"), "[work]\ntoken = " + secret.toUtf8() + "\n");

    const QString file = m_files.filePath(QStringLiteral("clean.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());
    const QByteArray bytes = readFile(file);
    QVERIFY(!bytes.isEmpty());
    QVERIFY(!bytes.contains(secret.toUtf8()));
    QVERIFY(!bytes.contains("/home/"));
    QVERIFY(!bytes.contains(qPrintable(QDir::homePath())));
    const QJsonObject exported = QJsonDocument::fromJson(bytes).object()["settings"].toObject();
    for (const QString &key : exported.keys())
        QVERIFY2(SettingsBundle::travels(key), qPrintable(key));
    for (const char *key : {"cloud/lastRemote", "recentFiles", "commandPalette/recent", "colors/recent", "designSystem/projectFolder",
                            "screenExport/folder", "shareDeviceAddress", "shareDeviceName", "somethingNew"})
        QVERIFY2(!exported.contains(QLatin1String(key)), key);
    QVERIFY(exported.contains("keyboardIncrement"));
    QVERIFY(exported.contains("swatches"));
}

void SettingsBundleTests::keysOffTheAllowlistAreIgnored()
{
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().sync();
    const QJsonObject file{{"format", "omastrator-settings"},
                           {"version", 1},
                           {"settings", QJsonObject{{"keyboardIncrement", QJsonObject{{"t", "double"}, {"v", 4}}},
                                                    {"cloud/lastRemote", QJsonObject{{"t", "string"}, {"v", "elsewhere:"}}},
                                                    {"recentFiles", QJsonObject{{"t", "strings"}, {"v", QJsonArray{"/etc/passwd"}}}},
                                                    {"designSystem/projectFolder", QJsonObject{{"t", "string"}, {"v", "/tmp/x"}}}}}};
    const QString path = m_files.filePath(QStringLiteral("hostile.json"));
    writeFile(path, QJsonDocument(file).toJson());
    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(path, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(plan.incoming.settings.size(), 1);
    QVERIFY(plan.notes.join(QLatin1Char(' ')).contains(QLatin1String("3 entries")));
    QVERIFY(SettingsBundle::apply(plan).isEmpty());
    QSettings settings;
    QCOMPARE(settings.value("keyboardIncrement").toDouble(), 4.0);
    QVERIFY(!settings.contains("cloud/lastRemote"));
    QVERIFY(!settings.contains("recentFiles"));
    QVERIFY(!settings.contains("designSystem/projectFolder"));
}

void SettingsBundleTests::theConfirmListsWhatWillBeReplacedAndCancelChangesNothing()
{
    // What the file holds.
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("incoming.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    // What this computer has.
    forgetEverything();
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().setValue("panelWidth", 280);
    QSettings().setValue("toolPreset", "Basic");
    QSettings().sync();
    PresetStore::Section mine;
    mine.saved.push_back({QStringLiteral("Mine"), QSizeF(100, 100), LengthUnit::px});
    QVERIFY(PresetStore::write(PresetStore::documents, mine).isEmpty());
    const auto settingsBefore = flat(now());
    const QByteArray presetsBefore = readFile(PresetStore::path());

    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(file, &error);
    QVERIFY(error.isEmpty());
    QStringList labels;
    for (const SettingsBundle::Change &change : plan.changes) {
        labels << change.label;
        QVERIFY(!change.to.isEmpty() || !change.from.isEmpty());
    }
    QVERIFY(labels.contains("Keyboard increment"));
    QVERIFY(labels.contains("Remapped keys"));
    QVERIFY(labels.contains("Document presets"));
    QVERIFY(labels.contains("Frame presets"));
    QVERIFY(labels.contains("Panel width"));

    QString shownText;
    int rows = 0;
    bool cancelled = true;
    SettingsConfirmDialog::setResponder([&](SettingsConfirmDialog &dialog) {
        shownText = dialog.text();
        rows = dialog.changes()->topLevelItemCount();
        auto *confirm = dialog.findChild<QPushButton *>(QStringLiteral("settingsConfirm"));
        cancelled = !confirm || confirm->text() != QLatin1String("Replace Settings");
        return false;
    });
    bool confirmed = true;
    QString backup;
    QVERIFY(SettingsConfirmDialog::run(plan, nullptr, &confirmed, &backup).isEmpty());
    QVERIFY(!confirmed);
    QVERIFY(!cancelled);
    QVERIFY(rows > 0);
    // It says what's here now, what it becomes and where the backup goes.
    QVERIFY2(shownText.contains(QLatin1String("Keyboard increment")), qPrintable(shownText));
    QVERIFY(shownText.contains(QLatin1String("1")));
    QVERIFY(shownText.contains(QLatin1String("2.5")));
    QVERIFY(shownText.contains(QLatin1String("Mine")));
    QVERIFY(shownText.contains(QLatin1String("Poster")));
    QVERIFY(shownText.contains(QLatin1String("backup")));
    // Cancel: nothing changed, and no backup was made.
    QCOMPARE(flat(now()), settingsBefore);
    QCOMPARE(readFile(PresetStore::path()), presetsBefore);
    QVERIFY(!QDir(SettingsBundle::backupFolder()).exists());

    SettingsConfirmDialog::setResponder([](SettingsConfirmDialog &) { return true; });
    QVERIFY(SettingsConfirmDialog::run(plan, nullptr, &confirmed, &backup).isEmpty());
    QVERIFY(confirmed);
    QCOMPARE(QSettings().value("keyboardIncrement").toDouble(), 2.5);
    QCOMPARE(PresetStore::read(PresetStore::documents).saved.front().name, QString("Poster"));
    QVERIFY(QFileInfo::exists(backup));
}

void SettingsBundleTests::aBackupIsKeptAndCanBeImportedBack()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("other.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    forgetEverything();
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().setValue("agent/timeoutSeconds", 90);
    QSettings().sync();
    PresetStore::Section mine;
    mine.saved.push_back({QStringLiteral("Mine"), QSizeF(100, 100), LengthUnit::px});
    QVERIFY(PresetStore::write(PresetStore::documents, mine).isEmpty());
    const auto before = flat(now());
    const QJsonObject presetsBefore = *now().presets;

    QString error, backup;
    QVERIFY(SettingsBundle::apply(SettingsBundle::planImport(file, &error), &backup).isEmpty());
    QVERIFY(error.isEmpty());
    QVERIFY(backup.startsWith(SettingsBundle::backupFolder()));
    QVERIFY(QFileInfo::exists(backup));
    // A setting the file doesn't have goes back to its default.
    QVERIFY(!QSettings().contains("agent/timeoutSeconds"));
    QVERIFY(flat(now()) != before);
    // presets.json is copied beside it, byte for byte.
    const QStringList copies = QDir(SettingsBundle::backupFolder()).entryList({QStringLiteral("presets-*.json")});
    QCOMPARE(copies.size(), 1);
    QCOMPARE(QJsonDocument::fromJson(readFile(SettingsBundle::backupFolder() + QLatin1Char('/') + copies.first())).object()["documents"].toObject(),
             presetsBefore["documents"].toObject());

    // The backup is a settings file: importing it undoes the import.
    QVERIFY(SettingsBundle::apply(SettingsBundle::planImport(backup, &error)).isEmpty());
    QVERIFY(error.isEmpty());
    QCOMPARE(flat(now()), before);
    QCOMPARE(*now().presets, presetsBefore);

    // Two backups in one second never share a name.
    const QString firstAgain = backup;
    QString second;
    QVERIFY(SettingsBundle::apply(SettingsBundle::planImport(file, &error), &second).isEmpty());
    QVERIFY(second != firstAgain);
}

void SettingsBundleTests::unreadablePresetsAreNeverOverwritten()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("withpresets.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    forgetEverything();
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().sync();
    const QByteArray damaged = "{ this was written by hand and isn't JSON";
    writeFile(PresetStore::path(), damaged);

    // Export still works; presets are left out, and it says so.
    QStringList exportNotes;
    const QString partial = m_files.filePath(QStringLiteral("partial.json"));
    QVERIFY(SettingsBundle::exportTo(partial, &exportNotes).isEmpty());
    QCOMPARE(exportNotes.size(), 1);
    QVERIFY(!QJsonDocument::fromJson(readFile(partial)).object().contains("presets"));

    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(file, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(plan.presetsBlocked);
    QVERIFY(plan.notes.join(QLatin1Char(' ')).contains(QLatin1String("can't be read")));
    for (const SettingsBundle::Change &change : plan.changes)
        QVERIFY(change.category != QLatin1String("Presets"));
    QVERIFY(SettingsBundle::apply(plan).isEmpty());
    QCOMPARE(readFile(PresetStore::path()), damaged);
    QCOMPARE(QSettings().value("keyboardIncrement").toDouble(), 2.5);
    QVERIFY(QDir(SettingsBundle::backupFolder()).entryList({QStringLiteral("presets-*.json")}).isEmpty());

    // The store itself refuses, too.
    QVERIFY(!PresetStore::replaceSections(*SettingsBundle::parse(readFile(file), nullptr).presets).isEmpty());
    QCOMPARE(readFile(PresetStore::path()), damaged);
}

void SettingsBundleTests::remappedKeysThatClashAreKept()
{
    QSettings().setValue("keyboardShortcuts.v1", chords(m_shortcutA, QString(), QStringLiteral("§"), QString()));
    QSettings().sync();
    const QByteArray mine = QSettings().value("keyboardShortcuts.v1").toByteArray();
    // Two commands on one key don't hold together.
    const QByteArray clash = chords(m_shortcutA, m_shortcutB, QStringLiteral("§"), QStringLiteral("§"));
    const QJsonObject file{{"format", "omastrator-settings"},
                           {"version", 1},
                           {"settings", QJsonObject{{"keyboardShortcuts.v1", QJsonObject{{"t", "bytes"}, {"v", QString::fromLatin1(clash.toBase64())}}},
                                                    {"historyLimit", QJsonObject{{"t", "int"}, {"v", 30}}}}}};
    const QString path = m_files.filePath(QStringLiteral("clash.json"));
    writeFile(path, QJsonDocument(file).toJson());
    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(path, &error);
    QVERIFY(error.isEmpty());
    QVERIFY(plan.notes.join(QLatin1Char(' ')).contains(QLatin1String("your keys stay as they are")));
    for (const SettingsBundle::Change &change : plan.changes)
        QVERIFY(change.label != QLatin1String("Remapped keys"));
    QVERIFY(SettingsBundle::apply(plan).isEmpty());
    QCOMPARE(QSettings().value("keyboardShortcuts.v1").toByteArray(), mine);
    QCOMPARE(QSettings().value("historyLimit").toInt(), 30);
}

void SettingsBundleTests::anUnwritableSettingsFileStopsTheImportBeforeAnythingChanges()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("unwritable.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    forgetEverything();
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().sync();
    PresetStore::Section mine;
    mine.saved.push_back({QStringLiteral("Mine"), QSizeF(100, 100), LengthUnit::px});
    QVERIFY(PresetStore::write(PresetStore::documents, mine).isEmpty());
    const QByteArray presetsBefore = readFile(PresetStore::path());
    QVERIFY(!presetsBefore.isEmpty());

    QString error;
    const SettingsBundle::Plan plan = SettingsBundle::planImport(file, &error);
    QVERIFY(error.isEmpty());

    // Only this run's temporary file, which the guard above has already shown is apart from the real config.
    const QString settingsFile = QSettings().fileName();
    QVERIFY(settingsFile.startsWith(qEnvironmentVariable("XDG_CONFIG_HOME") + QLatin1Char('/')));
    const QByteArray settingsBefore = readFile(settingsFile);
    QVERIFY(QFile::setPermissions(settingsFile, QFile::ReadOwner));
    if (QFileInfo(settingsFile).isWritable())
        QSKIP("Files stay writable here (running as root), so nothing can be made unwritable.");

    QString backup;
    const QString failure = SettingsBundle::apply(plan, &backup);
    QVERIFY(!failure.isEmpty());
    QVERIFY(failure.contains(QLatin1String("nothing was changed")));
    QVERIFY(backup.isEmpty());
    QCOMPARE(readFile(PresetStore::path()), presetsBefore);
    QCOMPARE(readFile(settingsFile), settingsBefore);
    QVERIFY(!QDir(SettingsBundle::backupFolder()).exists());

    // Writable again, the same plan goes through.
    QVERIFY(QFile::setPermissions(settingsFile, QFile::ReadOwner | QFile::WriteOwner));
    QVERIFY(SettingsBundle::apply(plan).isEmpty());
    QCOMPARE(QSettings().value("keyboardIncrement").toDouble(), 2.5);
}

void SettingsBundleTests::hugeOrNonFiniteIntegersAreUnreadable()
{
    const auto fileWith = [](const QString &literal) {
        return QByteArray("{\"format\":\"omastrator-settings\",\"version\":1,\"settings\":{\"historyLimit\":{\"t\":\"int\",\"v\":") + literal.toUtf8()
            + "}}}";
    };
    for (const QString &literal : {QStringLiteral("1e300"), QStringLiteral("-1e300"), QStringLiteral("9.1e15")}) {
        QString error;
        const SettingsBundle::Bundle bundle = SettingsBundle::parse(fileWith(literal), &error);
        QVERIFY2(!bundle.settings.contains("historyLimit"), qPrintable(literal));
    }
    const SettingsBundle::Bundle fine = SettingsBundle::parse(fileWith(QStringLiteral("9007199254740992")), nullptr);
    QCOMPARE(fine.settings.value("historyLimit").toLongLong(), 9007199254740992LL);
    QCOMPARE(SettingsBundle::parse(fileWith(QStringLiteral("-250")), nullptr).settings.value("historyLimit").toLongLong(), -250LL);
}

void SettingsBundleTests::onlyTheNewestTenBackupsOfEachKindAreKept()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("many.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    // Eight old settings backups and fifteen presets ones, files that aren't ours, and a same-second pair
    // that is the oldest of all: "-2" is later than the plain name, although it sorts before it as text.
    const QString folder = SettingsBundle::backupFolder();
    for (int day = 1; day <= 15; ++day) {
        const QString stamp = QStringLiteral("202001%1-120000").arg(day, 2, 10, QLatin1Char('0'));
        writeFile(QStringLiteral("%1/presets-%2.json").arg(folder, stamp), "{}");
        if (day <= 8)
            writeFile(QStringLiteral("%1/settings-%2.json").arg(folder, stamp), "{}");
    }
    writeFile(folder + QStringLiteral("/notes.txt"), "mine");
    writeFile(folder + QStringLiteral("/settings-hand-made.json"), "{}");
    writeFile(folder + QStringLiteral("/settings-20190101-120000.json"), "{}");
    writeFile(folder + QStringLiteral("/settings-20190101-120000-2.json"), "{}");

    QString backup;
    QVERIFY(SettingsBundle::apply(SettingsBundle::planImport(file, nullptr), &backup).isEmpty());

    const QStringList settings = QDir(folder).entryList({QStringLiteral("settings-2*.json")}, QDir::Files, QDir::Name);
    const QStringList presets = QDir(folder).entryList({QStringLiteral("presets-2*.json")}, QDir::Files, QDir::Name);
    // Eleven settings backups before the cap: only the oldest one goes.
    QCOMPARE(settings.size(), 10);
    QVERIFY(settings.contains(QStringLiteral("settings-20190101-120000-2.json")));
    QVERIFY(!settings.contains(QStringLiteral("settings-20190101-120000.json")));
    QVERIFY(settings.contains(QStringLiteral("settings-20200101-120000.json")));
    QVERIFY(QFileInfo::exists(backup));
    QVERIFY(settings.contains(QFileInfo(backup).fileName()));
    // Sixteen presets backups, if there was one to copy: the new one and the nine newest old ones stay.
    QVERIFY(QFileInfo::exists(folder + QStringLiteral("/presets-") + QFileInfo(backup).fileName().mid(int(qstrlen("settings-")))));
    QCOMPARE(presets.size(), 10);
    QVERIFY(!presets.contains(QStringLiteral("presets-20200106-120000.json")));
    QVERIFY(presets.contains(QStringLiteral("presets-20200107-120000.json")));
    QVERIFY(presets.contains(QStringLiteral("presets-20200115-120000.json")));
    // Files that aren't backups this app wrote are never touched.
    QVERIFY(QFileInfo::exists(folder + QStringLiteral("/notes.txt")));
    QVERIFY(QFileInfo::exists(folder + QStringLiteral("/settings-hand-made.json")));

    // A clock that has gone backwards makes the new backup the oldest; it still stays.
    forgetEverything();
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());
    for (int number = 1; number <= 12; ++number)
        writeFile(QStringLiteral("%1/settings-2999%2-120000.json").arg(folder).arg(1000 + number), "{}");
    QVERIFY(SettingsBundle::apply(SettingsBundle::planImport(file, nullptr), &backup).isEmpty());
    QVERIFY(QFileInfo::exists(backup));
    QCOMPARE(QDir(folder).entryList({QStringLiteral("settings-*.json")}, QDir::Files).size(), 11);
}

void SettingsBundleTests::newerAndForeignFilesAreRefused()
{
    QSettings().setValue("keyboardIncrement", 1.0);
    QSettings().sync();
    const auto before = flat(now());
    const QString path = m_files.filePath(QStringLiteral("odd.json"));
    const auto refused = [&](const QByteArray &bytes, const QString &mention) {
        writeFile(path, bytes);
        QString error;
        const SettingsBundle::Plan plan = SettingsBundle::planImport(path, &error);
        QVERIFY2(!error.isEmpty(), qPrintable(mention));
        QVERIFY2(error.contains(mention), qPrintable(error));
        QVERIFY(plan.isEmpty());
    };
    refused(R"({"format":"omastrator-settings","version":99,"settings":{}})", QStringLiteral("newer Omastrator"));
    refused(R"({"format":"something-else","version":1,"settings":{}})", QStringLiteral("isn't an Omastrator settings file"));
    refused("[1, 2, 3]", QStringLiteral("isn't an Omastrator settings file"));
    refused("not json at all", QStringLiteral("isn't an Omastrator settings file"));
    refused(R"({"format":"omastrator-settings","version":1})", QStringLiteral("isn't an Omastrator settings file"));
    QString error;
    SettingsBundle::planImport(m_files.filePath(QStringLiteral("missing.json")), &error);
    QVERIFY(error.startsWith(QLatin1String("Couldn't open")));
    writeFile(path, QByteArray(5 * 1024 * 1024, ' '));
    SettingsBundle::planImport(path, &error);
    QVERIFY(error.contains(QLatin1String("too big")));
    QCOMPARE(flat(now()), before);
}

void SettingsBundleTests::identicalSettingsOnlyShowANotice()
{
    fillSettings();
    fillPresets();
    const QString file = m_files.filePath(QStringLiteral("same.json"));
    QVERIFY(SettingsBundle::exportTo(file).isEmpty());

    ProjectWorkspace workspace;
    QStringList errors;
    workspace.errorHandler = [&errors](const QString &, const QString &message) { errors << message; };
    bool asked = false;
    SettingsConfirmDialog::setResponder([&asked](SettingsConfirmDialog &) {
        asked = true;
        return true;
    });
    workspace.importSettingsFrom(file);
    QVERIFY(!asked);
    QVERIFY(errors.isEmpty());
    QCOMPARE(workspace.cloudStatusText(), QString("These settings match the ones you have, so nothing changed."));
    QVERIFY(!QDir(SettingsBundle::backupFolder()).exists());

    // A file that isn't settings is an alert, not a confirm.
    const QString bad = m_files.filePath(QStringLiteral("bad.json"));
    writeFile(bad, "{}");
    workspace.importSettingsFrom(bad);
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().contains(QLatin1String("isn't an Omastrator settings file")));
    QVERIFY(!asked);

    // A real difference asks once, then says where the backup went.
    QSettings().setValue("keyboardIncrement", 9.0);
    QSettings().sync();
    workspace.importSettingsFrom(file);
    QVERIFY(asked);
    QVERIFY(workspace.cloudStatusText().startsWith(QLatin1String("Imported settings.")));
    QCOMPARE(QSettings().value("keyboardIncrement").toDouble(), 2.5);
}

void SettingsBundleTests::cloudExportAndImportGoThroughRclone()
{
    FakeCloud cloud;
    cloud.addRemote(QStringLiteral("work"), QStringLiteral("drive"));
    // The fake moves XDG_CONFIG_HOME; presets follow it, and the settings file stays where it was.
    QFile::remove(PresetStore::path());
    fillSettings();
    fillPresets();
    const auto before = flat(now());
    const QJsonObject presetsBefore = *now().presets;

    ProjectWorkspace workspace;
    QSignalSpy listed(&workspace.cloud(), &CloudStorage::remotesChanged);
    workspace.cloud().refreshRemotes();
    QVERIFY(listed.wait(10000));

    workspace.exportSettings();
    CloudBrowser *browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    browser->showRemote(QStringLiteral("work"));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading(), 10000);
    auto *name = browser->findChild<QLineEdit *>(QStringLiteral("fileName"));
    QVERIFY(name);
    QCOMPARE(name->text(), QString("Omastrator Settings.json"));
    browser->findChild<QPushButton *>(QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(!cloud.read(QStringLiteral("work"), QStringLiteral("Omastrator Settings.json")).isEmpty(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.uploads().pendingKeys().isEmpty(), 10000);

    // What reached the remote is the settings file, with nothing rclone-side in it.
    const QByteArray uploaded = cloud.read(QStringLiteral("work"), QStringLiteral("Omastrator Settings.json"));
    QCOMPARE(QJsonDocument::fromJson(uploaded).object()["format"].toString(), QString("omastrator-settings"));
    QVERIFY(!uploaded.contains(FakeCloud::secret));
    QVERIFY(!uploaded.contains("rclone"));
    QVERIFY(!uploaded.contains("\"token\""));

    // Another computer imports it from the same remote.
    forgetEverything();
    QVERIFY(flat(now()).isEmpty());
    bool asked = false;
    SettingsConfirmDialog::setResponder([&asked](SettingsConfirmDialog &) {
        asked = true;
        return true;
    });
    workspace.importSettings();
    browser = shown<CloudBrowser>(QStringLiteral("cloudBrowser"));
    QVERIFY(browser);
    browser->showRemote(QStringLiteral("work"));
    QTRY_VERIFY_WITH_TIMEOUT(!browser->isLoading() && browser->findChild<QListWidget *>(QStringLiteral("entries"))->count() > 0, 10000);
    auto *entries = browser->findChild<QListWidget *>(QStringLiteral("entries"));
    QCOMPARE(entries->count(), 1);
    entries->setCurrentRow(0);
    browser->findChild<QPushButton *>(QStringLiteral("chooseButton"))->click();
    QTRY_VERIFY_WITH_TIMEOUT(asked, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(workspace.cloudStatusText().startsWith(QLatin1String("Imported settings.")), 10000);
    QCOMPARE(flat(now()), before);
    QCOMPARE(*now().presets, presetsBefore);
}

void SettingsBundleTests::theEditMenuAndCommandPaletteOfferBoth()
{
    ProjectWorkspace workspace;
    ProjectWorkspaceView window(workspace);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QAction *exporting = window.menus()->action("exportSettings");
    QAction *importing = window.menus()->action("importSettings");
    QVERIFY(exporting && importing);
    QCOMPARE(exporting->text(), QString("Export Settings…"));
    QCOMPARE(importing->text(), QString("Import Settings…"));
    QVERIFY(exporting->isEnabled() && importing->isEnabled());
    // Ctrl+K finds them by name and by what people call them.
    CommandPalette *palette = window.menus()->commandPalette();
    palette->open();
    for (const QString &query : {QStringLiteral("export settings"), QStringLiteral("backup")}) {
        bool found = false;
        for (const CommandPalette::Command &command : palette->results(query))
            found = found || command.id == QLatin1String("action:exportSettings");
        QVERIFY2(found, qPrintable(query));
    }
    // Import from a file dialog: nothing happens until a file is picked.
    importing->trigger();
    QVERIFY(window.findChild<QFileDialog *>() != nullptr);
}

QTEST_MAIN(SettingsBundleTests)
#include "SettingsBundleTests.moc"
