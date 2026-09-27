#include "ContentView.h"
#include "Document/PathOperations.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/NewDocumentSheet.h"
#include "UI/NumberField.h"
#include "UI/ToolHeaders.h"
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QtTest>

// One document's editor: rail, bars, welcome, keys, dock and status.
namespace {
template <typename Widget> Widget &find(QWidget &root, const QString &name)
{
    Widget *found = root.findChild<Widget *>(name);
    if (!found)
        throw std::runtime_error("no widget named " + name.toStdString());
    return *found;
}

struct Editor {
    EditorSession session;
    ContentView view{session};
    explicit Editor(bool drawn = true)
    {
        if (drawn)
            session.createDocument(QSizeF(400, 300));
        view.resize(1100, 800);
        view.show();
        if (!QTest::qWaitForWindowActive(&view))
            throw std::runtime_error("the editor never became active");
    }
    QToolButton &tool(Tool tool) { return find<QToolButton>(view, QStringLiteral("tool:") + rawValue(tool)); }
    QString status(const char *name) { return find<QLabel>(view, QString::fromLatin1(name)).text(); }
    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::keyClick(&view.canvas(), key, modifiers); }
};
}

class ContentViewTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void theRailHoldsEveryToolInGroups();
    void aRailClickPicksTheTool();
    void aFlyoutOpensOnRightClickAndLongPressAndAltClickCycles();
    void basicPresetHidesTheAdvancedTools();
    void eachToolShowsItsBar();
    void shapeBarsEditTheSessionsNumbers();
    void shapeBuilderFoldsItsOptions();
    void typeBarsStyleSelectedTextInOneStep();
    void theWelcomeShowsWithoutADocument();
    void theStatusBarFollowsTheSession();
    void canvasKeysPickToolsAndSwapColours();
    void remappedKeysReachTheCanvasAsTheirOriginals();
    void theDockFollowsItsSettings();
    void theDockSplitIsRememberedAndResets();
};

void ContentViewTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ContentViewTests::cleanup()
{
    QSettings().clear();
    ShortcutSettings::shared().reload();
}

void ContentViewTests::theRailHoldsEveryToolInGroups()
{
    Editor editor;
    // Every tool lives in exactly one slot, named after the first tool in its group.
    size_t count = 0;
    for (const std::vector<Tool> &slot : ContentView::toolSlots())
        count += slot.size();
    QCOMPARE(count, allTools.size());
    for (const std::vector<Tool> &slot : ContentView::toolSlots())
        QVERIFY(editor.tool(slot.front()).isVisible());
    // Tooltips name the tool a slot shows and its key, if any.
    QCOMPARE(editor.tool(Tool::select).toolTip(), QString("Selection (V)"));
    QCOMPARE(editor.tool(Tool::artboard).toolTip(), QString("Artboard (Shift+O)"));
    // A remapped key shows at once.
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Canvas & Layers:Pen tool"), ShortcutChord("k")}}));
    QCOMPARE(editor.tool(Tool::pen).toolTip(), QString("Pen (K)"));
    // Groups run top to bottom: selection above drawing above navigation.
    QVERIFY(editor.tool(Tool::select).y() < editor.tool(Tool::pen).y());
    QVERIFY(editor.tool(Tool::pen).y() < editor.tool(Tool::rotate).y());
    QVERIFY(editor.tool(Tool::rotate).y() < editor.tool(Tool::hand).y());
}

void ContentViewTests::aRailClickPicksTheTool()
{
    Editor editor;
    QVERIFY(editor.tool(Tool::select).isChecked());
    QTest::mouseClick(&editor.tool(Tool::rectangle), Qt::LeftButton);
    QCOMPARE(editor.session.tool(), Tool::rectangle);
    QVERIFY(editor.tool(Tool::rectangle).isChecked() && !editor.tool(Tool::select).isChecked());
    // The session's choice from elsewhere shows too; Hand and Zoom share a slot.
    editor.session.selectTool(Tool::zoom);
    QVERIFY(editor.tool(Tool::hand).isChecked());
    // A slot with several tools shows the last one picked, even chosen elsewhere.
    editor.session.selectTool(Tool::ellipse);
    QVERIFY(editor.tool(Tool::rectangle).isChecked());
}

void ContentViewTests::aFlyoutOpensOnRightClickAndLongPressAndAltClickCycles()
{
    Editor editor;
    QToolButton &slot = editor.tool(Tool::rectangle);
    // A right-click or a long press opens a real (modal) flyout menu, which offscreen
    // Qt Test runs can't drive reliably; those are exercised by hand. Alt-click cycles
    // between the slot's own tools without one, and remembers the last one chosen.
    QTest::mouseClick(&slot, Qt::LeftButton, Qt::AltModifier);
    const Tool first = editor.session.tool();
    QVERIFY(first != Tool::select);
    QTest::mouseClick(&slot, Qt::LeftButton, Qt::AltModifier);
    QVERIFY(editor.session.tool() != first);
    QCOMPARE(toolNamed(QSettings().value(QStringLiteral("toolSlot/rectangle")).toString()), std::optional(editor.session.tool()));
}

