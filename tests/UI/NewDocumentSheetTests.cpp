#include "UI/KeyboardShortcuts.h"
#include "UI/NewDocumentSheet.h"
#include "UI/ProjectWorkspace.h"
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

// The welcome: presets, units, validation, create and recent files.
namespace {
template <typename Widget> Widget &find(QWidget &root, const QString &name)
{
    Widget *found = root.findChild<Widget *>(name);
    if (!found)
        throw std::runtime_error("no widget named " + name.toStdString());
    return *found;
}

struct Sheet {
    std::optional<QSizeF> created;
    int opens = 0;
    QString recent;
    NewDocumentSheet sheet{[this](QSizeF size) { created = size; }, [this] { ++opens; }, [this](const QString &path) { recent = path; }};
    QAction &action(const char *name) { return find<QAction>(sheet, name); }
    QStringList names() const
    {
        QStringList all;
        const auto *combo = sheet.findChild<QComboBox *>("presetInput");
        for (int i = 0; i < combo->count(); ++i)
            all << combo->itemText(i);
        return all;
    }
    QComboBox &preset() { return find<QComboBox>(sheet, "presetInput"); }
    QComboBox &unit() { return find<QComboBox>(sheet, "unitInput"); }
    QLineEdit &width() { return find<QLineEdit>(sheet, "widthInput"); }
    QLineEdit &height() { return find<QLineEdit>(sheet, "heightInput"); }
    QPushButton &create() { return find<QPushButton>(sheet, "createDocument"); }
    // A combo choice as a user makes it.
    void choose(QComboBox &box, int index)
    {
        box.setCurrentIndex(index);
        emit box.activated(index);
    }
};
}

class NewDocumentSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void lengthsReadInTheirUnits();
    void presetsFillTheFields();
    void unitsConvertWhatIsTyped();
    void invalidSizesRestCreate();
    void createAndOpenReachTheirCallbacks();
    void recentFilesAreListed();
    void storeKeepsWhatItDoesNotOwn();
    void storeSkipsBadEntriesAndBacksUpBrokenFiles();
    void savedPresetsShowAtTheTopNextTime();
    void savingNeedsAGoodSizeAndNameAndReplacesSameName();
    void renameAndDeleteApplyToSavedOnly();
    void builtInsHideAndReturn();
    void savedPresetsKeepTheirUnit();

private:
    QTemporaryDir m_config;
    static std::optional<QString> answer;
};
std::optional<QString> NewDocumentSheetTests::answer;

void NewDocumentSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    ProjectWorkspace::clearRecent();
    NewDocumentSheet::setNamer([](QWidget *, const QString &, const QString &) { return answer; });
}

