#include "Document/PathOperations.h"
#include "UI/NumberField.h"
#include "Canvas/EditorCanvas.h"
#include "UI/CharacterSection.h"
#include "UI/ColorPaletteControls.h"
#include "UI/PropertiesPanel.h"
#include <QAction>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QFontDatabase>
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
    void cleanup();
    void transformFieldsMoveAndScaleFromTheTopLeft();
    void rotationTurnsThenReadsZero();
    void withoutSelectionTheArtboardShows();
    void theDocumentSectionSwitchesAnArtboardSExportOffAndOn();
    void fillKindKeepsTheColour();
    void strokeSettingsApplyToTheSelection();
    void anOpacityDragIsOneStep();
    void blendModeApplies();
    void alignAndPathfinderFollowTheirGates();
    void swapAndDefaultButtons();
    void theReferencePointIsWhatXYReadAndWHKeep();
    void theLinkKeepsProportions();
    void scaleStrokesIsAnOption();
    void scaleCornersIsAnOption();
    void relativeInputAppliesToEachObject();
    void aLabelScrubIsOneUndoStep();
    void mixedValuesReadMixed();
    void sectionsFoldAndRememberIt();
    void sectionsFollowTheSelection();
    void iconButtonsSayWhatTheyDo();
    void theDocumentShowsWithNothingSelected();
    void characterShowsForSelectedText();
    void characterFieldsAreOneNamedStepEach();
    void characterShowMoreIsRemembered();
    void characterReadsMixedAcrossTexts();
    void areaTextResizesItsBoxNotItsGlyphs();
    void theLayoutSectionDrivesAutoLayout();
    void paddingCanBeSetPerSide();
    void inferredUnevenPaddingShowsEverySide();

};

void PropertiesPanelTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

void PropertiesPanelTests::cleanup()
{
    QSettings().clear();
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
    // About the centre reference point, the centre stays put.
    panel.findChild<ReferencePointPicker *>("referencePoint")->setPoint(4);
    type(panel, "transformRotationField", "90");
    const QRectF turned = bounds(session, box);
    QVERIFY(std::abs(turned.width() - 50) < 1e-6 && std::abs(turned.height() - 100) < 1e-6);
    QVERIFY(std::abs(turned.center().x() - 50) < 1e-6 && std::abs(turned.center().y() - 25) < 1e-6);
    QCOMPARE(session.undoName(), QString("Rotate"));
    QCOMPARE(panel.findChild<QLineEdit *>("transformRotationField")->text(), QString("0"));
    // About the top-left one, that corner stays put.
    session.undo();
    panel.findChild<ReferencePointPicker *>("referencePoint")->setPoint(0);
    type(panel, "transformRotationField", "90");
    const QRectF corner = bounds(session, box);
    QVERIFY(std::abs(corner.left() - 0) < 1e-6 && std::abs(corner.bottom() - 0) < 1e-6);
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

void PropertiesPanelTests::theDocumentSectionSwitchesAnArtboardSExportOffAndOn()
{
    EditorSession session;
    PropertiesPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    session.createDocument(QSizeF(400, 300));
    session.addArtboard(QRectF(500, 0, 400, 300));
    auto *box = panel.findChild<QCheckBox *>("artboardExported");
    QVERIFY(box && box->isChecked());
    box->click();
    QVERIFY(!session.document()->artboard(1).exported);
    QVERIFY(session.document()->artboard(0).exported);
    QCOMPARE(session.undoName(), QString("Don’t Export Artboard"));
    // The box follows the active artboard.
    session.setActiveArtboard(0);
    QVERIFY(box->isChecked());
    session.setActiveArtboard(1);
    QVERIFY(!box->isChecked());
    session.undo();
    QVERIFY(box->isChecked());
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
    panel.findChild<QAbstractButton *>("swapFillStrokeButton")->click();
    QCOMPARE(session.document()->find(box)->fill, Paint::solid(Qt::black));
    QCOMPARE(session.document()->find(box)->stroke.paint, Paint::solid(Qt::red));
    panel.findChild<QAbstractButton *>("defaultFillStrokeButton")->click();
    QCOMPARE(session.document()->find(box)->fill, Paint::solid(Qt::white));
    QCOMPARE(session.document()->find(box)->stroke, StrokeStyle());
}


namespace {
QUuid textAt(EditorSession &session, QPointF at, const QString &words)
{
    return session.addObject(session.textObject(at, words), QStringLiteral("Type"));
}

// A press, a drag of `dx` pixels and a release on a field's label.
void scrub(QWidget *handle, int dx, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPointF start(4, 4);
    const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, handle->mapToGlobal(at), Qt::LeftButton, buttons, modifiers);
        QCoreApplication::sendEvent(handle, &event);
    };
    send(QEvent::MouseButtonPress, start, Qt::LeftButton);
    for (int step = 1; step <= 4; ++step)
        send(QEvent::MouseMove, start + QPointF(dx * step / 4.0, 0), Qt::LeftButton);
    send(QEvent::MouseButtonRelease, start + QPointF(dx, 0), Qt::NoButton);
}

