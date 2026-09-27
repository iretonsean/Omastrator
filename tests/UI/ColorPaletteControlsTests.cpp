#include "Document/PathOperations.h"
#include "UI/ColorPaletteControls.h"
#include "UI/ColorPickerSheet.h"
#include <QApplication>
#include <QPushButton>
#include <QtTest>

// The rail's wells: what they show and set.
namespace {
QWidget *shownPicker()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("colorPickerPanel") && widget->isVisible())
            return widget;
    }
    return nullptr;
}

// Sets a colour in the shown picker, then OK.
void pick(const QString &hex)
{
    QWidget *panel = shownPicker();
    if (!panel)
        throw std::runtime_error("no picker shows");
    for (ColorPickerSheet *sheet : panel->findChildren<ColorPickerSheet *>()) {
        if (!sheet->isVisible())
            continue;
        sheet->setHSB(PickerHSB::from(QColor::fromString(hex)));
        sheet->findChild<QPushButton *>("pickerOK")->click();
        return;
    }
    throw std::runtime_error("no sheet shows");
}
}

class ColorPaletteControlsTests : public QObject {
    Q_OBJECT
private slots:
    void wellsShowTheSelectionOrTheDefaults();
    void pickersSetTheSelectionOrTheDefaults();
    void swapAndDefaultsFollowXAndD();
};

void ColorPaletteControlsTests::wellsShowTheSelectionOrTheDefaults()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 200));
    QCOMPARE(ShownStyle::fill(session), Paint::solid(Qt::white));
    QCOMPARE(ShownStyle::stroke(session).paint, Paint::solid(Qt::black));
    VectorObject box;
    box.path = Shapes::rectangle(QRectF(10, 10, 20, 20));
    box.fill = Paint::solid(Qt::red);
    box.stroke.width = 4;
    session.addObject(box, QStringLiteral("Box"));
    QCOMPARE(ShownStyle::fill(session), Paint::solid(Qt::red));
    QCOMPARE(ShownStyle::stroke(session).width, 4.0);
    // A group shows its first leaf's style.
    session.groupSelection();
    QCOMPARE(ShownStyle::fill(session), Paint::solid(Qt::red));
    session.deselectAll();
    QCOMPARE(ShownStyle::fill(session), Paint::solid(Qt::white));
}

void ColorPaletteControlsTests::pickersSetTheSelectionOrTheDefaults()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 200));
    ColorPaletteControls controls(session);
    controls.show();
    QVERIFY(QTest::qWaitForWindowExposed(&controls));
    // No selection: the defaults change.
    controls.findChild<QAbstractButton *>("fillSwatch")->click();
    QCOMPARE(shownPicker()->windowTitle(), QString("Fill Color"));
    pick("#336699");
    QCOMPARE(session.defaultFill(), Paint::solid(QColor(0x33, 0x66, 0x99)));
    const QUuid id = session.addPath(Shapes::ellipse(QRectF(0, 0, 50, 50)), QStringLiteral("Dot"));
    QCOMPARE(session.document().value().find(id)->fill, Paint::solid(QColor(0x33, 0x66, 0x99)));
    // A selection: its stroke changes, its width kept.
    controls.findChild<QAbstractButton *>("strokeSwatch")->click();
    QCOMPARE(shownPicker()->windowTitle(), QString("Stroke Color"));
    pick("#ff8800");
    QCOMPARE(session.document().value().find(id)->stroke.paint, Paint::solid(QColor(0xff, 0x88, 0x00)));
    QCOMPARE(session.document().value().find(id)->stroke.width, 1.0);
    QCOMPARE(session.undoName(), QString("Stroke"));
}

void ColorPaletteControlsTests::swapAndDefaultsFollowXAndD()
{
    EditorSession session;
    session.createDocument(QSizeF(200, 200));
    ColorPaletteControls controls(session);
    controls.findChild<QAbstractButton *>("swapFillStroke")->click();
    QCOMPARE(session.defaultFill(), Paint::solid(Qt::black));
    QCOMPARE(session.defaultStroke().paint, Paint::solid(Qt::white));
    QCOMPARE(controls.findChild<QAbstractButton *>("swapFillStroke")->toolTip(), QString("Swap fill and stroke (X)"));
    controls.findChild<QAbstractButton *>("defaultFillStroke")->click();
    QCOMPARE(session.defaultFill(), Paint::solid(Qt::white));
    QCOMPARE(session.defaultStroke().paint, Paint::solid(Qt::black));
}

QTEST_MAIN(ColorPaletteControlsTests)
#include "ColorPaletteControlsTests.moc"