void NewDocumentSheetTests::cleanup()
{
    ProjectWorkspace::clearRecent();
    QFile::remove(PresetStore::path());
    QFile::remove(PresetStore::path() + ".bak");
    answer.reset();
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

void NewDocumentSheetTests::lengthsReadInTheirUnits()
{
    QCOMPARE(NewDocumentSheet::pointsPer(LengthUnit::pt), 1.0);
    QCOMPARE(NewDocumentSheet::pointsPer(LengthUnit::px), 1.0);
    QCOMPARE(NewDocumentSheet::pointsPer(LengthUnit::in), 72.0);
    QCOMPARE(NewDocumentSheet::dimension("2", LengthUnit::in).value(), 144.0);
    QVERIFY(std::abs(NewDocumentSheet::dimension("210", LengthUnit::mm).value() - 595.2756) < 1e-3);
    QCOMPARE(NewDocumentSheet::dimension(" 1920 ", LengthUnit::px).value(), 1920.0);
    // Out of range, empty or no number: nothing.
    QVERIFY(!NewDocumentSheet::dimension("0.5", LengthUnit::pt));
    QVERIFY(!NewDocumentSheet::dimension("16385", LengthUnit::pt));
    QVERIFY(!NewDocumentSheet::dimension("300", LengthUnit::in));
    QVERIFY(!NewDocumentSheet::dimension("", LengthUnit::pt));
    QVERIFY(!NewDocumentSheet::dimension("wide", LengthUnit::pt));
    QVERIFY(!NewDocumentSheet::dimension("inf", LengthUnit::pt));
}

void NewDocumentSheetTests::presetsFillTheFields()
{
    Sheet sheet;
    // Letter first, in inches.
    QCOMPARE(sheet.preset().currentText(), QString("Letter"));
    QCOMPARE(sheet.unit().currentText(), QString("in"));
    QCOMPARE(sheet.width().text(), QString("8.5"));
    QCOMPARE(sheet.height().text(), QString("11"));
    QCOMPARE(find<QLabel>(sheet.sheet, "documentNote").text(), QString("612 × 792 pt · White artboard · sRGB"));
    sheet.choose(sheet.preset(), 1);
    QCOMPARE(sheet.unit().currentText(), QString("mm"));
    QCOMPARE(sheet.width().text(), QString("210"));
    QCOMPARE(sheet.height().text(), QString("297"));
    sheet.choose(sheet.preset(), 3);
    QCOMPARE(sheet.unit().currentText(), QString("px"));
    QCOMPARE(sheet.width().text(), QString("1920"));
    QCOMPARE(sheet.height().text(), QString("1080"));
    // Typing a size makes it Custom.
    QTest::keyClicks(&sheet.width(), "0");
    QCOMPARE(sheet.preset().currentText(), QString("Custom"));
    QCOMPARE(sheet.preset().count(), 6);
}

void NewDocumentSheetTests::unitsConvertWhatIsTyped()
{
    Sheet sheet;
    sheet.choose(sheet.unit(), int(LengthUnit::pt));
    QCOMPARE(sheet.width().text(), QString("612"));
    QCOMPARE(sheet.height().text(), QString("792"));
    sheet.choose(sheet.unit(), int(LengthUnit::mm));
    QCOMPARE(sheet.width().text(), QString("215.9"));
    QCOMPARE(sheet.height().text(), QString("279.4"));
    sheet.create().click();
    QVERIFY(std::abs(sheet.created.value().width() - 612) < 0.01);
    QVERIFY(std::abs(sheet.created.value().height() - 792) < 0.01);
}

void NewDocumentSheetTests::invalidSizesRestCreate()
{
    Sheet sheet;
    auto &note = find<QLabel>(sheet.sheet, "documentNote");
    sheet.width().setText("0");
    QVERIFY(!sheet.create().isEnabled());
    QCOMPARE(note.text(), QString("Enter sizes from 1 to 16,384 points."));
    QCOMPARE(note.foregroundRole(), QPalette::BrightText);
    // Return does nothing while the size is refused.
    sheet.sheet.show();
    QVERIFY(QTest::qWaitForWindowActive(&sheet.sheet));
    QTest::keyClick(&sheet.width(), Qt::Key_Return);
    QVERIFY(!sheet.created);
    sheet.width().setText("4");
    QVERIFY(sheet.create().isEnabled());
    QCOMPARE(note.foregroundRole(), QPalette::PlaceholderText);
    QTest::keyClick(&sheet.width(), Qt::Key_Return);
    QCOMPARE(sheet.created.value(), QSizeF(288, 792));
}

void NewDocumentSheetTests::createAndOpenReachTheirCallbacks()
{
    Sheet sheet;
    sheet.choose(sheet.preset(), 4);
    sheet.create().click();
    QCOMPARE(sheet.created.value(), QSizeF(1080, 1080));
    find<QPushButton>(sheet.sheet, "openDocument").click();
    QCOMPARE(sheet.opens, 1);
    // Without recent files there is no list.
    QVERIFY(!sheet.sheet.findChild<QWidget *>("recentFiles"));
}

void NewDocumentSheetTests::recentFilesAreListed()
{
    ProjectWorkspace::noteRecent("/tmp/first.omai");
    ProjectWorkspace::noteRecent("/tmp/second.svg");
    Sheet sheet;
    auto &list = find<QWidget>(sheet.sheet, "recentFiles");
    const QList<QPushButton *> entries = list.findChildren<QPushButton *>();
    QCOMPARE(entries.size(), 2);
    // Newest first, by file name, the path in the tip.
    QCOMPARE(entries.at(0)->text(), QString("second.svg"));
    QCOMPARE(entries.at(0)->toolTip(), QString("/tmp/second.svg"));
    entries.at(1)->click();
    QCOMPARE(sheet.recent, QString("/tmp/first.omai"));
}

void NewDocumentSheetTests::storeKeepsWhatItDoesNotOwn()
{
    QDir().mkpath(QFileInfo(PresetStore::path()).absolutePath());
    QFile file(PresetStore::path());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"version": 1, "frames": {"saved": [{"name": "Phone", "width": 393, "height": 852, "unit": "px"}], "hidden": []}, "extra": 7})");
    file.close();
    QVERIFY(PresetStore::write(PresetStore::documents, {{{"Poster", QSizeF(1200, 1800), LengthUnit::mm}}, {"A3"}}).isEmpty());
    // The documents section is ours; frames and unknown keys stay.
    const PresetStore::Section documents = PresetStore::read(PresetStore::documents);
    QCOMPARE(documents.saved.size(), size_t(1));
    QCOMPARE(documents.saved[0].name, QString("Poster"));
    QCOMPARE(documents.saved[0].points, QSizeF(1200, 1800));
    QVERIFY(documents.saved[0].unit == LengthUnit::mm);
    QCOMPARE(documents.hidden, QStringList{"A3"});
    const PresetStore::Section frames = PresetStore::read(PresetStore::frames);
    QCOMPARE(frames.saved.size(), size_t(1));
    QCOMPARE(frames.saved[0].name, QString("Phone"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["extra"].toInt(), 7);
}

