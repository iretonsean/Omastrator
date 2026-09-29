#include "Canvas/EditorCanvas.h"
#include "ContentView.h"
#include "Document/PathOperations.h"
#include "UI/ColorPickerSheet.h"
#include "UI/CommandPalette.h"
#include "UI/ContextMenus.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/Menus.h"
#include "UI/PaintStack.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/PropertiesPanel.h"
#include "TemporaryConfig.h"
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QtTest>

// Properties' fill and stroke stacks, hex fields, stroke alignment and
// arrowheads, Selection colors, recent colours, and Copy/Paste Properties.
namespace {
QUuid box(EditorSession &session, double x = 10)
{
    return session.addPath(Shapes::rectangle(QRectF(x, 10, 40, 40)), QStringLiteral("Box"));
}

void type(QWidget &panel, const QString &field, const QString &text)
{
    auto *edit = panel.findChild<QLineEdit *>(field);
    QVERIFY2(edit, qPrintable(field));
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, text);
    QTest::keyClick(edit, Qt::Key_Return);
    edit->clearFocus();
}

void choose(QWidget &panel, const QString &combo, int index)
{
    auto *box = panel.findChild<QComboBox *>(combo);
    QVERIFY2(box, qPrintable(combo));
    box->setCurrentIndex(index);
    emit box->activated(index);
}

const VectorObject &object(const EditorSession &session, const QUuid &id)
{
    return *session.document()->find(id);
}

StrokeStyle stroke(const QColor &color, double width)
{
    StrokeStyle style;
    style.paint = Paint::solid(color);
    style.width = width;
    return style;
}
}

class PaintStackTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        useTemporaryConfig();
        QSettings().clear();
        ShortcutSettings::shared().reload();
    }

    void cleanup() { QSettings().clear(); }

    void oneFillIsARowAndPlusStartsAStack()
    {
        EditorSession session;
        session.createDocument({300, 300});
        const QUuid id = box(session);
        PropertiesPanel panel(session);
        auto *fills = panel.findChild<PaintStack *>(QStringLiteral("fillStack"));
        QVERIFY(fills);
        QVERIFY(fills->isHidden());
        QVERIFY(!panel.findChild<QWidget *>(QStringLiteral("fillRow"))->isHidden());
        panel.findChild<QToolButton *>(QStringLiteral("fillAdd"))->click();
        QCOMPARE(session.undoName(), QStringLiteral("Add Fill"));
        QCOMPARE(object(session, id).fills().size(), size_t(2));
        QVERIFY(!fills->isHidden());
        QCOMPARE(fills->findChildren<QWidget *>(QStringLiteral("fillStackRow")).size(), qsizetype(2));
        // Removing back to one plain fill folds the stack away.
        fills->findChildren<QToolButton *>(QStringLiteral("fillStackRemove")).first()->click();
        QCOMPARE(session.undoName(), QStringLiteral("Remove Fill"));
        QCOMPARE(object(session, id).fills().size(), size_t(1));
        QVERIFY(fills->isHidden());
        session.undo();
        QVERIFY(!fills->isHidden());
    }

    void stackRowsHideReorderAndSetOpacity()
    {
        EditorSession session;
        session.createDocument({300, 300});
        const QUuid id = box(session);
        session.setFillsOfSelection({Paint::solid(Qt::red), Paint::solid(Qt::blue)}, QStringLiteral("Fill"));
        PropertiesPanel panel(session);
        panel.resize(320, 900);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto *fills = panel.findChild<PaintStack *>(QStringLiteral("fillStack"));
        QVERIFY(!fills->isHidden());
        // The top of the stack reads first.
        const QList<QLineEdit *> hexes = fills->findChildren<QLineEdit *>(QStringLiteral("fillStackHex"));
        QCOMPARE(hexes.size(), qsizetype(2));
        QStringList shown;
        for (QWidget *row : fills->findChildren<QWidget *>(QStringLiteral("fillStackRow"))) {
            shown << row->findChild<QLineEdit *>(QStringLiteral("fillStackHex"))->text();
            Q_UNUSED(row);
        }
        QVERIFY(shown.contains(QStringLiteral("0000FF")) && shown.contains(QStringLiteral("FF0000")));
        fills->setEntryHidden(1, true);
        QCOMPARE(session.undoName(), QStringLiteral("Hide Fill"));
        QVERIFY(object(session, id).extraFills.front().isHidden);
        fills->setEntryHidden(1, false);
        fills->setEntryOpacity(0, 0.4);
        QCOMPARE(object(session, id).fill.opacity, 0.4);
        fills->setEntryBlendMode(1, LayerBlendMode::multiply);
        QCOMPARE(object(session, id).extraFills.front().blendMode, LayerBlendMode::multiply);
        fills->moveEntry(1, 0);
        QCOMPARE(session.undoName(), QStringLiteral("Reorder Fills"));
        QCOMPARE(object(session, id).fill.color, QColor(Qt::blue));
        QCOMPARE(object(session, id).extraFills.front().color, QColor(Qt::red));

        // Dragging the bottom row's grip above the top one moves it up the stack.
        QTRY_COMPARE(fills->findChildren<QWidget *>(QStringLiteral("fillStackRow")).size(), qsizetype(2));
        std::vector<QWidget *> rows;
        for (QWidget *row : fills->findChildren<QWidget *>(QStringLiteral("fillStackRow")))
            rows.push_back(row);
        std::sort(rows.begin(), rows.end(), [](QWidget *a, QWidget *b) { return a->y() < b->y(); });
        QWidget *grip = rows.back()->findChild<QWidget *>(QStringLiteral("fillStackGrip"));
        const QPoint topRow = grip->mapFrom(fills, QPoint(5, rows.front()->geometry().center().y()));
        QTest::mousePress(grip, Qt::LeftButton, Qt::NoModifier, QPoint(3, 3));
        QTest::mouseRelease(grip, Qt::LeftButton, Qt::NoModifier, topRow);
        QCOMPARE(object(session, id).fill.color, QColor(Qt::red));
        QCOMPARE(object(session, id).extraFills.front().color, QColor(Qt::blue));
    }

    void hexFieldsApplyTypedColours()
    {
        EditorSession session;
        session.createDocument({300, 300});
        const QUuid id = box(session);
        PropertiesPanel panel(session);
        type(panel, QStringLiteral("fillHex"), QStringLiteral("#ff6600"));
        QCOMPARE(object(session, id).fill, Paint::solid(QColor(0xff, 0x66, 0x00)));
        QCOMPARE(session.undoName(), QStringLiteral("Fill"));
        QCOMPARE(panel.findChild<QLineEdit *>(QStringLiteral("fillHex"))->text(), QStringLiteral("FF6600"));
        type(panel, QStringLiteral("strokeHex"), QStringLiteral("0af"));
        QCOMPARE(object(session, id).stroke.paint.color, QColor(0x00, 0xaa, 0xff));
        // Nonsense changes nothing and reads back.
        type(panel, QStringLiteral("fillHex"), QStringLiteral("orange"));
        QCOMPARE(object(session, id).fill.color, QColor(0xff, 0x66, 0x00));
        QCOMPARE(panel.findChild<QLineEdit *>(QStringLiteral("fillHex"))->text(), QStringLiteral("FF6600"));
        QCOMPARE(HexColor::parse(QStringLiteral("#ABC")), std::optional<QColor>(QColor(0xaa, 0xbb, 0xcc)));
        QVERIFY(!HexColor::parse(QStringLiteral("#abcd")));
    }

    void theStrokeSectionEditsTheActiveStroke()
    {
        EditorSession session;
        session.createDocument({300, 300});
        const QUuid id = box(session);
        session.setStrokesOfSelection({stroke(Qt::black, 10), stroke(Qt::white, 2)}, QStringLiteral("Stroke"));
        PropertiesPanel panel(session);
        auto *strokes = panel.findChild<PaintStack *>(QStringLiteral("strokeStack"));
        QVERIFY(!strokes->isHidden());
        QCOMPARE(strokes->activeIndex(), 0);
        strokes->setActive(1);
        QCOMPARE(panel.findChild<QLineEdit *>(QStringLiteral("strokeWidth"))->text(), QStringLiteral("2"));
        type(panel, QStringLiteral("strokeWidth"), QStringLiteral("3"));
        QCOMPARE(object(session, id).stroke.width, 10.0);
        QCOMPARE(object(session, id).extraStrokes.front().width, 3.0);
        choose(panel, QStringLiteral("strokeAlign"), 2);
        QCOMPARE(object(session, id).extraStrokes.front().alignment, StrokeAlignment::outside);
        QCOMPARE(object(session, id).stroke.alignment, StrokeAlignment::center);
        // A row's own weight field.
        QLineEdit *bottom = nullptr;
        for (QLineEdit *field : strokes->findChildren<QLineEdit *>(QStringLiteral("strokeStackValue"))) {
            if (field->text() == QStringLiteral("10"))
                bottom = field;
        }
        QVERIFY(bottom);
        bottom->setFocus();
        bottom->selectAll();
        QTest::keyClicks(bottom, QStringLiteral("12"));
        QTest::keyClick(bottom, Qt::Key_Return);
        QCOMPARE(object(session, id).stroke.width, 12.0);
    }

    void alignmentShowsForClosedPathsAndArrowsForOpenOnes()
    {
        EditorSession session;
        session.createDocument({300, 300});
        box(session);
        PropertiesPanel panel(session);
        auto *align = panel.findChild<QComboBox *>(QStringLiteral("strokeAlign"));
        auto *arrows = panel.findChild<QWidget *>(QStringLiteral("strokeArrows"));
        QVERIFY(!align->isHidden());
        QVERIFY(arrows->isHidden());
        choose(panel, QStringLiteral("strokeAlign"), 1);
        QCOMPARE(session.undoName(), QStringLiteral("Stroke"));
        const QUuid line = session.addPath(Shapes::line({10, 100}, {200, 100}), QStringLiteral("Line"));
        QVERIFY(align->isHidden());
        QVERIFY(!arrows->isHidden());
        choose(panel, QStringLiteral("strokeEndArrow"), 2);
        QCOMPARE(object(session, line).stroke.endArrow, Arrowhead::triangle);
        choose(panel, QStringLiteral("strokeStartArrow"), 5);
        QCOMPARE(object(session, line).stroke.startArrow, Arrowhead::bar);
        type(panel, QStringLiteral("strokeArrowScale"), QStringLiteral("150"));
        QCOMPARE(object(session, line).stroke.arrowScale, 150.0);
        // Align to corners waits for dashes.
        auto *corners = panel.findChild<QCheckBox *>(QStringLiteral("strokeAlignDashes"));
        QVERIFY(corners->isHidden());
        type(panel, QStringLiteral("strokeDashes"), QStringLiteral("6 3"));
        QVERIFY(!corners->isHidden());
        corners->click();
        QVERIFY(object(session, line).stroke.alignDashes);
    }

    void selectionColorsListAndRecolour()
    {
        EditorSession session;
        session.createDocument({300, 300});
        const QUuid a = box(session, 10);
        session.setFillOfSelection(Paint::solid(Qt::red));
        const QUuid b = box(session, 100);
        session.setFillOfSelection(Paint::solid(Qt::blue));
        PropertiesPanel panel(session);
        auto *colors = panel.findChild<SelectionColors *>(QStringLiteral("selectionColors"));
        QVERIFY(colors->isHidden());
        session.select({a, b});
        QVERIFY(!colors->isHidden());
        // Red, blue and the shared black stroke.
        QCOMPARE(colors->findChildren<QAbstractButton *>(QStringLiteral("selectionColor")).size(), qsizetype(3));
        session.replaceColor(Qt::black, Qt::green);
        QCOMPARE(object(session, a).stroke.paint.color, QColor(Qt::green));
        QCOMPARE(object(session, b).stroke.paint.color, QColor(Qt::green));
    }

    void thePickerRemembersRecentColours()
    {
        QSettings().remove(QStringLiteral("colors/recent"));
        std::optional<QColor> chosen;
        {
            ColorPickerSheet sheet(Qt::red, [&chosen](std::optional<QColor> color) { chosen = color; });
            QVERIFY(sheet.findChildren<QAbstractButton *>(QStringLiteral("recentColor")).isEmpty());
            sheet.findChild<QPushButton *>(QStringLiteral("pickerOK"))->click();
        }
        QCOMPARE(chosen, std::optional<QColor>(QColor(Qt::red)));
        for (int n = 0; n < 14; ++n)
            RecentColors::add(QColor(n * 10, 0, 0));
        RecentColors::add(QColor(0, 0, 0));
        const std::vector<QColor> recent = RecentColors::list();
        QCOMPARE(int(recent.size()), RecentColors::limit);
        QCOMPARE(recent.front(), QColor(0, 0, 0));
        // No colour twice.
        QCOMPARE(std::count(recent.begin(), recent.end(), QColor(0, 0, 0)), 1);
        ColorPickerSheet again(Qt::white, [](std::optional<QColor>) {});
        const QList<QAbstractButton *> chips = again.findChildren<QAbstractButton *>(QStringLiteral("recentColor"));
        QCOMPARE(chips.size(), qsizetype(RecentColors::limit));
        chips.at(1)->click();
        QCOMPARE(again.color(), recent.at(1));
    }

    void copyAndPastePropertiesAreEditEntries()
    {
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        Menus &menus = *window.menus();
        workspace.createDocument(QSizeF(300, 300));
        EditorSession &session = workspace.current().session;
        QCOMPARE(menus.action("copyProperties")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
        QCOMPARE(menus.action("pasteProperties")->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V));
        QVERIFY(!menus.action("copyProperties")->isEnabled());
        const QUuid a = box(session, 10);
        session.setFillOfSelection(Paint::solid(Qt::magenta));
        QVERIFY(menus.action("copyProperties")->isEnabled());
        menus.action("copyProperties")->trigger();
        const QUuid b = box(session, 100);
        session.setFillOfSelection(Paint::solid(Qt::cyan));
        QVERIFY(menus.action("pasteProperties")->isEnabled());
        std::unique_ptr<QMenu> menu(ContextMenus::forCanvas(menus, session, window.content()->canvas(), {b}, nullptr));
        QVERIFY(menu->actions().contains(menus.action("copyProperties")));
        QVERIFY(menu->actions().contains(menus.action("pasteProperties")));
        menus.action("pasteProperties")->trigger();
        QCOMPARE(object(session, b).fill, object(session, a).fill);
        QCOMPARE(session.undoName(), QStringLiteral("Paste Properties"));
    }
};

QTEST_MAIN(PaintStackTests)
#include "PaintStackTests.moc"
