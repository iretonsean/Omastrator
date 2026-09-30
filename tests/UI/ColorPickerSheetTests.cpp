#include "TemporaryConfig.h"
#include "UI/ColorPickerSheet.h"
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest>

// The picker: field, strip, channels, hex, OK and Cancel.
namespace {
template <typename Widget> Widget &find(QWidget &root, const QString &name)
{
    Widget *found = root.findChild<Widget *>(name);
    if (!found)
        throw std::runtime_error("no widget named " + name.toStdString());
    return *found;
}

struct Picker {
    std::optional<std::optional<QColor>> finished;
    ColorPickerSheet sheet;
    explicit Picker(QColor start, bool alpha = false) : sheet(start, [this](std::optional<QColor> chosen) { finished = chosen; }, nullptr, alpha)
    {
        sheet.show();
        if (!QTest::qWaitForWindowExposed(&sheet))
            throw std::runtime_error("the picker never showed");
    }
    PickerField &field(const char *name) { return find<PickerField>(sheet, QString::fromLatin1(name)); }
    // Types into a field and leaves it with Return.
    void type(const char *name, const QString &text)
    {
        PickerField &entry = field(name);
        entry.setFocus();
        entry.selectAll();
        QTest::keyClicks(&entry, text);
        QTest::keyClick(&entry, Qt::Key_Return);
    }
};
}

class ColorPickerSheetTests : public QObject {
    Q_OBJECT
private slots:
    // The recent colours are kept in the settings; a test must not write the user's.
    void initTestCase() { useTemporaryConfig(); }
    void init() { QSettings().clear(); }
    void hsbRoundTripsColours();
    void fieldsShowTheStartingColour();
    void channelsAndHexSetTheColour();
    void theFieldAndStripPickByPointer();
    void okAndCancelFinish();
    void opacityIsAnOptionInTheSheet();
    void aRecentColourKeepsItsOpacity();
};

void ColorPickerSheetTests::hsbRoundTripsColours()
{
    const PickerHSB red = PickerHSB::from(QColor(255, 0, 0));
    QCOMPARE(red.hue, 0.0);
    QCOMPARE(red.saturation, 1.0);
    QCOMPARE(red.brightness, 1.0);
    QCOMPARE(PickerHSB::from(QColor(0, 0, 255)).hue, 240.0);
    // Grey has no hue: zero, never Qt's −1.
    QCOMPARE(PickerHSB::from(QColor(128, 128, 128)).hue, 0.0);
    QCOMPARE((PickerHSB{120, 1, 1}.color()), QColor(0, 255, 0));
    QCOMPARE((PickerHSB{360, 1, 1}.color()), QColor(255, 0, 0));
}

void ColorPickerSheetTests::fieldsShowTheStartingColour()
{
    Picker picker(QColor(0x12, 0x34, 0x56));
    QCOMPARE(picker.field("r").text(), QString("18"));
    QCOMPARE(picker.field("g").text(), QString("52"));
    QCOMPARE(picker.field("b").text(), QString("86"));
    QCOMPARE(picker.field("hex").text(), QString("123456"));
    QCOMPARE(picker.sheet.color(), QColor(0x12, 0x34, 0x56));
}

void ColorPickerSheetTests::channelsAndHexSetTheColour()
{
    Picker picker(QColor(0, 0, 0));
    QSignalSpy changes(&picker.sheet, &ColorPickerSheet::colorChanged);
    picker.type("r", "200");
    QCOMPARE(picker.sheet.color(), QColor(200, 0, 0));
    QCOMPARE(picker.field("hex").text(), QString("C80000"));
    // Overflows clamp to 255; arrows step, Shift by ten.
    picker.type("g", "999");
    QCOMPARE(picker.sheet.color().green(), 255);
    QTest::keyClick(&picker.field("b"), Qt::Key_Up, Qt::ShiftModifier);
    QCOMPARE(picker.sheet.color().blue(), 10);
    picker.type("hex", "#00ff80");
    QCOMPARE(picker.sheet.color(), QColor(0, 255, 128));
    QCOMPARE(picker.field("r").text(), QString("0"));
    // A malformed hex is refused and shows the colour again.
    picker.type("hex", "zz");
    QCOMPARE(picker.sheet.color(), QColor(0, 255, 128));
    QCOMPARE(picker.field("hex").text(), QString("00FF80"));
    QCOMPARE(changes.count(), 4);
}

void ColorPickerSheetTests::theFieldAndStripPickByPointer()
{
    Picker picker(QColor(255, 0, 0));
    QWidget &field = find<QWidget>(picker.sheet, "saturationBrightness");
    QWidget &strip = find<QWidget>(picker.sheet, "hueStrip");
    // The field's bottom-left is black, its top-right the hue.
    QTest::mouseClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(0, 255));
    QCOMPARE(picker.sheet.color(), QColor(0, 0, 0));
    QTest::mouseClick(&field, Qt::LeftButton, Qt::NoModifier, QPoint(255, 0));
    QCOMPARE(picker.sheet.color(), QColor(255, 0, 0));
    // A third down the strip is 240 degrees: blue.
    QTest::mouseClick(&strip, Qt::LeftButton, Qt::NoModifier, QPoint(17, int(256.0 / 3)));
    QVERIFY(std::abs(picker.sheet.hsb().hue - 240) < 1.5);
    QCOMPARE(picker.sheet.color().blue(), 255);
    QCOMPARE(picker.sheet.color().red(), 0);
}

