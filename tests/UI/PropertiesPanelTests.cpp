#include "Document/PathOperations.h"
#include "UI/NumberField.h"
#include "UI/PropertiesPanel.h"
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QtTest>

// The inspector: transform, artboard, appearance, stroke, align, pathfinder.
namespace {
void type(PropertiesPanel &panel, const QString &field, const QString &text)
{
    auto *edit = panel.findChild<QLineEdit *>(field);
    QVERIFY(edit);
    edit->setText(text);
    QTest::keyClick(edit, Qt::Key_Return);
}

QRectF bounds(const EditorSession &session, const QUuid &id)
{
    return session.document()->bounds(id);
}

void choose(PropertiesPanel &panel, const QString &combo, int index)
{
    auto *box = panel.findChild<QComboBox *>(combo);
    box->setCurrentIndex(index);
    emit box->activated(index);
}
}

class PropertiesPanelTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void transformFieldsMoveAndScaleFromTheTopLeft();
    void rotationTurnsThenReadsZero();
    void withoutSelectionTheArtboardShows();
    void fillKindKeepsTheColour();
    void strokeSettingsApplyToTheSelection();
    void anOpacityDragIsOneStep();
    void blendModeApplies();
    void alignAndPathfinderFollowTheirGates();
    void swapAndDefaultButtons();

};

void PropertiesPanelTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void PropertiesPanelTests::transformFieldsMoveAndScaleFromTheTopLeft()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    QCOMPARE(panel.findChild<QLineEdit *>("transformXField")->text(), QString("10"));
    QCOMPARE(panel.findChild<QLineEdit *>("transformWField")->text(), QString("100"));
    type(panel, "transformXField", "30");
    QCOMPARE(bounds(session, box), QRectF(30, 20, 100, 50));
    QCOMPARE(session.undoName(), QString("Move"));
    type(panel, "transformYField", "5.5");
    QCOMPARE(bounds(session, box), QRectF(30, 5.5, 100, 50));
    QCOMPARE(panel.findChild<QLineEdit *>("transformYField")->text(), QString("5.50"));
    // Width and height scale with the top-left fixed.
    type(panel, "transformWField", "200");
    QCOMPARE(bounds(session, box), QRectF(30, 5.5, 200, 50));
    type(panel, "transformHField", "25");
    QCOMPARE(bounds(session, box), QRectF(30, 5.5, 200, 25));
    QCOMPARE(session.undoName(), QString("Scale"));
    // Nonsense and nothing new change nothing.
    type(panel, "transformWField", "wide");
    type(panel, "transformHField", "25");
    QCOMPARE(bounds(session, box), QRectF(30, 5.5, 200, 25));
    QCOMPARE(panel.findChild<QLineEdit *>("transformWField")->text(), QString("200"));
    // Up steps one point, Shift ten.
    QTest::keyClick(panel.findChild<QLineEdit *>("transformXField"), Qt::Key_Up);
    QCOMPARE(bounds(session, box).left(), 31.0);
    QTest::keyClick(panel.findChild<QLineEdit *>("transformXField"), Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(bounds(session, box).left(), 21.0);
}

void PropertiesPanelTests::rotationTurnsThenReadsZero()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 100, 50)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    type(panel, "transformRotationField", "90");
    const QRectF turned = bounds(session, box);
    QVERIFY(std::abs(turned.width() - 50) < 1e-6 && std::abs(turned.height() - 100) < 1e-6);
    QVERIFY(std::abs(turned.center().x() - 50) < 1e-6 && std::abs(turned.center().y() - 25) < 1e-6);
    QCOMPARE(session.undoName(), QString("Rotate"));
    QCOMPARE(panel.findChild<QLineEdit *>("transformRotationField")->text(), QString("0"));
}

