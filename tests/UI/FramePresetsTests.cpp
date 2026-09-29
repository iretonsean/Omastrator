#include "Document/PathOperations.h"
#include "UI/FramePresets.h"
#include "UI/NewDocumentSheet.h"
#include "UI/PropertiesPanel.h"
#include "UI/PresetStore.h"
#include "TemporaryConfig.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

// Properties ▸ Frame: device and screen sizes while the Frame tool is active.
namespace {
struct Fixture {
    EditorSession session;
    PropertiesPanel panel{session};
    FramePresetsSection &section;
    QListWidget &list;
    QComboBox &group;

    Fixture()
        : section(*panel.findChild<FramePresetsSection *>("framePresetsSection")), list(*panel.findChild<QListWidget *>("framePresetList")),
          group(*panel.findChild<QComboBox *>("framePresetGroup"))
    {
        session.createDocument(QSizeF(1000, 1000));
        panel.resize(300, 900);
        panel.show();
        session.selectTool(Tool::frame);
    }
    QStringList rows() const
    {
        QStringList names;
        for (int i = 0; i < list.count(); ++i)
            names << list.item(i)->data(Qt::UserRole).toString();
        return names;
    }
    QStringList groups() const
    {
        QStringList names;
        for (int i = 0; i < group.count(); ++i)
            names << group.itemText(i);
        return names;
    }
    void chooseGroup(const QString &name)
    {
        const int index = group.findText(name);
        QVERIFY(index >= 0);
        group.setCurrentIndex(index);
        emit group.activated(index);
    }
    // A click on the row, as a user makes it.
    void click(const QString &name)
    {
        for (int i = 0; i < list.count(); ++i)
            if (list.item(i)->data(Qt::UserRole).toString() == name) {
                QTest::mouseClick(list.viewport(), Qt::LeftButton, {}, list.visualItemRect(list.item(i)).center());
                return;
            }
        QFAIL(qPrintable("no row " + name));
    }
};

QJsonObject fileContents()
{
    QFile file(PresetStore::path());
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}
}

class FramePresetsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void theSectionShowsWhileTheFrameToolIsActive();
    void everyGroupHasSizes();
    void pickingIPhone16DropsA393By852Frame();
    void theFrameLandsInTheMiddleOfTheView();
    void pickingTwiceGivesEachFrameItsOwnName();
    void aSavedFrameSizeBecomesAPreset();
    void aNameThatClashesIsRefused();
    void renameAndDeleteApplyToSavedOnly();
    void builtInsHideAndReturn();
    void framesAndDocumentsKeepTheirOwnSections();
    void anotherWindowSeesWhatWasSaved();
    void aFileThatCannotBeReadIsNeverOverwritten();

private:
    static std::optional<QString> answer;
};
std::optional<QString> FramePresetsTests::answer;

void FramePresetsTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
    FramePresetsSection::setNamer([](QWidget *, const QString &, const QString &) { return answer; });
}

void FramePresetsTests::init()
{
    QSettings().clear();
    QFile::remove(PresetStore::path());
}

void FramePresetsTests::cleanup()
{
    answer.reset();
    QSettings().clear();
}

void FramePresetsTests::theSectionShowsWhileTheFrameToolIsActive()
{
    Fixture f;
    QVERIFY(f.section.isVisible());
    f.session.selectTool(Tool::select);
    QVERIFY(!f.section.isVisible());
    f.session.selectTool(Tool::frame);
    QVERIFY(f.section.isVisible());
}

void FramePresetsTests::everyGroupHasSizes()
{
    Fixture f;
    QCOMPARE(f.groups(), (QStringList{"Phone", "Tablet", "Desktop", "Social", "Paper"}));
    for (const QString &name : f.groups()) {
        f.chooseGroup(name);
        QVERIFY2(f.list.count() > 0, qPrintable(name));
    }
    f.chooseGroup("Phone");
    QVERIFY(f.rows().contains("iPhone 16"));
    if (const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB"); !grab.isEmpty()) {
        QTest::qWait(100);
        f.panel.grab().save(QString::fromLocal8Bit(grab) + QStringLiteral("/frame-presets-panel.png"));
    }
    f.chooseGroup("Paper");
    QVERIFY(f.rows().contains("A4"));
    // The last group looked at is the one shown next time.
    Fixture again;
    QCOMPARE(again.group.currentText(), QString("Paper"));
}

