#include "UI/KeyboardShortcuts.h"
#include <QKeyEvent>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

// The shortcut model: chords, definitions, rules, rewriting.
namespace {
QKeyEvent press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = QString())
{
    return QKeyEvent(QEvent::KeyPress, key, modifiers, text);
}

const ShortcutDefinition &named(const QString &title)
{
    for (const ShortcutDefinition &definition : ShortcutDefinition::all()) {
        if (definition.title == title)
            return definition;
    }
    throw std::runtime_error("no shortcut named " + title.toStdString());
}

void clear()
{
    QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
    ShortcutSettings::shared().reload();
}
}

class KeyboardShortcutsTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void chordsReadKeysAsCocoaDoes();
    void theListIsIllustratorsKeys();
    void toolLettersNameTheirTools();
    void problemsNameWhatIsWrong();
    void savedOverridesPersistAndBadOnesAreIgnored();
    void menusAndSheetsTakeTheirRemappedKeys();
    void theCanvasTranslatesRemappedKeys();
};

void KeyboardShortcutsTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    clear();
}

void KeyboardShortcutsTests::cleanup()
{
    clear();
}

void KeyboardShortcutsTests::chordsReadKeysAsCocoaDoes()
{
    QCOMPARE(ShortcutChord(press(Qt::Key_B)), ShortcutChord("b"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier)), ShortcutChord("z", 9));
    QCOMPARE(ShortcutChord(press(Qt::Key_H, Qt::AltModifier | Qt::MetaModifier)), ShortcutChord("h", 6));
    // Shifted brackets and signs are their own keys.
    QCOMPARE(ShortcutChord(press(Qt::Key_BraceLeft, Qt::ShiftModifier)), ShortcutChord("[", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_BraceRight, Qt::ShiftModifier)), ShortcutChord("]", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Plus, Qt::ShiftModifier)), ShortcutChord("=", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Underscore, Qt::ShiftModifier)), ShortcutChord("-", 8));
    // Both deletes, returns and tabs are one key each.
    QCOMPARE(ShortcutChord(press(Qt::Key_Backspace)), ShortcutChord("\x7f"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Delete)), ShortcutChord("\x7f"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Enter)), ShortcutChord("\r"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Backtab, Qt::ShiftModifier)), ShortcutChord("\t", 8));
    QCOMPARE(ShortcutChord(press(Qt::Key_Escape)), ShortcutChord("\x1b"));
    QCOMPARE(ShortcutChord(press(Qt::Key_Left)), ShortcutChord(QString(QChar(0xf702))));
    QCOMPARE(ShortcutChord(press(Qt::Key_Down)).key, QString(QChar(0xf701)));
    QCOMPARE(ShortcutChord(press(Qt::Key_F1)).key.size(), 2);
    // Labels and key combinations, as the menus show them.
    QCOMPARE(ShortcutChord("s", 11).label(), QString("Ctrl+Alt+Shift+S"));
    QCOMPARE(ShortcutChord("\x7f", 2).label(), QString("Alt+Backspace"));
    QCOMPARE(ShortcutChord("[", 8).combination(), QKeyCombination(Qt::ShiftModifier, Qt::Key_BraceLeft));
    QCOMPARE(ShortcutChord(QString(QChar(0xf700)), 1).combination(), QKeyCombination(Qt::ControlModifier, Qt::Key_Up));
    QCOMPARE(ShortcutChord(QKeyCombination(Qt::ControlModifier, Qt::Key_Equal)), ShortcutChord("=", 1));
    // A chord typed again carries its key, text and repeat.
    const QKeyEvent like(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier, QStringLiteral("k"), true);
    const std::unique_ptr<QKeyEvent> typed = ShortcutChord("b").event(like);
    QVERIFY(typed->key() == Qt::Key_B && typed->modifiers() == Qt::NoModifier && typed->text() == "b" && typed->isAutoRepeat());
    const std::unique_ptr<QKeyEvent> space = ShortcutChord(" ").event(like);
    QVERIFY(space->key() == Qt::Key_Space && space->text().isEmpty());
    const std::unique_ptr<QKeyEvent> hard = ShortcutChord("]", 8).event(like);
    QVERIFY(hard->key() == Qt::Key_BraceRight && hard->modifiers() == Qt::ShiftModifier && hard->text() == "}");
}