void PropertiesPanelTests::withoutSelectionTheArtboardShows()
{
    EditorSession session;
    PropertiesPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    // No document: nothing to edit, no negative sizes.
    QVERIFY(!panel.widget()->isEnabled());
    QCOMPARE(panel.findChild<QLineEdit *>("artboardWidth")->text(), QString("0"));
    session.createDocument(QSizeF(612, 792));
    auto *artboard = panel.findChild<QWidget *>("artboardSection");
    auto *transform = panel.findChild<QWidget *>("transformSection");
    QVERIFY(artboard->isVisible() && !transform->isVisible());
    QCOMPARE(panel.findChild<QLineEdit *>("artboardWidth")->text(), QString("612"));
    type(panel, "artboardWidth", "800");
    type(panel, "artboardHeight", "600");
    QCOMPARE(session.document()->size, QSizeF(800, 600));
    QCOMPARE(session.undoName(), QString("Artboard Size"));
    // A zero size is refused.
    type(panel, "artboardHeight", "0");
    QCOMPARE(session.document()->size, QSizeF(800, 600));
    session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    QVERIFY(!artboard->isVisible() && transform->isVisible());
}

void PropertiesPanelTests::fillKindKeepsTheColour()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    session.setFillOfSelection(Paint::solid(QColor(200, 30, 40)));
    PropertiesPanel panel(session);
    auto *kind = panel.findChild<QComboBox *>("fillKind");
    QCOMPARE(kind->currentText(), QString("Solid"));
    QVERIFY(!panel.findChild<QWidget *>("fillEnd")->isVisibleTo(&panel));
    choose(panel, "fillKind", 2);
    const Paint linear = session.document()->find(box)->fill;
    QCOMPARE(linear, Paint::linear(QColor(200, 30, 40), Qt::white));
    QVERIFY(panel.findChild<QWidget *>("fillEnd")->isVisibleTo(&panel));
    // Radial keeps both ends; solid takes the first.
    choose(panel, "fillKind", 3);
    QCOMPARE(session.document()->find(box)->fill, Paint::radial(QColor(200, 30, 40), Qt::white));
    choose(panel, "fillKind", 1);
    QCOMPARE(session.document()->find(box)->fill, Paint::solid(QColor(200, 30, 40)));
    choose(panel, "fillKind", 0);
    QCOMPARE(session.document()->find(box)->fill.kind, PaintKind::none);
    QCOMPARE(kind->currentText(), QString("None"));
    // The stroke's kind edits the stroke's paint alone.
    choose(panel, "strokeKind", 0);
    QCOMPARE(session.document()->find(box)->stroke.paint.kind, PaintKind::none);
    QCOMPARE(session.document()->find(box)->stroke.width, 1.0);
    QCOMPARE(PaintRow::converted(Paint::none(), PaintKind::solid), Paint::solid(Qt::black));
}

void PropertiesPanelTests::strokeSettingsApplyToTheSelection()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("A"));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(20, 0, 10, 10)), QStringLiteral("B"));
    session.select({a, b});
    PropertiesPanel panel(session);
    type(panel, "strokeWidth", "4.5");
    QCOMPARE(session.document()->find(a)->stroke.width, 4.5);
    QCOMPARE(session.document()->find(b)->stroke.width, 4.5);
    choose(panel, "strokeCap", 1);
    choose(panel, "strokeJoin", 2);
    QCOMPARE(session.document()->find(b)->stroke.cap, Qt::RoundCap);
    QCOMPARE(session.document()->find(b)->stroke.join, Qt::BevelJoin);
    auto *dashes = panel.findChild<QLineEdit *>("strokeDashes");
    dashes->setText(QStringLiteral("12, 6 3"));
    emit dashes->editingFinished();
    QCOMPARE(session.document()->find(a)->stroke.dashes, (std::vector<double>{12, 6, 3}));
    QCOMPARE(dashes->text(), QString("12 6 3"));
    // Anything but numbers is refused and shown back.
    dashes->setText(QStringLiteral("long"));
    emit dashes->editingFinished();
    QCOMPARE(session.document()->find(a)->stroke.dashes, (std::vector<double>{12, 6, 3}));
    dashes->setText(QString());
    emit dashes->editingFinished();
    QVERIFY(session.document()->find(a)->stroke.dashes.empty());
    // The other settings stay as they were.
    QCOMPARE(session.document()->find(a)->stroke.width, 4.5);
    QCOMPARE(session.document()->find(a)->stroke.cap, Qt::RoundCap);
}