void FramePresetsTests::pickingIPhone16DropsA393By852Frame()
{
    Fixture f;
    const size_t before = f.session.document()->objects.size();
    f.click("iPhone 16");
    QCOMPARE(f.session.selectedFrames().size(), size_t(1));
    const VectorObject *frame = f.session.document()->find(f.session.selection().front());
    QCOMPARE(frame->kind, ObjectKind::frame);
    QCOMPARE(frame->name, QString("iPhone 16"));
    QCOMPARE(f.session.document()->bounds(frame->id).size(), QSizeF(393, 852));
    // One step, and the tool is Select so the frame's own properties show.
    QCOMPARE(f.session.undoName(), QString("Frame"));
    QCOMPARE(f.session.tool(), Tool::select);
    QVERIFY(!f.section.isVisible());
    f.session.undo();
    QCOMPARE(f.session.document()->objects.size(), before);
}

void FramePresetsTests::theFrameLandsInTheMiddleOfTheView()
{
    Fixture f;
    // Without a canvas the active artboard's middle stands in.
    f.chooseGroup("Social");
    f.click("Instagram post");
    QCOMPARE(f.session.document()->bounds(f.session.selection().front()), QRectF(-40, -40, 1080, 1080));
    // With a view, its centre.
    f.session.selectTool(Tool::frame);
    f.session.viewport.resize(QSizeF(600, 400), 1, f.session.document()->size);
    const QRectF placed = f.session.framePlacement(QSizeF(100, 50));
    const QPointF middle = f.session.viewport.documentPoint(f.session.viewport.center(), f.session.document()->size);
    QVERIFY(qAbs(placed.center().x() - middle.x()) <= 0.5 && qAbs(placed.center().y() - middle.y()) <= 0.5);
    QCOMPARE(placed.topLeft(), QPointF(std::round(placed.left()), std::round(placed.top())));
}

void FramePresetsTests::pickingTwiceGivesEachFrameItsOwnName()
{
    Fixture f;
    const size_t before = f.session.document()->objects.size();
    f.click("iPhone 16");
    f.session.selectTool(Tool::frame);
    f.click("iPhone 16");
    QCOMPARE(f.session.document()->find(f.session.selection().front())->name, QString("iPhone 16 2"));
    f.session.selectTool(Tool::frame);
    f.click("iPhone 16");
    QCOMPARE(f.session.document()->find(f.session.selection().front())->name, QString("iPhone 16 3"));
    QCOMPARE(f.session.document()->objects.size(), before + 3);
}

void FramePresetsTests::aSavedFrameSizeBecomesAPreset()
{
    Fixture f;
    // Nothing selected, nothing to save.
    f.section.saveSelectedFrame();
    QVERIFY(!QFile::exists(PresetStore::path()));
    const QUuid frame = f.session.addFrame(QRectF(10, 10, 320.4, 200));
    f.session.selectTool(Tool::frame);
    QVERIFY(f.panel.findChild<QAction *>("saveFramePreset")->isEnabled());
    answer = QStringLiteral("  Kiosk ");
    f.section.saveSelectedFrame();
    QCOMPARE(f.groups().first(), QString("Saved"));
    QCOMPARE(f.group.currentText(), QString("Saved"));
    QCOMPARE(f.rows(), QStringList{"Kiosk"});
    const PresetStore::Section stored = PresetStore::read(PresetStore::frames);
    QCOMPARE(stored.saved.size(), size_t(1));
    QCOMPARE(stored.saved[0].points, QSizeF(320, 200));
    // Saved presets drop frames like the built-ins do.
    f.session.select({});
    f.click("Kiosk");
    QVERIFY(f.session.selection().front() != frame);
    QCOMPARE(f.session.document()->bounds(f.session.selection().front()).size(), QSizeF(320, 200));
    // And they come back in a window opened later, at the top.
    Fixture later;
    QCOMPARE(later.groups().first(), QString("Saved"));
}

void FramePresetsTests::aNameThatClashesIsRefused()
{
    Fixture f;
    f.session.addFrame(QRectF(0, 0, 100, 100));
    answer = QStringLiteral("iphone 16");
    f.section.saveSelectedFrame();
    QVERIFY(PresetStore::read(PresetStore::frames).saved.empty());
    QVERIFY(f.panel.findChild<QLabel *>("framePresetNote")->text().contains("built-in"));
    answer = QStringLiteral("Mine");
    f.section.saveSelectedFrame();
    answer = QStringLiteral("MINE");
    f.section.saveSelectedFrame();
    // The same name again replaces the saved one, as on the welcome sheet.
    QCOMPARE(PresetStore::read(PresetStore::frames).saved.size(), size_t(1));
    QCOMPARE(PresetStore::read(PresetStore::frames).saved[0].name, QString("MINE"));
}

