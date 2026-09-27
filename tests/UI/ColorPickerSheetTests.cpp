#include "UI/ColorPickerSheet.h"
#include <QPushButton>
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
    explicit Picker(QColor start) : sheet(start, [this](std::optional<QColor> chosen) { finished = chosen; })
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
    void hsbRoundTripsColours();
    void fieldsShowTheStartingColour();
    void channelsAndHexSetTheColour();
    void theFieldAndStripPickByPointer();
    void okAndCancelFinish();
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

QTEST_MAIN(ColorPickerSheetTests)
#include "ColorPickerSheetTests.moc"