void ContentViewTests::basicPresetHidesTheAdvancedTools()
{
    Editor editor;
    // Shape Builder is alone in its slot and on the Basic-hides list.
    QVERIFY(editor.tool(Tool::shapeBuilder).isVisible());
    ContentView::setToolPreset(ContentView::ToolPreset::basic);
    // Nothing refreshes the rail until the session says something changed.
    editor.session.selectTool(Tool::directSelect);
    editor.session.selectTool(Tool::select);
    // A tool the preset hides: its slot disappears, unless it's the active one.
    QVERIFY(!editor.tool(Tool::shapeBuilder).isVisible());
    editor.session.selectTool(Tool::shapeBuilder);
    QVERIFY(editor.tool(Tool::shapeBuilder).isVisible());
    ContentView::setToolPreset(ContentView::ToolPreset::advanced);
}

void ContentViewTests::eachToolShowsItsBar()
{
    Editor editor;
    const auto bar = [&] { return &find<ToolHeaderBar>(editor.view, "toolHeader"); };
    QCOMPARE(bar()->title->text(), QString("Selection"));
    QCOMPARE(bar()->height(), ToolHeaderStyle::height);
    editor.session.selectTool(Tool::text);
    QVERIFY(qobject_cast<TypeControls *>(bar()));
    editor.session.selectTool(Tool::polygon);
    QVERIFY(qobject_cast<ShapeControls *>(bar()));
    QVERIFY(find<NumberField>(*bar(), "polygonSides").isVisible());
    QVERIFY(!find<NumberField>(*bar(), "cornerRadius").isVisible());
    // Hand and Zoom share one bar, retitled.
    editor.session.selectTool(Tool::hand);
    ToolHeaderBar *hand = bar();
    QCOMPARE(hand->title->text(), QString("Hand"));
    editor.session.selectTool(Tool::zoom);
    QCOMPARE(bar(), hand);
    QCOMPARE(hand->title->text(), QString("Zoom"));
    editor.session.selectTool(Tool::rotate);
    QVERIFY(qobject_cast<TransformToolHeader *>(bar()));
}