void FramePresetsTests::renameAndDeleteApplyToSavedOnly()
{
    Fixture f;
    f.session.addFrame(QRectF(0, 0, 100, 100));
    answer = QStringLiteral("Mine");
    f.section.saveSelectedFrame();
    answer = QStringLiteral("Ours");
    f.section.renameSaved("Mine");
    QCOMPARE(f.rows(), QStringList{"Ours"});
    // A built-in can't be renamed or deleted.
    f.section.renameSaved("iPhone 16");
    f.section.deleteSaved("iPhone 16");
    QCOMPARE(PresetStore::read(PresetStore::frames).saved.size(), size_t(1));
    QVERIFY(PresetStore::read(PresetStore::frames).hidden.isEmpty());
    f.section.deleteSaved("Ours");
    QVERIFY(PresetStore::read(PresetStore::frames).saved.empty());
    QVERIFY(!f.groups().contains("Saved"));
}

void FramePresetsTests::builtInsHideAndReturn()
{
    Fixture f;
    f.section.hideBuiltIn("iPhone 16");
    f.section.hideBuiltIn("iPhone 16");
    QCOMPARE(PresetStore::read(PresetStore::frames).hidden, QStringList{"iPhone 16"});
    f.chooseGroup("Phone");
    QVERIFY(!f.rows().contains("iPhone 16"));
    Fixture later;
    later.chooseGroup("Phone");
    QVERIFY(!later.rows().contains("iPhone 16"));
    f.section.showHidden();
    f.chooseGroup("Phone");
    QVERIFY(f.rows().contains("iPhone 16"));
    QVERIFY(PresetStore::read(PresetStore::frames).hidden.isEmpty());
    // Every one hidden leaves a way back.
    for (const FramePresets::Preset &preset : FramePresets::builtIn())
        f.section.hideBuiltIn(QString::fromUtf8(preset.name));
    QVERIFY(f.group.isHidden() && f.list.isHidden());
    QVERIFY(f.panel.findChild<QLabel *>("framePresetNote")->text().contains("hidden"));
}

void FramePresetsTests::framesAndDocumentsKeepTheirOwnSections()
{
    PresetStore::Section documents;
    documents.saved.push_back({"Poster", QSizeF(1200, 1800), LengthUnit::px});
    documents.hidden << "A3";
    QVERIFY(PresetStore::write(PresetStore::documents, documents).isEmpty());
    Fixture f;
    f.session.addFrame(QRectF(0, 0, 100, 100));
    answer = QStringLiteral("Tile");
    f.section.saveSelectedFrame();
    const PresetStore::Section after = PresetStore::read(PresetStore::documents);
    QCOMPARE(after.saved.size(), size_t(1));
    QCOMPARE(after.saved[0].name, QString("Poster"));
    QCOMPARE(after.hidden, QStringList{"A3"});
    QCOMPARE(PresetStore::read(PresetStore::frames).saved.size(), size_t(1));
    // The welcome sheet's own writes leave the frames alone.
    NewDocumentSheet sheet([](QSizeF) {}, [] {}, [](const QString &) {});
    QCOMPARE(fileContents()["frames"].toObject()["saved"].toArray().size(), 1);
}

void FramePresetsTests::anotherWindowSeesWhatWasSaved()
{
    Fixture older;
    Fixture newer;
    newer.session.addFrame(QRectF(0, 0, 100, 100));
    answer = QStringLiteral("Tile");
    newer.section.saveSelectedFrame();
    older.section.hideBuiltIn("A4");
    // The older window's hide started from the file, so Tile is still there.
    QCOMPARE(PresetStore::read(PresetStore::frames).saved.size(), size_t(1));
    QCOMPARE(PresetStore::read(PresetStore::frames).hidden, QStringList{"A4"});
    // A window shown after a change reads it.
    older.session.selectTool(Tool::select);
    newer.section.hideBuiltIn("A5");
    older.session.selectTool(Tool::frame);
    QVERIFY(older.groups().contains("Saved"));
    older.chooseGroup("Paper");
    QVERIFY(!older.rows().contains("A5") && !older.rows().contains("A4"));
}

void FramePresetsTests::aFileThatCannotBeReadIsNeverOverwritten()
{
    QVERIFY(QDir().mkpath(QFileInfo(PresetStore::path()).absolutePath()));
    QFile file(PresetStore::path());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not json");
    file.close();
    Fixture f;
    f.section.hideBuiltIn("A4");
    QVERIFY(f.panel.findChild<QLabel *>("framePresetNote")->text().contains("can't be read"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("not json"));
}

QTEST_MAIN(FramePresetsTests)
#include "FramePresetsTests.moc"