NumberField *numberNamed(QWidget &root, const QString &name)
{
    return root.findChild<NumberField *>(name);
}
}

void PropertiesPanelTests::theReferencePointIsWhatXYReadAndWHKeep()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    auto *picker = panel.findChild<ReferencePointPicker *>("referencePoint");
    picker->setPoint(4);
    QCOMPARE(panel.findChild<QLineEdit *>("transformXField")->text(), QString("60"));
    QCOMPARE(panel.findChild<QLineEdit *>("transformYField")->text(), QString("45"));
    // With the centre reference, W keeps the centre in place.
    type(panel, "transformWField", "200");
    QCOMPARE(bounds(session, box), QRectF(-40, 20, 200, 50));
    // X moves that point to the value.
    type(panel, "transformXField", "100");
    QCOMPARE(bounds(session, box).center().x(), 100.0);
    // Bottom right: H grows upwards.
    picker->setPoint(8);
    type(panel, "transformHField", "100");
    QCOMPARE(bounds(session, box).bottom(), 70.0);
    QCOMPARE(bounds(session, box).height(), 100.0);
    // The choice is remembered.
    PropertiesPanel second(session);
    QCOMPARE(second.findChild<ReferencePointPicker *>("referencePoint")->point(), 8);
}

void PropertiesPanelTests::theLinkKeepsProportions()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 100, 40)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    auto *link = panel.findChild<QToolButton *>("transformLink");
    QVERIFY(link && link->isCheckable() && !link->isChecked());
    link->click();
    QVERIFY(link->isChecked());
    type(panel, "transformWField", "200");
    QCOMPARE(bounds(session, box), QRectF(0, 0, 200, 80));
    type(panel, "transformHField", "40");
    QCOMPARE(bounds(session, box), QRectF(0, 0, 100, 40));
    QVERIFY(QSettings().value("properties/constrainProportions").toBool());
}

void PropertiesPanelTests::scaleStrokesIsAnOption()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(0, 0, 100, 100)), QStringLiteral("Box"));
    StrokeStyle stroke;
    stroke.width = 4;
    session.setStrokeOfSelection(stroke);
    PropertiesPanel panel(session);
    auto *option = panel.findChild<QAction *>("scaleStrokes");
    QVERIFY(option && option->isCheckable() && !option->isChecked());
    // Off: the stroke keeps its weight through a 200 % scale.
    type(panel, "transformWField", "*2");
    QCOMPARE(bounds(session, box).width(), 200.0);
    QCOMPARE(session.document()->find(box)->stroke.width, 4.0);
    session.undo();
    option->trigger();
    QVERIFY(session.scaleStrokes);
    type(panel, "transformWField", "200");
    type(panel, "transformHField", "200");
    QVERIFY(std::abs(session.document()->find(box)->stroke.width - 8) < 1e-9);
}