void NewDocumentSheetTests::storeSkipsBadEntriesAndBacksUpBrokenFiles()
{
    QDir().mkpath(QFileInfo(PresetStore::path()).absolutePath());
    QFile file(PresetStore::path());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"documents": {"saved": [
        {"name": "Good", "width": 100, "height": 200, "unit": "in"},
        {"name": "", "width": 100, "height": 200},
        {"name": "Huge", "width": 99999, "height": 200},
        {"name": "good", "width": 5, "height": 5},
        {"name": "Odd unit", "width": 5, "height": 5, "unit": "furlong"},
        7], "hidden": ["A4", "A4", 3]}})");
    file.close();
    const PresetStore::Section section = PresetStore::read(PresetStore::documents);
    QCOMPARE(section.saved.size(), size_t(2));
    QCOMPARE(section.saved[0].name, QString("Good"));
    QCOMPARE(section.saved[1].name, QString("Odd unit"));
    QVERIFY(section.saved[1].unit == LengthUnit::px);
    QCOMPARE(section.hidden, QStringList{"A4"});
    // A file that isn't JSON reads as empty and is kept as .bak when the next write replaces it.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not json at all");
    file.close();
    QVERIFY(PresetStore::read(PresetStore::documents).saved.empty());
    QVERIFY(PresetStore::write(PresetStore::documents, {{{"Kept", QSizeF(10, 10), LengthUnit::pt}}, {}}).isEmpty());
    QFile backup(PresetStore::path() + ".bak");
    QVERIFY(backup.open(QIODevice::ReadOnly));
    QCOMPARE(backup.readAll(), QByteArray("not json at all"));
    QCOMPARE(PresetStore::read(PresetStore::documents).saved.size(), size_t(1));
}

void NewDocumentSheetTests::savedPresetsShowAtTheTopNextTime()
{
    {
        Sheet first;
        first.choose(first.preset(), 3);
        answer = "Zine cover";
        // 1920 × 1080 px is now a preset of its own, on top.
        first.action("savePreset").trigger();
        QCOMPARE(first.names(), QStringList({"Zine cover", "Letter", "A4", "A3", "1920 × 1080", "1080 × 1080", "Custom"}));
        QCOMPARE(first.preset().currentText(), QString("Zine cover"));
    }
    Sheet next;
    QCOMPARE(next.names().first(), QString("Zine cover"));
    // The default stays the first built-in, so Return in a fresh sheet still makes Letter.
    QCOMPARE(next.preset().currentText(), QString("Letter"));
    next.choose(next.preset(), 0);
    QCOMPARE(next.unit().currentText(), QString("px"));
    QCOMPARE(next.width().text(), QString("1920"));
    next.create().click();
    QCOMPARE(next.created.value(), QSizeF(1920, 1080));
}

void NewDocumentSheetTests::savingNeedsAGoodSizeAndNameAndReplacesSameName()
{
    Sheet sheet;
    auto &note = find<QLabel>(sheet.sheet, "documentNote");
    sheet.width().setText("0");
    QVERIFY(!sheet.action("savePreset").isEnabled());
    answer = "Nope";
    sheet.action("savePreset").trigger();
    QCOMPARE(sheet.names().size(), 6);
    sheet.width().setText("100");
    QVERIFY(sheet.action("savePreset").isEnabled());
    // A blank or cancelled name saves nothing.
    answer = "   ";
    sheet.action("savePreset").trigger();
    answer.reset();
    sheet.action("savePreset").trigger();
    QCOMPARE(sheet.names().size(), 6);
    // A built-in's name is taken, whatever its case.
    answer = "a4";
    sheet.action("savePreset").trigger();
    QCOMPARE(sheet.names().size(), 6);
    QVERIFY(note.text().contains("built-in"));
    answer = "Custom";
    sheet.action("savePreset").trigger();
    QCOMPARE(sheet.names().size(), 6);
    // The same saved name again replaces it, and moves it to the top.
    answer = "Banner";
    sheet.action("savePreset").trigger();
    sheet.width().setText("30");
    answer = "Sticker";
    sheet.action("savePreset").trigger();
    sheet.width().setText("40");
    answer = "banner";
    sheet.action("savePreset").trigger();
    QCOMPARE(sheet.names().mid(0, 3), QStringList({"banner", "Sticker", "Letter"}));
    QCOMPARE(PresetStore::read(PresetStore::documents).saved.size(), size_t(2));
    QCOMPARE(sheet.width().text(), QString("40"));
}

