#include "UI/KeyboardShortcuts.h"
#include "UI/NewDocumentSheet.h"
#include "UI/ProjectWorkspace.h"
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
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
};

void NewDocumentSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    ProjectWorkspace::clearRecent();
}

void NewDocumentSheetTests::cleanup()
{
    ProjectWorkspace::clearRecent();
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

QTEST_MAIN(NewDocumentSheetTests)
#include "NewDocumentSheetTests.moc"