void PropertiesPanelTests::scaleCornersIsAnOption()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    LiveRectangle shape;
    shape.rect = QRectF(0, 0, 100, 60);
    shape.radii.fill(10);
    VectorObject rectangle;
    rectangle.kind = ObjectKind::path;
    rectangle.fill = Paint::solid(Qt::black);
    EditorSession::reshape(rectangle, shape);
    const QUuid box = session.addObject(rectangle, QStringLiteral("Draw Rectangle"));
    PropertiesPanel panel(session);
    auto *option = panel.findChild<QAction *>("scaleCorners");
    QVERIFY(option && option->isCheckable() && option->isChecked());
    // Off: a uniform scale leaves the live rectangle's radii alone.
    option->trigger();
    QVERIFY(!session.scaleCorners);
    type(panel, "transformWField", "200");
    type(panel, "transformHField", "120");
    const LiveRectangle *scaledOff = session.document()->find(box)->liveShape();
    QVERIFY(scaledOff);
    QCOMPARE(scaledOff->radii[0], 10.0);
    session.undo();
    session.undo();
    option->trigger();
    QVERIFY(session.scaleCorners);
    type(panel, "transformWField", "200");
    type(panel, "transformHField", "120");
    const LiveRectangle *scaledOn = session.document()->find(box)->liveShape();
    QVERIFY(scaledOn);
    QVERIFY(std::abs(scaledOn->radii[0] - 20.0) < 1e-6);
}

void PropertiesPanelTests::relativeInputAppliesToEachObject()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("A"));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(50, 20, 20, 10)), QStringLiteral("B"));
    const QUuid c = session.addPath(Shapes::rectangle(QRectF(100, 40, 40, 10)), QStringLiteral("C"));
    session.select({a, b, c});
    PropertiesPanel panel(session);
    type(panel, "transformXField", "+10");
    QCOMPARE(bounds(session, a).left(), 10.0);
    QCOMPARE(bounds(session, b).left(), 60.0);
    QCOMPARE(bounds(session, c).left(), 110.0);
    QCOMPARE(session.undoName(), QString("Move"));
    // Each doubles its own width about its own reference point.
    type(panel, "transformWField", "*2");
    QCOMPARE(bounds(session, a), QRectF(10, 0, 20, 10));
    QCOMPARE(bounds(session, b), QRectF(60, 20, 40, 10));
    QCOMPARE(bounds(session, c), QRectF(110, 40, 80, 10));
    // One step for all three.
    session.undo();
    QCOMPARE(bounds(session, c), QRectF(110, 40, 40, 10));
    // Math and units work on the whole selection.
    type(panel, "transformYField", "1in - 2pt");
    QCOMPARE(session.selectionBounds().top(), 70.0);
}

void PropertiesPanelTests::aLabelScrubIsOneUndoStep()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid box = session.addPath(Shapes::rectangle(QRectF(10, 20, 100, 50)), QStringLiteral("Box"));
    PropertiesPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    NumberField *x = numberNamed(panel, "transformX");
    QVERIFY(x->handle() && x->handle()->text() == "X");
    const bool couldUndo = session.canUndo();
    scrub(x->handle(), 20);
    QCOMPARE(bounds(session, box).left(), 30.0);
    QCOMPARE(session.undoName(), QString("Move"));
    // Shift is ten times as far, Alt a tenth.
    scrub(x->handle(), 20, Qt::ShiftModifier);
    QCOMPARE(bounds(session, box).left(), 230.0);
    scrub(x->handle(), -20, Qt::AltModifier);
    QVERIFY(std::abs(bounds(session, box).left() - 228) < 1e-9);
    session.undo();
    session.undo();
    session.undo();
    QCOMPARE(bounds(session, box).left(), 10.0);
    QCOMPARE(session.canUndo(), couldUndo);
}

