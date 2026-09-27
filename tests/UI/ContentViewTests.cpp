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
    void eachToolShowsItsBar();
    void shapeBarsEditTheSessionsNumbers();
    void shapeBuilderFoldsItsOptions();
    void typeBarsStyleSelectedTextInOneStep();
    void theWelcomeShowsWithoutADocument();
    void theStatusBarFollowsTheSession();
    void canvasKeysPickToolsAndSwapColours();
    void remappedKeysReachTheCanvasAsTheirOriginals();
    void theDockFollowsItsSettings();
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
    size_t count = 0;
    for (const std::vector<Tool> &group : ContentView::railGroups)
        count += group.size();
    QCOMPARE(count, allTools.size());
    for (const Tool tool : allTools)
        QVERIFY(editor.tool(tool).isVisible());
    // Tooltips name the tool and its key, if any.
    QCOMPARE(editor.tool(Tool::select).toolTip(), QString("Selection (V)"));
    QCOMPARE(editor.tool(Tool::line).toolTip(), QString("Line Segment (\\)"));
    QCOMPARE(editor.tool(Tool::star).toolTip(), QString("Star"));
    // A remapped key shows at once.
    QVERIFY(ShortcutSettings::shared().save({{QStringLiteral("Canvas & Layers:Pen tool"), ShortcutChord("k")}}));
    QCOMPARE(editor.tool(Tool::pen).toolTip(), QString("Pen (K)"));
    // Groups run top to bottom: selection above drawing above navigation.
    QVERIFY(editor.tool(Tool::directSelect).y() < editor.tool(Tool::pen).y());
    QVERIFY(editor.tool(Tool::star).y() < editor.tool(Tool::rotate).y());
    QVERIFY(editor.tool(Tool::eyedropper).y() < editor.tool(Tool::hand).y());
}

void ContentViewTests::aRailClickPicksTheTool()
{
    Editor editor;
    QVERIFY(editor.tool(Tool::select).isChecked());
    editor.tool(Tool::ellipse).click();
    QCOMPARE(editor.session.tool(), Tool::ellipse);
    QVERIFY(editor.tool(Tool::ellipse).isChecked() && !editor.tool(Tool::select).isChecked());
    // The session's choice from elsewhere shows too.
    editor.session.selectTool(Tool::zoom);
    QVERIFY(editor.tool(Tool::zoom).isChecked());
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
    QCOMPARE(editor.session.undoName(), QString("Character"));
    // Bold and alignment follow; the next text takes them too.
    find<QToolButton>(bar, "typeBold").click();
    find<QToolButton>(bar, "typeAlignCenter").click();
    QVERIFY(editor.session.document().value().find(second)->text.bold);
    QCOMPARE(editor.session.document().value().find(first)->text.alignment, TextAlignment::center);
    QVERIFY(editor.session.defaultText.bold);
    QVERIFY(find<QToolButton>(bar, "typeAlignCenter").isChecked());
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

QTEST_MAIN(ContentViewTests)
#include "ContentViewTests.moc"