void ContentViewTests::shapeBarsEditTheSessionsNumbers()
{
    Editor editor;
    editor.session.selectTool(Tool::star);
    auto &bar = find<ShapeControls>(editor.view, "toolHeader");
    NumberField &points = find<NumberField>(bar, "starPoints");
    QCOMPARE(points.field->text(), QString("5"));
    points.field->setFocus();
    points.field->selectAll();
    QTest::keyClicks(points.field, "8");
    QTest::keyClick(points.field, Qt::Key_Return);
    QCOMPARE(editor.session.starPoints, 8);
    // Arrows step the inner radius by five.
    NumberField &inner = find<NumberField>(bar, "starInnerRatio");
    QCOMPARE(inner.field->text(), QString("50"));
    QTest::keyClick(inner.field, Qt::Key_Up);
    QCOMPARE(editor.session.starInnerRatio, 0.55);
    editor.session.selectTool(Tool::roundedRectangle);
    auto &rounded = find<ShapeControls>(editor.view, "toolHeader");
    NumberField &radius = find<NumberField>(rounded, "cornerRadius");
    QVERIFY(radius.isVisible());
    QTest::keyClick(radius.field, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(editor.session.cornerRadius, 2.0);
    // The stroke weight goes to the defaults without a selection.
    QTest::keyClick(find<NumberField>(rounded, "shapeStrokeWeight").field, Qt::Key_Up);
    QCOMPARE(editor.session.defaultStroke().width, 1.5);
}

void ContentViewTests::shapeBuilderFoldsItsOptions()
{
    Editor editor;
    editor.press(Qt::Key_M, Qt::ShiftModifier);
    QCOMPARE(editor.session.tool(), Tool::shapeBuilder);
    QVERIFY(editor.status("hintStatus").contains("merge"));
    auto &bar = find<ShapeBuilderControls>(editor.view, "toolHeader");
    // Only the colour source shows until Options opens.
    auto &colour = find<QComboBox>(bar, "shapeBuilderColorFrom");
    QVERIFY(colour.isVisible());
    auto &gaps = find<QCheckBox>(bar, "shapeBuilderGapDetection");
    QVERIFY(!gaps.isVisible());
    find<QToolButton>(bar, "shapeBuilderOptions").click();
    QVERIFY(gaps.isVisible());
    gaps.click();
    QVERIFY(editor.session.shapeBuilder.gapDetection);
    QVERIFY(find<NumberField>(bar, "shapeBuilderGap").isEnabled());
    colour.setCurrentIndex(1);
    emit colour.activated(1);
    QVERIFY(!editor.session.shapeBuilder.colorFromArtwork);
    find<QCheckBox>(bar, "shapeBuilderHighlightFill").click();
    QVERIFY(!editor.session.shapeBuilder.highlightFill);
}

void ContentViewTests::typeBarsStyleSelectedTextInOneStep()
{
    Editor editor;
    const QUuid first = editor.session.addText(QPointF(20, 40), QStringLiteral("One"));
    const QUuid second = editor.session.addText(QPointF(20, 90), QStringLiteral("Two"));
    editor.session.select({first, second});
    editor.session.selectTool(Tool::text);
    auto &bar = find<TypeControls>(editor.view, "toolHeader");
    NumberField &size = find<NumberField>(bar, "typeSize");
    QCOMPARE(size.field->text(), QString("24"));
    const QString before = editor.session.undoName();
    QTest::keyClick(size.field, Qt::Key_Up, Qt::ShiftModifier);
    const VectorDocument &document = editor.session.document().value();
    QCOMPARE(document.find(first)->text.size, 34.0);
    QCOMPARE(document.find(second)->text.size, 34.0);
    QCOMPARE(editor.session.undoName(), QString("Font Size"));
    // Style and alignment follow; the next text takes them too.
    auto &style = find<QComboBox>(bar, "typeStyle");
    QVERIFY(style.count() >= 1);
    QCOMPARE(style.currentText(), editor.session.shownText().style);
    find<QToolButton>(bar, "typeAlignCenter").click();
    QCOMPARE(editor.session.document().value().find(second)->text.alignment, TextAlignment::center);
    QCOMPARE(editor.session.document().value().find(first)->text.alignment, TextAlignment::center);
    QCOMPARE(editor.session.defaultText.alignment, TextAlignment::center);
    QVERIFY(find<QToolButton>(bar, "typeAlignCenter").isChecked());
    find<QToolButton>(bar, "typeAlignJustify").click();
    // One undo takes the size back from both.
    editor.session.undo();
    editor.session.undo();
    editor.session.undo();
    QCOMPARE(editor.session.document().value().find(first)->text.size, 24.0);
    QCOMPARE(editor.session.undoName(), before);
}

void ContentViewTests::theWelcomeShowsWithoutADocument()
{
    Editor editor(false);
    auto &sheet = find<NewDocumentSheet>(editor.view, "newDocumentSheet");
    QVERIFY(sheet.isVisible());
    QCOMPARE(editor.status("hintStatus"), QString("Ready when you are"));
    QVERIFY(!find<QLabel>(editor.view, "zoomStatus").isVisible());
    // Create makes the artboard; the welcome goes.
    find<QPushButton>(sheet, "createDocument").click();
    QCOMPARE(editor.session.document().value().size, QSizeF(612, 792));
    QTRY_VERIFY(!editor.view.findChild<NewDocumentSheet *>());
    editor.session.closeDocument();
    QVERIFY(editor.view.findChild<NewDocumentSheet *>("newDocumentSheet"));
}

void ContentViewTests::theStatusBarFollowsTheSession()
{
    Editor editor;
    QCOMPARE(editor.status("artboardStatus"), QString("400 × 300 pt"));
    QCOMPARE(editor.status("selectionStatus"), QString("No selection"));
    QCOMPARE(editor.status("pointerStatus"), QString("X –  Y –"));
    editor.session.addPath(Shapes::rectangle(QRectF(10, 10, 50, 50)), QStringLiteral("A"));
    QCOMPARE(editor.status("selectionStatus"), QString("1 object selected"));
    editor.session.addPath(Shapes::ellipse(QRectF(80, 10, 50, 50)), QStringLiteral("B"));
    editor.session.selectAll();
    QCOMPARE(editor.status("selectionStatus"), QString("2 objects selected"));
    QCOMPARE(editor.status("zoomStatus"), ContentView::percent(editor.session.viewport.zoom()));
    editor.session.actualSize();
    QCOMPARE(editor.status("zoomStatus"), QString("100%"));
    emit editor.view.canvas().pointerMoved(QPointF(12.26, 30));
    QCOMPARE(editor.status("pointerStatus"), QString("X 12.3  Y 30.0 pt"));
    editor.session.selectTool(Tool::pen);
    QCOMPARE(editor.status("hintStatus"), ContentView::hint(Tool::pen));
    QCOMPARE(ContentView::percent(0.8351), QString("83.5%"));
    QCOMPARE(ContentView::percent(12), QString("1,200%"));
}

void ContentViewTests::canvasKeysPickToolsAndSwapColours()
{
    Editor editor;
    editor.view.canvas().setFocus();
    editor.press(Qt::Key_P);
    QCOMPARE(editor.session.tool(), Tool::pen);
    editor.press(Qt::Key_Backslash);
    QCOMPARE(editor.session.tool(), Tool::line);
    editor.press(Qt::Key_V);
    QCOMPARE(editor.session.tool(), Tool::select);
    // X trades fill and stroke; D restores the defaults.
    editor.press(Qt::Key_X);
    QCOMPARE(editor.session.defaultFill(), Paint::solid(Qt::black));
    QCOMPARE(editor.session.defaultStroke().paint, Paint::solid(Qt::white));
    editor.press(Qt::Key_D);
    QCOMPARE(editor.session.defaultFill(), Paint::solid(Qt::white));
    QCOMPARE(editor.session.defaultStroke().paint, Paint::solid(Qt::black));
}

void ContentViewTests::remappedKeysReachTheCanvasAsTheirOriginals()
{
    Editor editor;
    editor.view.canvas().setFocus();
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Canvas & Layers:Pen tool"), ShortcutChord("k")},
                                             {QStringLiteral("Canvas & Layers:Nudge Right"), ShortcutChord("j")}}));
    editor.press(Qt::Key_K);
    QCOMPARE(editor.session.tool(), Tool::pen);
    // The old key no longer picks the tool.
    editor.press(Qt::Key_V);
    editor.press(Qt::Key_P);
    QCOMPARE(editor.session.tool(), Tool::select);
    // A moved nudge arrives at the canvas as Right.
    editor.session.addPath(Shapes::rectangle(QRectF(10, 10, 50, 50)), QStringLiteral("A"));
    editor.press(Qt::Key_J);
    QCOMPARE(editor.session.selectionBounds().left(), 11.0);
    editor.press(Qt::Key_Right);
    QCOMPARE(editor.session.selectionBounds().left(), 11.0);
}