void PropertiesPanelTests::mixedValuesReadMixed()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("A"));
    session.setFillOfSelection(Paint::solid(Qt::red));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(20, 0, 10, 10)), QStringLiteral("B"));
    session.setFillOfSelection(Paint::solid(Qt::blue));
    StrokeStyle thick;
    thick.width = 5;
    session.setStrokeOfSelection(thick);
    session.select({a, b});
    PropertiesPanel panel(session);
    auto *well = panel.findChild<PaintSwatch *>("fillWell");
    QVERIFY(well->isMixed());
    QCOMPARE(panel.findChild<QComboBox *>("fillKind")->currentIndex(), -1);
    QCOMPARE(panel.findChild<QComboBox *>("fillKind")->placeholderText(), QString("Mixed"));
    auto *weight = panel.findChild<QLineEdit *>("strokeWidth");
    QVERIFY(weight->text().isEmpty());
    QCOMPARE(weight->placeholderText(), QString("Mixed"));
    // A step on a mixed weight adds to each one's own.
    QTest::keyClick(weight, Qt::Key_Up);
    QCOMPARE(session.document()->find(a)->stroke.width, 1.5);
    QCOMPARE(session.document()->find(b)->stroke.width, 5.5);
    // A value applies to both, and they read as one again.
    weight->setText("2");
    QTest::keyClick(weight, Qt::Key_Return);
    QCOMPARE(session.document()->find(a)->stroke.width, 2.0);
    QCOMPARE(weight->text(), QString("2"));
    session.setFillOfSelection(Paint::solid(Qt::green));
    QVERIFY(!well->isMixed());
}

void PropertiesPanelTests::sectionsFoldAndRememberIt()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("Box"));
    {
        PropertiesPanel panel(session);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *stroke = panel.findChild<PanelSection *>("strokeSection");
        QVERIFY(!stroke->isCollapsed());
        QVERIFY(panel.findChild<QWidget *>("strokeCap")->isVisible());
        stroke->toggle()->click();
        QVERIFY(stroke->isCollapsed());
        QVERIFY(!panel.findChild<QWidget *>("strokeCap")->isVisible());
    }
    PropertiesPanel again(session);
    QVERIFY(again.findChild<PanelSection *>("strokeSection")->isCollapsed());
    again.findChild<PanelSection *>("strokeSection")->toggle()->click();
    QVERIFY(!QSettings().value(PanelSection::settingsKey("stroke")).toBool());
}

void PropertiesPanelTests::sectionsFollowTheSelection()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(0, 0, 10, 10)), QStringLiteral("A"));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(20, 0, 10, 10)), QStringLiteral("B"));
    PropertiesPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const auto shown = [&](const char *name) { return panel.findChild<QWidget *>(name)->isVisible(); };
    session.select({a});
    QVERIFY(shown("alignSection") && !shown("pathfinderSection") && !shown("characterSection"));
    session.select({a, b});
    QVERIFY(shown("pathfinderSection"));
    session.deselectAll();
    QVERIFY(!shown("alignSection") && !shown("transformSection") && shown("artboardSection"));
    // The Type tool shows Character for the next text, with nothing selected.
    session.selectTool(Tool::text);
    QVERIFY(shown("characterSection"));
}

void PropertiesPanelTests::iconButtonsSayWhatTheyDo()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    PropertiesPanel panel(session);
    QVERIFY(panel.findChild<QToolButton *>("unite")->toolTip().startsWith("Unite (Pathfinder)"));
    QCOMPARE(panel.findChild<QToolButton *>("unite")->accessibleName(), QString("Unite"));
    QCOMPARE(panel.findChild<QToolButton *>("alignLeft")->toolTip(), QString("Align left edges"));
    QVERIFY(panel.findChild<QToolButton *>("distributeHorizontal")->toolTip().startsWith("Distribute horizontal centers"));
    for (QToolButton *button : panel.findChildren<QToolButton *>())
        QVERIFY2(!button->toolTip().isEmpty(), qPrintable(button->objectName()));
    // The rotation field is labelled by name for tooltips and screen readers.
    QCOMPARE(panel.findChild<QLineEdit *>("transformRotationField")->accessibleName(), QString("Rotation"));
    QVERIFY(!numberNamed(panel, "transformRotation")->handle()->pixmap().isNull());
    // Menus, number fields' boxes and icon buttons share one height (docs/PANELS.md).
    for (const char *name : {"strokeCap", "alignTarget", "fillKind", "transformLink"})
        QCOMPARE(panel.findChild<QWidget *>(name)->height(), NumberField::fieldHeight);
    QCOMPARE(panel.findChild<QWidget *>("strokeWidth")->parentWidget()->height(), NumberField::fieldHeight);
    // Folded, a section keeps a summary of what's inside; Align and Pathfinder start folded.
    auto *stroke = panel.findChild<PanelSection *>("strokeSection");
    stroke->setCollapsed(true);
    QVERIFY(stroke->summaryText().contains(QStringLiteral(" pt · ")));
    stroke->setCollapsed(false);
    QVERIFY(stroke->summaryText().isEmpty());
}