void KeyboardShortcutsTests::theListIsIllustratorsKeys()
{
    const std::vector<ShortcutDefinition> &all = ShortcutDefinition::all();
    // Forty menu entries, fourteen tools, five keys, eight nudges.
    QCOMPARE(int(all.size()), 67);
    QCOMPARE(int(std::count_if(all.begin(), all.end(), [](const ShortcutDefinition &each) { return each.isMenu(); })), 40);
    QSet<QString> ids;
    for (const ShortcutDefinition &definition : all)
        ids.insert(definition.id());
    QCOMPARE(int(ids.size()), int(all.size()));
    QCOMPARE(named("Deselect").original, ShortcutChord("a", 9));
    QCOMPARE(named("Release Compound Path").original, ShortcutChord("8", 11));
    QCOMPARE(named("Show Grid").original, ShortcutChord("'", 1));
    QCOMPARE(named("Outline").original, ShortcutChord("y", 1));
    QVERIFY(named("Export PNG").isMenu());
    QCOMPARE(named("Line Segment tool").original, ShortcutChord("\\"));
    QCOMPARE(named("Temporary Hand tool (hold)").original, ShortcutChord(" "));
    QCOMPARE(named("Nudge Down 10 pt").original, ShortcutChord(QString(QChar(0xf701)), 8));
    QCOMPARE(named("Swap fill and stroke").group, QString("Canvas & Layers"));
    QCOMPARE(named("Undo").id(), QString("Menus:Undo"));
    // The defaults hold together.
    QVERIFY(!ShortcutSettings::problem({}));
}

void KeyboardShortcutsTests::toolLettersNameTheirTools()
{
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("v")).value(), Tool::select);
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("a")).value(), Tool::directSelect);
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("\\")).value(), Tool::line);
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("z")).value(), Tool::zoom);
    // Modifiers and unassigned letters pick nothing.
    QVERIFY(!ShortcutDefinition::tool(ShortcutChord("v", 8)));
    QVERIFY(!ShortcutDefinition::tool(ShortcutChord("q")));
    // Shape Builder is Illustrator's Shift-M, beside the Rectangle's M.
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("m", 8)).value(), Tool::shapeBuilder);
    QCOMPARE(ShortcutDefinition::tool(ShortcutChord("m")).value(), Tool::rectangle);
    QCOMPARE(named("Shape Builder tool").original, ShortcutChord("m", 8));
    QCOMPARE(named("Pen tool").original, ShortcutChord("p"));
    // A shifted apostrophe and digit are their own keys.
    QCOMPARE(ShortcutChord(press(Qt::Key_QuoteDbl, Qt::ControlModifier | Qt::ShiftModifier)), ShortcutChord("'", 9));
    QCOMPARE(ShortcutChord(press(Qt::Key_Asterisk, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)), ShortcutChord("8", 11));
    QCOMPARE(ShortcutChord("'", 9).combination(), QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_QuoteDbl));
}

void KeyboardShortcutsTests::problemsNameWhatIsWrong()
{
    const QString pen = named("Pen tool").id();
    QCOMPARE(ShortcutSettings::problem({{pen, ShortcutChord("f1")}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{pen, ShortcutChord("k", 16)}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{pen, ShortcutChord("k", -1)}}).value(), QString("Choose a single key with optional modifiers."));
    QCOMPARE(ShortcutSettings::problem({{pen, ShortcutChord("v")}}).value(), QString("V is assigned to both Selection tool and Pen tool."));
    // Moving both apart is fine.
    QVERIFY(!ShortcutSettings::problem({{pen, ShortcutChord("v")}, {named("Selection tool").id(), ShortcutChord("p")}}));
}