void ColorPickerSheetTests::okAndCancelFinish()
{
    Picker ok(QColor(10, 20, 30));
    find<QPushButton>(ok.sheet, "pickerOK").click();
    QCOMPARE(ok.finished.value().value(), QColor(10, 20, 30));
    Picker cancel(QColor(10, 20, 30));
    find<QPushButton>(cancel.sheet, "pickerCancel").click();
    QVERIFY(cancel.finished.has_value() && !cancel.finished.value());
}

void ColorPickerSheetTests::opacityIsAnOptionInTheSheet()
{
    // Without the option there is no field, and the colour is opaque whatever it started as.
    {
        Picker plain(QColor(10, 20, 30, 128));
        QVERIFY(!plain.sheet.findChild<PickerField *>(QStringLiteral("alpha")));
        QCOMPARE(plain.sheet.color(), QColor(10, 20, 30));
    }
    Picker picker(QColor(10, 20, 30, 128), true);
    QCOMPARE(picker.field("alpha").text(), QStringLiteral("50"));
    QSignalSpy changed(&picker.sheet, &ColorPickerSheet::colorChanged);
    picker.type("alpha", QStringLiteral("25"));
    QVERIFY(qAbs(picker.sheet.color().alphaF() - 0.25) < 0.001);
    QCOMPARE(picker.sheet.color().rgb(), QColor(10, 20, 30).rgb());
    QVERIFY(changed.size() >= 1);
    // The arrows step one percent, and a typed overflow stops at 100.
    QTest::keyClick(&picker.field("alpha"), Qt::Key_Up);
    QCOMPARE(picker.field("alpha").text(), QStringLiteral("26"));
    picker.type("alpha", QStringLiteral("250"));
    QCOMPARE(picker.sheet.color().alpha(), 255);
    picker.sheet.setAlphaPercent(40);
    QCOMPARE(picker.field("alpha").text(), QStringLiteral("40"));
    find<QPushButton>(picker.sheet, QStringLiteral("pickerOK")).click();
    QVERIFY(picker.finished && *picker.finished);
    QVERIFY(qAbs((*picker.finished)->alphaF() - 0.4) < 0.001);
}

void ColorPickerSheetTests::aRecentColourKeepsItsOpacity()
{
    // The translucent one is older, so it is the second chip; the newest is opaque.
    RecentColors::add(QColor(200, 30, 40, 128));
    RecentColors::add(QColor(10, 20, 30));
    const std::vector<QColor> recent = RecentColors::list();
    QCOMPARE(recent.size(), size_t(2));
    QCOMPARE(recent.at(1).rgba(), QColor(200, 30, 40, 128).rgba());
    // The same colour at another opacity is another recent colour.
    RecentColors::add(QColor(200, 30, 40, 255));
    QCOMPARE(RecentColors::list().size(), size_t(3));

    {
        // With an opacity field, a chip brings its opacity: 50 % and back to 100 %.
        Picker picker(QColor(1, 2, 3), true);
        const QList<QAbstractButton *> chips = picker.sheet.findChildren<QAbstractButton *>(QStringLiteral("recentColor"));
        QCOMPARE(chips.size(), qsizetype(3));
        // Newest first: opaque red, the blue-grey, then the translucent red.
        QVERIFY(chips.at(2)->toolTip().contains(QLatin1String("50%")));
        chips.at(2)->click();
        QCOMPARE(picker.sheet.color().rgb(), QColor(200, 30, 40).rgb());
        QVERIFY(qAbs(picker.sheet.color().alphaF() - 0.5) < 0.01);
        QCOMPARE(picker.field("alpha").text(), QStringLiteral("50"));
        chips.at(1)->click();
        QCOMPARE(picker.sheet.color(), QColor(10, 20, 30));
        QCOMPARE(picker.field("alpha").text(), QStringLiteral("100"));
        // OK keeps the opacity in the recent colours too.
        picker.sheet.setAlphaPercent(25);
        find<QPushButton>(picker.sheet, QStringLiteral("pickerOK")).click();
        QVERIFY(qAbs(RecentColors::list().front().alphaF() - 0.25) < 0.01);
    }
    {
        // Without one, a chip sets the colour as it always did, opaque.
        Picker plain(QColor(1, 2, 3));
        const QList<QAbstractButton *> chips = plain.sheet.findChildren<QAbstractButton *>(QStringLiteral("recentColor"));
        chips.at(chips.size() - 1)->click();
        QCOMPARE(plain.sheet.color().alpha(), 255);
    }
}

QTEST_MAIN(ColorPickerSheetTests)
#include "ColorPickerSheetTests.moc"