void PropertiesPanelTests::theDocumentShowsWithNothingSelected()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    PropertiesPanel panel(session);
    auto *grid = panel.findChild<QCheckBox *>("documentShowGrid");
    grid->click();
    QVERIFY(session.showsGrid);
    panel.findChild<QCheckBox *>("documentSnapToGrid")->click();
    QVERIFY(session.snapsToGrid);
    session.setShowsOutline(true);
    QVERIFY(panel.findChild<QCheckBox *>("documentOutline")->isChecked());
    type(panel, "documentNudgeField", "0.5");
    QCOMPARE(EditorCanvas::keyboardIncrement(), 0.5);
    QVERIFY(panel.findChild<QPushButton *>("documentFit"));
    QVERIFY(panel.findChild<QPushButton *>("documentExport")->menu()->actions().size() == 4);
}

void PropertiesPanelTests::characterShowsForSelectedText()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid text = textAt(session, QPointF(20, 100), "Hello");
    PropertiesPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QCOMPARE(session.tool(), Tool::select);
    QVERIFY(panel.findChild<QWidget *>("characterSection")->isVisible());
    const TextContent &shown = session.document()->find(text)->text;
    QCOMPARE(panel.findChild<QLineEdit *>("characterSizeField")->text(), NumberField::formatted(shown.size));
    QCOMPARE(panel.findChild<QLineEdit *>("characterTrackingField")->text(), QString("0"));
    // Auto leading is blank, with its value in the placeholder.
    QVERIFY(panel.findChild<QLineEdit *>("characterLeadingField")->text().isEmpty());
    QVERIFY(panel.findChild<QLineEdit *>("characterLeadingField")->placeholderText().startsWith("Auto"));
    // The style list is the family's real faces.
    auto *style = panel.findChild<QComboBox *>("characterStyle");
    const QStringList faces = QFontDatabase::styles(shown.family);
    for (const QString &face : faces)
        QVERIFY(style->findText(face) >= 0);
    QVERIFY(panel.findChild<QToolButton *>("characterAlignLeft")->isChecked());
}