void KeyboardShortcutsTests::savedOverridesPersistAndBadOnesAreIgnored()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    QSignalSpy changes(&settings, &ShortcutSettings::changed);
    const QString pen = named("Pen tool").id();
    QVERIFY(!settings.save({{pen, ShortcutChord("v")}}));
    QVERIFY(settings.overrides().isEmpty() && changes.isEmpty());
    QVERIFY(settings.save({{pen, ShortcutChord("k")}}));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(settings.chord(named("Pen tool")), ShortcutChord("k"));
    QCOMPARE(settings.chord(named("Pencil tool")), ShortcutChord("n"));
    // Read again as at launch, from QSettings.
    settings.reload();
    QCOMPARE(settings.overrides(), (QHash<QString, ShortcutChord>{{pen, ShortcutChord("k")}}));
    QCOMPARE(QSettings().value(QLatin1String(ShortcutSettings::storageKey)).toByteArray(),
             QByteArray(R"({"Canvas & Layers:Pen tool":{"key":"k","modifiers":0}})"));
    // Stored overrides that clash are dropped with a warning.
    QSettings().setValue(QLatin1String(ShortcutSettings::storageKey), QByteArray(R"({"Canvas & Layers:Pen tool":{"key":"v","modifiers":0}})"));
    QTest::ignoreMessage(QtWarningMsg, "stored keyboard shortcuts ignored: V is assigned to both Selection tool and Pen tool.");
    settings.reload();
    QVERIFY(settings.overrides().isEmpty());
}

void KeyboardShortcutsTests::menusAndSheetsTakeTheirRemappedKeys()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    const QKeySequence undo(Qt::CTRL | Qt::Key_Z);
    QCOMPARE(settings.menu(undo), undo);
    QVERIFY(settings.save({{named("Undo").id(), ShortcutChord("u", 3)}, {named("Group").id(), ShortcutChord("h", 3)},
                           {named("Apply / finish current operation").id(), ShortcutChord("k")}}));
    QCOMPARE(settings.menu(undo), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U));
    // An unlisted entry keeps its key; none stays none.
    QCOMPARE(settings.menu(QKeySequence(Qt::CTRL | Qt::Key_G)), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    QCOMPARE(settings.menu(QKeySequence(Qt::CTRL | Qt::Key_L)), QKeySequence(Qt::CTRL | Qt::Key_L));
    QCOMPARE(settings.menu(QKeySequence(Qt::Key_F7)), QKeySequence(Qt::Key_F7));
    QCOMPARE(settings.menu(QKeySequence()), QKeySequence());
    // Sheets read the canvas's Apply and Cancel.
    QCOMPARE(settings.native(ShortcutChord("\r")), ShortcutChord("k"));
    QCOMPARE(settings.native(ShortcutChord("\x1b")), ShortcutChord("\x1b"));
    QCOMPARE(settings.native(ShortcutChord("y", 1)), ShortcutChord("y", 1));
    // A menu's chord is no sheet's.
    QCOMPARE(settings.native(ShortcutChord("z", 1)), ShortcutChord("z", 1));
}

void KeyboardShortcutsTests::theCanvasTranslatesRemappedKeys()
{
    ShortcutSettings &settings = ShortcutSettings::shared();
    // Without overrides every key passes as it is.
    std::unique_ptr<QKeyEvent> same = settings.canvasEvent(press(Qt::Key_P, Qt::NoModifier, "p"));
    QVERIFY(same && same->key() == Qt::Key_P);
    QVERIFY(settings.save({{named("Pen tool").id(), ShortcutChord("k")}, {named("Undo").id(), ShortcutChord("u", 3)}}));
    // The new key stands for the old; the old goes.
    const std::unique_ptr<QKeyEvent> pen = settings.canvasEvent(press(Qt::Key_K, Qt::NoModifier, "k"));
    QVERIFY(pen && pen->key() == Qt::Key_P && pen->modifiers() == Qt::NoModifier);
    QVERIFY(!settings.canvasEvent(press(Qt::Key_P, Qt::NoModifier, "p")));
    // A menu's old chord is swallowed at the canvas too.
    QVERIFY(!settings.canvasEvent(press(Qt::Key_Z, Qt::ControlModifier)));
    // Shift follows a letter home.
    const std::unique_ptr<QKeyEvent> shifted = settings.canvasEvent(press(Qt::Key_K, Qt::ShiftModifier, "K"));
    QVERIFY(shifted && shifted->key() == Qt::Key_P && shifted->modifiers() == Qt::ShiftModifier);
    QVERIFY(!settings.canvasEvent(press(Qt::Key_P, Qt::ShiftModifier, "P")));
    // Keys nobody moved pass untouched.
    const std::unique_ptr<QKeyEvent> other = settings.canvasEvent(press(Qt::Key_Q, Qt::NoModifier, "q"));
    QVERIFY(other && other->key() == Qt::Key_Q);
}

QTEST_MAIN(KeyboardShortcutsTests)
#include "KeyboardShortcutsTests.moc"