void PropertiesPanelTests::anOpacityDragIsOneStep()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    auto *slider = panel.findChild<QSlider *>("opacitySlider");
    QVERIFY(slider->isEnabled());
    slider->setSliderDown(true);
    slider->setValue(500);
    slider->setValue(250);
    slider->setSliderDown(false);
    QCOMPARE(session.document()->find(box)->opacity, 0.25);
    QCOMPARE(panel.findChild<QLineEdit *>("opacityPercent")->text(), QString("25"));
    QCOMPARE(session.undoName(), QString("Opacity"));
    session.undo();
    QCOMPARE(session.document()->find(box)->opacity, 1.0);
    QCOMPARE(session.undoName(), QString("Draw Box"));
    // The percent field types an exact value.
    type(panel, "opacityPercent", "40");
    QCOMPARE(session.document()->find(box)->opacity, 0.4);
    session.deselectAll();
    QVERIFY(!slider->isEnabled());
}

void PropertiesPanelTests::blendModeApplies()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    auto *blend = panel.findChild<QComboBox *>("blendMode");
    QCOMPARE(blend->currentText(), QString("Normal"));
    choose(panel, "blendMode", blend->findText(rawValue(LayerBlendMode::multiply)));
    QCOMPARE(session.document()->find(box)->blendMode, LayerBlendMode::multiply);
    session.deselectAll();
    QVERIFY(!blend->isEnabled());
    QCOMPARE(blend->currentText(), QString("Normal"));
}

void PropertiesPanelTests::alignAndPathfinderFollowTheirGates()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(0, 0, 50, 50)), QStringLiteral("A"));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(25, 10, 50, 50)), QStringLiteral("B"));
    session.deselectAll();
    PropertiesPanel panel(session);
    auto *left = panel.findChild<QToolButton *>("alignLeft");
    auto *distribute = panel.findChild<QToolButton *>("distributeHorizontal");
    auto *unite = panel.findChild<QToolButton *>("unite");
    QVERIFY(!left->isEnabled() && !distribute->isEnabled() && !unite->isEnabled());
    session.select({a, b});
    QVERIFY(left->isEnabled() && unite->isEnabled());
    // Distribute needs three.
    QVERIFY(!distribute->isEnabled());
    panel.findChild<QToolButton *>("alignTop")->click();
    QCOMPARE(bounds(session, b).top(), 0.0);
    // To the artboard, as the menu says.
    choose(panel, "alignTarget", 1);
    panel.findChild<QToolButton *>("alignRight")->click();
    QCOMPARE(bounds(session, a).right(), 400.0);
    QCOMPARE(bounds(session, b).right(), 400.0);
    session.undo();
    session.undo();
    unite->click();
    QCOMPARE(session.undoName(), QString("Unite"));
    QCOMPARE(session.document()->children(session.activeLayer().value()).size(), size_t(1));
    QCOMPARE(session.selectionBounds(), QRectF(0, 0, 75, 60));
    QVERIFY(!unite->isEnabled());
}

void PropertiesPanelTests::swapAndDefaultButtons()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    session.setFillOfSelection(Paint::solid(Qt::red));
    PropertiesPanel panel(session);
    panel.findChild<QPushButton *>("swapFillStrokeButton")->click();
    QCOMPARE(session.document()->find(box)->fill, Paint::solid(Qt::black));
    QCOMPARE(session.document()->find(box)->stroke.paint, Paint::solid(Qt::red));
    panel.findChild<QPushButton *>("defaultFillStrokeButton")->click();
    QCOMPARE(session.document()->find(box)->fill, Paint::solid(Qt::white));
    QCOMPARE(session.document()->find(box)->stroke, StrokeStyle());
}

QTEST_MAIN(PropertiesPanelTests)
#include "PropertiesPanelTests.moc"