void PropertiesPanelTests::characterFieldsAreOneNamedStepEach()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid id = textAt(session, QPointF(20, 100), "Hello");
    PropertiesPanel panel(session);
    const auto text = [&] { return session.document()->find(id)->text; };
    type(panel, "characterTrackingField", "50");
    QCOMPARE(text().tracking, 50.0);
    QCOMPARE(session.undoName(), QString("Tracking"));
    type(panel, "characterLeadingField", "30");
    QCOMPARE(text().leading, std::optional<double>(30));
    QCOMPARE(session.undoName(), QString("Leading"));
    // Emptied, leading is Auto again.
    type(panel, "characterLeadingField", "");
    QVERIFY(!text().leading.has_value());
    type(panel, "characterSizeField", "36");
    QCOMPARE(text().size, 36.0);
    QCOMPARE(session.undoName(), QString("Font Size"));
    type(panel, "characterBaselineShiftField", "4");
    QCOMPARE(text().baselineShift, 4.0);
    QCOMPARE(session.undoName(), QString("Baseline Shift"));
    type(panel, "characterHorizontalScaleField", "80");
    QCOMPARE(text().horizontalScale, 80.0);
    type(panel, "characterVerticalScaleField", "120");
    QCOMPARE(text().verticalScale, 120.0);
    QCOMPARE(session.undoName(), QString("Vertical Scale"));
    choose(panel, "characterKerning", 1);
    QCOMPARE(text().kerning, TextKerning::none);
    QCOMPARE(session.undoName(), QString("Kerning"));
    choose(panel, "characterCase", 1);
    QCOMPARE(text().textCase, TextCase::allCaps);
    QCOMPARE(session.undoName(), QString("Case"));
    panel.findChild<QToolButton *>("characterUnderline")->click();
    QVERIFY(text().underline);
    QCOMPARE(session.undoName(), QString("Underline"));
    panel.findChild<QToolButton *>("characterStrikethrough")->click();
    QVERIFY(text().strikethrough);
    panel.findChild<QToolButton *>("characterJustifyAll")->click();
    QCOMPARE(text().alignment, TextAlignment::justifyAll);
    QCOMPARE(session.undoName(), QString("Alignment"));
    const QStringList faces = QFontDatabase::styles(text().family);
    if (faces.size() > 1) {
        auto *style = panel.findChild<QComboBox *>("characterStyle");
        const int last = style->findText(faces.last());
        choose(panel, "characterStyle", last);
        QCOMPARE(text().style, faces.last());
        QCOMPARE(session.undoName(), QString("Font Style"));
    }
    // Area type from the Kind menu, one step.
    choose(panel, "characterKind", 1);
    QVERIFY(text().area.has_value());
    QCOMPARE(session.undoName(), QString("Convert to Area Type"));
}

void PropertiesPanelTests::characterShowMoreIsRemembered()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    textAt(session, QPointF(20, 100), "Hello");
    {
        PropertiesPanel panel(session);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *more = panel.findChild<QPushButton *>("characterMore");
        QCOMPARE(more->text(), QString("Show more"));
        QVERIFY(!panel.findChild<QWidget *>("characterExtra")->isVisible());
        more->click();
        QVERIFY(panel.findChild<QWidget *>("characterExtra")->isVisible());
        QCOMPARE(more->text(), QString("Show less"));
    }
    PropertiesPanel again(session);
    QVERIFY(again.findChild<CharacterSection *>()->showsMore());
}

void PropertiesPanelTests::characterReadsMixedAcrossTexts()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = textAt(session, QPointF(20, 100), "One");
    const QUuid b = textAt(session, QPointF(20, 200), "Two");
    session.select({b});
    session.updateText([](TextContent &text) { text.tracking = 100; }, "Tracking");
    session.select({a, b});
    PropertiesPanel panel(session);
    auto *tracking = panel.findChild<QLineEdit *>("characterTrackingField");
    QVERIFY(tracking->text().isEmpty());
    QCOMPARE(tracking->placeholderText(), QString("Mixed"));
    // "+20" adds to each text's own tracking, in one step.
    type(panel, "characterTrackingField", "+20");
    QCOMPARE(session.document()->find(a)->text.tracking, 20.0);
    QCOMPARE(session.document()->find(b)->text.tracking, 120.0);
    session.undo();
    QCOMPARE(session.document()->find(a)->text.tracking, 0.0);
    QCOMPARE(session.document()->find(b)->text.tracking, 100.0);
    // A plain value makes them one.
    type(panel, "characterTrackingField", "10");
    QCOMPARE(tracking->text(), QString("10"));
}

void PropertiesPanelTests::areaTextResizesItsBoxNotItsGlyphs()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    VectorObject box = session.textObject(QPointF(0, 0), "A few words that wrap");
    box.text.area = QSizeF(200, 0);
    box.transform = QTransform::fromTranslate(10, 10);
    const QUuid id = session.addObject(box, "Type");
    PropertiesPanel panel(session);
    type(panel, "transformWField", "80");
    const VectorObject *after = session.document()->find(id);
    QCOMPARE(after->text.area->width(), 80.0);
    QCOMPARE(after->text.size, box.text.size);
    QVERIFY(after->transform.type() <= QTransform::TxTranslate);
    QCOMPARE(bounds(session, id).left(), 10.0);
}

