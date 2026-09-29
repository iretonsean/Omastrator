#include "Document/PathOperations.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspaceView.h"
#include "TemporaryConfig.h"
#include <QtTest>
#include <QApplication>
#include <QLabel>
#include <QSettings>
#include <QStandardPaths>

// The Keyboard Shortcuts window and the menus it remaps.
namespace {
// A window and its menu bar.
struct Bar {
    ProjectWorkspace workspace;
    ProjectWorkspaceView window{workspace};
    EditorSession &session() { return workspace.current().session; }
    QAction &action(const char *name)
    {
        QAction *found = window.menus()->action(QString::fromLatin1(name));
        if (!found)
            throw std::runtime_error(std::string("no entry named ") + name);
        return *found;
    }
};

QWidget *shownPanel()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() == QStringLiteral("keyboardShortcuts") && widget->isVisible())
            return widget;
    }
    return nullptr;
}

template <typename Widget> Widget &find(QWidget &root, const QString &name)
{
    Widget *found = root.findChild<Widget *>(name);
    if (!found)
        throw std::runtime_error("no widget named " + name.toStdString());
    return *found;
}

QWidget &opened(Bar &bar)
{
    // A shown window's menus hold live shortcuts.
    bar.window.show();
    if (!QTest::qWaitForWindowExposed(&bar.window))
        throw std::runtime_error("the window never showed");
    bar.action("keyboardShortcuts").trigger();
    QWidget *panel = nullptr;
    if (!QTest::qWaitFor([&] { return (panel = shownPanel()) != nullptr; }))
        throw std::runtime_error("the panel never showed");
    return *panel;
}
}

class KeyboardShortcutsSheetTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void aRecordedKeyRemapsItsMenuEntryOnSave();
    void recordingWaitsForARealKey();
    void aClashRestsSaveAndDefaultsComeBack();
    void cancelKeepsWhatWasSaved();
};

void KeyboardShortcutsSheetTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    useTemporaryConfig();
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}

void KeyboardShortcutsSheetTests::cleanup()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
    if (QWidget *panel = shownPanel())
        panel->hide();
}

void KeyboardShortcutsSheetTests::aRecordedKeyRemapsItsMenuEntryOnSave()
{
    Bar bar;
    QCOMPARE(bar.action("keyboardShortcuts").text(), QString("Keyboard Shortcuts…"));
    QWidget &panel = opened(bar);
    QCOMPARE(panel.windowTitle(), QString("Keyboard Shortcuts"));
    auto &sheet = find<QWidget>(panel, "keyboardShortcutsSheet");
    QCOMPARE(sheet.width(), 660);
    auto &undo = find<ShortcutRecorder>(sheet, "recorder:Menus:Undo");
    QCOMPARE(undo.text(), QString("Ctrl+Z"));
    QCOMPARE(undo.size(), QSize(150, 26));
    QTRY_COMPARE(QApplication::activeWindow(), &panel);
    undo.click();
    QVERIFY(undo.text() == "Press keys…" && undo.accessibleName() == "Press a shortcut");
    QTRY_VERIFY(undo.hasFocus());
    // Save rests while a key is being recorded.
    QVERIFY(!find<QPushButton>(sheet, "saveShortcuts").isEnabled());
    QTest::keyClick(&undo, Qt::Key_U, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(undo.text(), QString("Ctrl+Alt+U"));
    // Recording done, the button lets go of the keys.
    QVERIFY(!undo.hasFocus());
    // Nothing applies until Save.
    QCOMPARE(bar.action("undo").shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Z));
    find<QPushButton>(sheet, "saveShortcuts").click();
    QTRY_VERIFY(!shownPanel());
    QCOMPARE(bar.action("undo").shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U));
    QCOMPARE(ShortcutSettings::shared().overrides().value("Menus:Undo"), ShortcutChord("u", 3));
    // Another entry keeps its key.
    QCOMPARE(bar.action("fitArtboard").shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));
    // A new window reads the saved keys too.
    Bar again;
    QCOMPARE(again.action("undo").shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U));
    ShortcutSettings::shared().save({});
    QCOMPARE(bar.action("undo").shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Z));
}