void NewDocumentSheetTests::renameAndDeleteApplyToSavedOnly()
{
    Sheet sheet;
    // Nothing saved yet: the built-in Letter can't be renamed or deleted, only hidden.
    QVERIFY(!sheet.action("renamePreset").isEnabled());
    QVERIFY(!sheet.action("deletePreset").isEnabled());
    QVERIFY(sheet.action("hidePreset").isEnabled());
    sheet.width().setText("100");
    answer = "Flyer";
    sheet.action("savePreset").trigger();
    sheet.width().setText("200");
    answer = "Leaflet";
    sheet.action("savePreset").trigger();
    QVERIFY(sheet.action("renamePreset").isEnabled());
    QVERIFY(sheet.action("deletePreset").isEnabled());
    QVERIFY(!sheet.action("hidePreset").isEnabled());
    // Rename keeps the size and follows the preset to its new name.
    answer = "Handbill";
    sheet.action("renamePreset").trigger();
    QCOMPARE(sheet.names().mid(0, 3), QStringList({"Handbill", "Flyer", "Letter"}));
    QCOMPARE(sheet.preset().currentText(), QString("Handbill"));
    QCOMPARE(sheet.width().text(), QString("200"));
    // Names already used are refused, in the note.
    answer = "flyer";
    sheet.action("renamePreset").trigger();
    QCOMPARE(sheet.preset().currentText(), QString("Handbill"));
    QVERIFY(find<QLabel>(sheet.sheet, "documentNote").text().contains("already"));
    answer = "Letter";
    sheet.action("renamePreset").trigger();
    QCOMPARE(sheet.preset().currentText(), QString("Handbill"));
    sheet.action("deletePreset").trigger();
    QCOMPARE(sheet.names().mid(0, 2), QStringList({"Flyer", "Letter"}));
    // Back on the first built-in.
    QCOMPARE(sheet.preset().currentText(), QString("Letter"));
    QCOMPARE(PresetStore::read(PresetStore::documents).saved.size(), size_t(1));
    // Renaming a built-in does nothing.
    sheet.action("renamePreset").trigger();
    QCOMPARE(sheet.names().size(), 7);
}

void NewDocumentSheetTests::builtInsHideAndReturn()
{
    {
        Sheet sheet;
        QVERIFY(!sheet.action("showHiddenPresets").isEnabled());
        sheet.action("hidePreset").trigger();
        QCOMPARE(sheet.names(), QStringList({"A4", "A3", "1920 × 1080", "1080 × 1080", "Custom"}));
        QCOMPARE(sheet.preset().currentText(), QString("A4"));
        QCOMPARE(sheet.width().text(), QString("210"));
        QVERIFY(sheet.action("showHiddenPresets").isEnabled());
    }
    // Hidden stays hidden in the next session.
    Sheet next;
    QCOMPARE(next.names().first(), QString("A4"));
    next.action("showHiddenPresets").trigger();
    QCOMPARE(next.names().first(), QString("Letter"));
    QCOMPARE(next.names().size(), 6);
    QVERIFY(PresetStore::read(PresetStore::documents).hidden.isEmpty());
    // Hide every built-in: only Custom is left, and the size stays usable.
    for (int i = 0; i < 5; ++i)
        next.action("hidePreset").trigger();
    QCOMPARE(next.names(), QStringList({"Custom"}));
    QVERIFY(next.create().isEnabled());
    QVERIFY(!next.action("hidePreset").isEnabled());
    // Saving into an empty list works.
    answer = "Only one";
    next.action("savePreset").trigger();
    QCOMPARE(next.names(), QStringList({"Only one", "Custom"}));
}

void NewDocumentSheetTests::savedPresetsKeepTheirUnit()
{
    Sheet sheet;
    sheet.choose(sheet.unit(), int(LengthUnit::mm));
    sheet.width().setText("100");
    sheet.height().setText("150");
    answer = "Postcard";
    sheet.action("savePreset").trigger();
    sheet.choose(sheet.preset(), 1);
    QCOMPARE(sheet.unit().currentText(), QString("in"));
    sheet.choose(sheet.preset(), 0);
    QCOMPARE(sheet.unit().currentText(), QString("mm"));
    QCOMPARE(sheet.width().text(), QString("100"));
    QCOMPARE(sheet.height().text(), QString("150"));
    sheet.create().click();
    QVERIFY(std::abs(sheet.created.value().width() - 100 * 72 / 25.4) < 0.01);
}

QTEST_MAIN(NewDocumentSheetTests)
#include "NewDocumentSheetTests.moc"