// Frames and auto layout: Add, the flow, gap, padding and alignment, sizing and Absolute.
void PropertiesPanelTests::theLayoutSectionDrivesAutoLayout()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(20, 20, 40, 30)), QStringLiteral("A"));
    const QUuid b = session.addPath(Shapes::rectangle(QRectF(80, 20, 20, 50)), QStringLiteral("B"));
    session.select({a, b});
    session.frameSelection();
    const QUuid frame = session.selection().front();
    PropertiesPanel panel(session);
    panel.resize(300, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const auto shown = [&](const char *name) { return panel.findChild<QWidget *>(name)->isVisible(); };
    QVERIFY(shown("layoutSection") && shown("layoutAdd") && !shown("layoutGap") && shown("layoutClip"));
    panel.findChild<QPushButton *>("layoutAdd")->click();
    QVERIFY(session.document()->find(frame)->autoLayout);
    QVERIFY(shown("layoutGap") && !shown("layoutAdd"));
    type(panel, "layoutGapField", "4");
    QCOMPARE(session.document()->find(frame)->autoLayout->gap, 4.0);
    QCOMPARE(bounds(session, b).left(), 64.0);
    choose(panel, "layoutFlow", 1);
    QCOMPARE(session.document()->find(frame)->autoLayout->direction, LayoutDirection::vertical);
    QCOMPARE(bounds(session, b).top(), 54.0);
    // Bottom right of the grid: for a column, the end along it and across it.
    panel.findChild<QToolButton *>("layoutAlign8")->click();
    QCOMPARE(session.document()->find(frame)->autoLayout->primary, LayoutAlign::end);
    QCOMPARE(session.document()->find(frame)->autoLayout->counter, LayoutAlign::end);
    // A child: Fill width, then Absolute.
    session.select({b});
    QVERIFY(shown("layoutSection") && shown("layoutAbsolute") && !shown("layoutClip") && !shown("layoutGap"));
    choose(panel, "layoutWidth", 2);
    QCOMPARE(session.document()->find(b)->layout.width, LayoutSizing::fill);
    QCOMPARE(bounds(session, b).width(), 40.0);
    panel.findChild<QCheckBox *>("layoutAbsolute")->click();
    QVERIFY(session.document()->find(b)->layout.absolute);
    if (const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB"); !grab.isEmpty()) {
        session.select({frame});
        QTest::qWait(100);
        panel.grab().save(QString::fromLocal8Bit(grab) + QStringLiteral("/layout-panel.png"));
    }
}