void ContentViewTests::theDockFollowsItsSettings()
{
    Editor editor;
    auto &layers = find<QWidget>(editor.view, "layersPanel");
    auto &properties = find<QWidget>(editor.view, "propertiesPanel");
    auto &dock = find<QWidget>(editor.view, "panelDock");
    QVERIFY(layers.isVisible() && properties.isVisible());
    QCOMPARE(dock.width(), int(ContentView::defaultPanelWidth));
    ContentView::setShowsPanel(ContentView::layersKey, false);
    editor.view.synchronizePanels();
    QVERIFY(!layers.isVisible() && properties.isVisible());
    ContentView::setShowsPanel(ContentView::propertiesKey, false);
    editor.view.synchronizePanels();
    QVERIFY(!dock.isVisible());
    // A stored width out of range is the default.
    ContentView::setPanelWidth(900);
    QTest::ignoreMessage(QtWarningMsg, "panelWidth 900 is out of range, using 264");
    QCOMPARE(ContentView::panelWidth(), ContentView::defaultPanelWidth);
    ContentView::setPanelWidth(300);
    QCOMPARE(ContentView::panelWidth(), 300.0);
}

void ContentViewTests::theDockSplitIsRememberedAndResets()
{
    int moved = 0;
    {
        Editor editor;
        auto &split = find<QSplitter>(editor.view, "panelSplit");
        QSplitterHandle *handle = split.handle(1);
        QVERIFY(handle && handle->isVisible() && handle->height() >= 8);
        QVERIFY(!handle->toolTip().isEmpty());
        const int before = split.sizes().at(0);
        // Properties opens with three fifths of the column.
        QVERIFY(std::abs(before - (before + split.sizes().at(1)) * 3 / 5) <= 2);
        // A drag on the grip moves it, and the place is saved.
        const QPoint middle = handle->rect().center();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, middle);
        QMouseEvent drag(QEvent::MouseMove, middle + QPoint(0, -120), handle->mapToGlobal(middle + QPoint(0, -120)), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(handle, &drag);
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, middle + QPoint(0, -120));
        moved = split.sizes().at(0);
        QVERIFY(moved < before - 60);
        QVERIFY(QSettings().contains("panelSplitState"));
    }
    Editor again;
    auto &split = find<QSplitter>(again.view, "panelSplit");
    QTRY_VERIFY(std::abs(split.sizes().at(0) - moved) <= 2);
    // A double-click puts it back and forgets the place.
    QTest::mouseDClick(split.handle(1), Qt::LeftButton);
    const int total = split.sizes().at(0) + split.sizes().at(1);
    QCOMPARE(split.sizes().at(0), total * 3 / 5);
    QVERIFY(!QSettings().contains("panelSplitState"));
}

QTEST_MAIN(ContentViewTests)
#include "ContentViewTests.moc"