void KeyboardShortcutsSheetTests::recordingWaitsForARealKey()
{
    Bar bar;
    bar.workspace.createDocument(QSizeF(20, 20));
    QWidget &panel = opened(bar);
    auto &pen = find<ShortcutRecorder>(panel, "recorder:Canvas & Layers:Pen tool");
    QCOMPARE(pen.text(), QString("P"));
    pen.click();
    // A modifier alone waits; a long key name is refused.
    QTest::keyClick(&pen, Qt::Key_Control, Qt::ControlModifier);
    QTest::keyClick(&pen, Qt::Key_F1);
    QCOMPARE(pen.text(), QString("Press keys…"));
    // A menu's own key is recorded, not run.
    bar.session().addPath(Shapes::rectangle(QRectF(2, 2, 8, 8)), QStringLiteral("Box"));
    bar.session().deselectAll();
    QVERIFY(bar.action("selectAll").isEnabled());
    QTest::keyClick(&pen, Qt::Key_A, Qt::ControlModifier);
    QVERIFY(bar.session().selection().empty());
    QCOMPARE(pen.text(), QString("Ctrl+A"));
    // The clash with Select All rests Save.
    QVERIFY(!find<QPushButton>(panel, "saveShortcuts").isEnabled());
    QVERIFY(find<QLabel>(panel, "shortcutProblem").isVisible());
    // Tab is recorded too, never moving the focus.
    pen.click();
    QTRY_VERIFY(pen.hasFocus());
    QTest::keyClick(&pen, Qt::Key_Tab);
    QCOMPARE(pen.text(), QString("Tab"));
}

void KeyboardShortcutsSheetTests::aClashRestsSaveAndDefaultsComeBack()
{
    Bar bar;
    QWidget &panel = opened(bar);
    auto &pen = find<ShortcutRecorder>(panel, "recorder:Canvas & Layers:Pen tool");
    auto &problem = find<QLabel>(panel, "shortcutProblem");
    auto &save = find<QPushButton>(panel, "saveShortcuts");
    QVERIFY(!problem.isVisible() && save.isEnabled() && save.isDefault());
    pen.click();
    QTest::keyClick(&pen, Qt::Key_V);
    QCOMPARE(problem.text(), QString("V is assigned to both Selection tool and Pen tool."));
    QVERIFY(problem.isVisible() && !save.isEnabled());
    QCOMPARE(problem.font().pixelSize(), 12);
    // Search hides rows by title, whatever the case.
    auto &search = find<QLineEdit>(panel, "shortcutSearch");
    QCOMPARE(search.placeholderText(), QString("Search shortcuts"));
    search.setText("PEN TOOL");
    QVERIFY(pen.parentWidget()->isVisible());
    QVERIFY(!find<ShortcutRecorder>(panel, "recorder:Menus:Undo").parentWidget()->isVisible());
    search.clear();
    QVERIFY(find<ShortcutRecorder>(panel, "recorder:Menus:Undo").parentWidget()->isVisible());
    // Restore Defaults also ends a recording.
    pen.click();
    QCOMPARE(pen.text(), QString("Press keys…"));
    find<QPushButton>(panel, "restoreShortcuts").click();
    QVERIFY(!problem.isVisible() && save.isEnabled() && pen.text() == "P");
    // Return saves, Save being the default.
    QTRY_COMPARE(QApplication::activeWindow(), &panel);
    QTest::keyClick(&search, Qt::Key_Return);
    QTRY_VERIFY(!shownPanel());
    QVERIFY(ShortcutSettings::shared().overrides().isEmpty());
}

void KeyboardShortcutsSheetTests::cancelKeepsWhatWasSaved()
{
    Bar bar;
    ShortcutSettings::shared().save({{"Canvas & Layers:Pen tool", ShortcutChord("e")}});
    QWidget &panel = opened(bar);
    auto &pen = find<ShortcutRecorder>(panel, "recorder:Canvas & Layers:Pen tool");
    // The sheet starts from what is saved.
    QCOMPARE(pen.text(), QString("E"));
    pen.click();
    QTest::keyClick(&pen, Qt::Key_Y);
    find<QPushButton>(panel, "cancelShortcuts").click();
    QTRY_VERIFY(!shownPanel());
    QCOMPARE(ShortcutSettings::shared().overrides().value("Canvas & Layers:Pen tool"), ShortcutChord("e"));
    // Escape closes it as Cancel does.
    QWidget &again = opened(bar);
    QTRY_COMPARE(QApplication::activeWindow(), &again);
    QTest::keyClick(QApplication::focusWidget() ? QApplication::focusWidget() : &again, Qt::Key_Escape);
    QTRY_VERIFY(!shownPanel());
    QCOMPARE(ShortcutSettings::shared().overrides().size(), 1);
}

QTEST_MAIN(KeyboardShortcutsSheetTests)
#include "KeyboardShortcutsSheetTests.moc"