void PropertiesPanelTests::paddingCanBeSetPerSide()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(20, 20, 40, 30)), QStringLiteral("A"));
    session.select({a});
    session.frameSelection();
    const QUuid frame = session.selection().front();
    session.addAutoLayout();
    PropertiesPanel panel(session);
    panel.resize(300, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    const auto shown = [&](const char *name) { return panel.findChild<QWidget *>(name)->isVisible(); };
    const auto padding = [&] { return *session.document()->find(frame)->autoLayout; };
    auto *toggle = panel.findChild<QToolButton *>("layoutPaddingSides");
    QVERIFY(shown("layoutPaddingX") && shown("layoutPaddingY") && !shown("layoutPaddingLeft") && !toggle->isChecked());
    // The toggle opens a field per side, showing what the pair did.
    toggle->click();
    QVERIFY(!shown("layoutPaddingX") && shown("layoutPaddingLeft") && shown("layoutPaddingTop")
            && shown("layoutPaddingRight") && shown("layoutPaddingBottom"));
    QCOMPARE(numberNamed(panel, "layoutPaddingRight")->value(), padding().paddingRight);
    // Each side is its own edit.
    type(panel, "layoutPaddingLeftField", "3");
    type(panel, "layoutPaddingTopField", "5");
    type(panel, "layoutPaddingRightField", "7");
    type(panel, "layoutPaddingBottomField", "11");
    QCOMPARE(padding().paddingLeft, 3.0);
    QCOMPARE(padding().paddingTop, 5.0);
    QCOMPARE(padding().paddingRight, 7.0);
    QCOMPARE(padding().paddingBottom, 11.0);
    QCOMPARE(session.undoName(), QString("Padding"));
    // Uneven sides stay open, and the pair reads Mixed.
    toggle->click();
    QVERIFY(toggle->isChecked() && shown("layoutPaddingLeft"));
    if (const QByteArray grab = qgetenv("OMASTRATOR_TEST_GRAB"); !grab.isEmpty()) {
        QTest::qWait(100);
        panel.grab().save(QString::fromLocal8Bit(grab) + QStringLiteral("/layout-padding-sides.png"));
    }
    // A scrub passes through many values and undoes in one step.
    const std::vector<QString> before = session.undoNames();
    scrub(numberNamed(panel, "layoutPaddingBottom")->handle(), 20);
    QCOMPARE(padding().paddingBottom, 31.0);
    QCOMPARE(padding().paddingTop, 5.0);
    QCOMPARE(session.undoNames().size(), before.size() + 1);
    session.undo();
    QCOMPARE(padding().paddingBottom, 11.0);
    // Equal pairs let the toggle go back to the pair.
    type(panel, "layoutPaddingRightField", "3");
    type(panel, "layoutPaddingBottomField", "5");
    toggle->click();
    QVERIFY(shown("layoutPaddingX") && !shown("layoutPaddingLeft"));
    QCOMPARE(numberNamed(panel, "layoutPaddingX")->value(), 3.0);
    QCOMPARE(numberNamed(panel, "layoutPaddingY")->value(), 5.0);
    // A pair scrub is one step as well.
    const size_t steps = session.undoNames().size();
    scrub(numberNamed(panel, "layoutPaddingX")->handle(), 10);
    QCOMPARE(padding().paddingLeft, 13.0);
    QCOMPARE(padding().paddingRight, 13.0);
    QCOMPARE(session.undoNames().size(), steps + 1);
}

// What Add Auto Layout inferred on the author's canvas: the pair read "Mixed" and hid why children sat off-centre.
void PropertiesPanelTests::inferredUnevenPaddingShowsEverySide()
{
    EditorSession session;
    session.createDocument(QSizeF(400, 300));
    const QUuid a = session.addPath(Shapes::rectangle(QRectF(20, 20, 40, 30)), QStringLiteral("A"));
    session.select({a});
    session.frameSelection();
    session.addAutoLayout();
    AutoLayout layout = *session.selectedAutoLayout();
    layout.paddingLeft = 10;
    layout.paddingTop = 10;
    layout.paddingRight = 316;
    layout.paddingBottom = 118;
    session.setAutoLayout(layout, QStringLiteral("Padding"));
    PropertiesPanel panel(session);
    panel.resize(300, 900);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QVERIFY(panel.findChild<QToolButton *>("layoutPaddingSides")->isChecked());
    QVERIFY(panel.findChild<QWidget *>("layoutPaddingLeft")->isVisible() && !panel.findChild<QWidget *>("layoutPaddingX")->isVisible());
    const std::array<std::pair<const char *, double>, 4> sides{{{"layoutPaddingLeft", 10}, {"layoutPaddingTop", 10},
                                                                {"layoutPaddingRight", 316}, {"layoutPaddingBottom", 118}}};
    for (const auto &[name, value] : sides) {
        QVERIFY(!numberNamed(panel, name)->isMixed());
        QCOMPARE(numberNamed(panel, name)->value(), value);
        QCOMPARE(numberNamed(panel, name)->field->text(), NumberField::formatted(value));
    }
}

QTEST_MAIN(PropertiesPanelTests)
#include "PropertiesPanelTests.moc"
