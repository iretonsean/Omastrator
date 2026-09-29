#include "Document/PathOperations.h"
#include "UI/CommandPalette.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Ctrl+K: every command by name with its key, fuzzy matching, recent first, and plain language going to the agent.
namespace {
constexpr const char *fakeOmarchy = "#!/bin/sh\n"
                                    "if [ \"$1\" = default ]; then printf '%s\\n' \"$FAKE_AGENT\"; exit 0; fi\n"
                                    "if [ \"$1\" = agent ] && [ \"$2\" = prompt ]; then printf '%s' \"$3\" > \"$FAKE_OUT/prompt\"; exit 0; fi\n"
                                    "exit 2\n";

struct Window {
    ProjectWorkspace workspace;
    ProjectWorkspaceView view{workspace};

    Window()
    {
        view.show();
        if (!QTest::qWaitForWindowExposed(&view))
            qWarning("window never exposed");
        workspace.createDocument(QSizeF(400, 300));
    }
    EditorSession &session() { return workspace.current().session; }
    QUuid box(double x) { return session().addPath(Shapes::rectangle(QRectF(x, 20, 60, 40)), QStringLiteral("Box")); }
    CommandPalette &palette()
    {
        CommandPalette *palette = view.menus()->commandPalette();
        if (!palette->isVisible())
            palette->open();
        return *palette;
    }
    std::optional<CommandPalette::Command> command(const QString &id)
    {
        for (const CommandPalette::Command &each : palette().commands()) {
            if (each.id == id)
                return each;
        }
        return std::nullopt;
    }
    QStringList ids(const QString &query, int count = 100)
    {
        QStringList result;
        for (const CommandPalette::Command &each : palette().results(query))
            result << each.id;
        return result.mid(0, count);
    }
};

QString key(QKeyCombination combination)
{
    return QKeySequence(combination).toString(QKeySequence::NativeText);
}
}

class CommandPaletteTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

    QString prompt() const { return FakeAgents::read(m_directory.filePath(QStringLiteral("prompt"))); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        const QString script = m_directory.filePath(QStringLiteral("omarchy"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(fakeOmarchy);
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_OMARCHY", script.toUtf8());
        qputenv("FAKE_OUT", m_directory.path().toUtf8());
        qputenv("FAKE_AGENT", "sh");
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("omastrator.sock")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
    }

    void init()
    {
        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        QSettings().remove(QStringLiteral("commandPalette/recent"));
        QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
        QSettings().remove(QStringLiteral("roast/heat"));
        ShortcutSettings::shared().reload();
    }

    void opensFromHelpAndItsKeys()
    {
        Window w;
        QAction *open = w.view.menus()->action(QStringLiteral("commandPalette"));
        QVERIFY(open);
        QCOMPARE(open->text(), QStringLiteral("Command Palette…"));
        QCOMPARE(open->shortcuts(), (QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::Key_Slash)}));
        bool inHelp = false;
        for (QAction *menu : w.view.menuBar()->actions()) {
            if (menu->text() == QLatin1String("&Help"))
                inHelp = menu->menu()->actions().contains(open);
        }
        QVERIFY(inHelp);
        open->trigger();
        CommandPalette *palette = w.view.findChild<CommandPalette *>();
        QVERIFY(palette && palette->isVisible());
        QCOMPARE(palette->accessibleName(), QStringLiteral("Command Palette"));
        QCOMPARE(palette->search()->accessibleName(), QStringLiteral("Search commands"));
        QVERIFY(w.view.rect().contains(palette->geometry()));
        // Esc puts it away.
        QTest::keyClick(palette->search(), Qt::Key_Escape);
        QVERIFY(!palette->isVisible());
    }

    void listsEveryKindOfCommandWithItsKey()
    {
        QSettings().setValue(QStringLiteral("recentFiles"), QStringList{m_directory.filePath(QStringLiteral("logo.omai"))});
        Window w;
        const auto group = w.command(QStringLiteral("action:group"));
        QVERIFY(group);
        QCOMPARE(group->title, QStringLiteral("Group"));
        QCOMPARE(group->where, QStringLiteral("Object"));
        QCOMPARE(group->shortcut, key(Qt::CTRL | Qt::Key_G));
        QCOMPARE(w.command(QStringLiteral("action:outlineStroke"))->where, QStringLiteral("Object ▸ Path"));
        QCOMPARE(w.command(QStringLiteral("action:makeCompoundPath"))->title, QStringLiteral("Make Compound Path"));
        QCOMPARE(w.command(QStringLiteral("action:selectSameFillColor"))->title, QStringLiteral("Select Same Fill Color"));
        // Tools, with their keys.
        QCOMPARE(w.command(QStringLiteral("tool:pen"))->title, QStringLiteral("Pen Tool"));
        QCOMPARE(w.command(QStringLiteral("tool:pen"))->shortcut, QStringLiteral("P"));
        QCOMPARE(w.command(QStringLiteral("tool:select"))->shortcut, QStringLiteral("V"));
        QCOMPARE(w.command(QStringLiteral("tool:shapeBuilder"))->shortcut, QStringLiteral("Shift+M"));
        // Panels, settings, AI and the document's own.
        QVERIFY(w.command(QStringLiteral("action:showLayers")));
        QVERIFY(w.command(QStringLiteral("panel:variations")));
        QVERIFY(w.command(QStringLiteral("action:artboardSize")));
        QVERIFY(w.command(QStringLiteral("action:showGrid")));
        QVERIFY(w.command(QStringLiteral("action:snapToGrid")));
        QVERIFY(w.command(QStringLiteral("setting:smartGuides"))->on);
        QVERIFY(w.command(QStringLiteral("setting:keyboardIncrement"))->where.startsWith(QLatin1String("Preferences")));
        QVERIFY(w.command(QStringLiteral("action:generate")));
        QVERIFY(w.command(QStringLiteral("action:vectorizeWithAI")));
        QCOMPARE(w.command(QStringLiteral("ai:roast"))->where, QStringLiteral("AI · Heat: Savage"));
        const auto recent = w.command(QStringLiteral("recent:") + m_directory.filePath(QStringLiteral("logo.omai")));
        QVERIFY(recent);
        QCOMPARE(recent->where, QStringLiteral("File ▸ Open Recent"));
        // Its own entry isn't listed in itself.
        QVERIFY(!w.command(QStringLiteral("action:commandPalette")));
        // The selection's right-click entries join in: Align and Pathfinder.
        w.palette().close();
        w.session().select({w.box(20), w.box(100)});
        QCOMPARE(w.command(QStringLiteral("action:contextAlignLeft"))->title, QStringLiteral("Align Left"));
        QCOMPARE(w.command(QStringLiteral("action:contextPathfinderUnite"))->where, QStringLiteral("Pathfinder"));
        QSettings().remove(QStringLiteral("recentFiles"));
    }

    void fuzzyMatchesAndRanks()
    {
        QCOMPARE(CommandPalette::score(QStringLiteral("group"), QStringLiteral("Group")), std::optional(1000));
        QVERIFY(*CommandPalette::score(QStringLiteral("gro"), QStringLiteral("Group")) > *CommandPalette::score(QStringLiteral("gro"), QStringLiteral("Ungroup")));
        QVERIFY(*CommandPalette::score(QStringLiteral("outl"), QStringLiteral("Create Outlines")) >= 600);
        QVERIFY(*CommandPalette::score(QStringLiteral("co"), QStringLiteral("Create Outlines")) >= 500);
        QVERIFY(CommandPalette::score(QStringLiteral("shp bldr"), QStringLiteral("Shape Builder Tool")));
        QVERIFY(CommandPalette::score(QStringLiteral("grp"), QStringLiteral("Group")));
        QVERIFY(!CommandPalette::score(QStringLiteral("zqx"), QStringLiteral("Group")));
        QVERIFY(!CommandPalette::score(QStringLiteral("outl"), QStringLiteral("Rounded Rectangle Tool")));

        Window w;
        w.box(20);
        // "outl" finds Outline Stroke, Create Outlines and Outline mode, first.
        const QStringList outl = w.ids(QStringLiteral("outl"), 3);
        for (const char *id : {"action:outlineStroke", "action:createOutlines", "action:outline"})
            QVERIFY2(outl.contains(QString::fromLatin1(id)), qPrintable(outl.join(',')));
        // A tool by its name, or its keyword; an exact name first.
        QCOMPARE(w.ids(QStringLiteral("pen tool"), 1), QStringList{"tool:pen"});
        QCOMPARE(w.ids(QStringLiteral("group"), 1), QStringList{"action:group"});
        QCOMPARE(w.ids(QStringLiteral("document setup"), 1), QStringList{"action:artboardSize"});
        QCOMPARE(w.ids(QStringLiteral("nudge"), 1), QStringList{"setting:keyboardIncrement"});
        // What can't run now sinks below what can.
        QVERIFY(!w.command(QStringLiteral("action:ungroup"))->enabled);
        const QStringList group = w.ids(QStringLiteral("group"));
        QVERIFY(group.indexOf(QStringLiteral("action:group")) < group.indexOf(QStringLiteral("action:ungroup")));
    }

    void runsTheChosenActionAndRemembersIt()
    {
        Window w;
        const QUuid a = w.box(20), b = w.box(100);
        w.session().select({a, b});
        CommandPalette &palette = w.palette();
        QTest::keyClicks(palette.search(), QStringLiteral("group"));
        QCOMPARE(palette.list()->currentRow(), 0);
        QCOMPARE(palette.list()->currentItem()->text(), QStringLiteral("Group"));
        QTest::keyClick(palette.search(), Qt::Key_Return);
        QVERIFY(!palette.isVisible());
        QCOMPARE(w.session().undoName(), QStringLiteral("Group"));
        QCOMPARE(CommandPalette::recent().front(), QStringLiteral("action:group"));
        // A tool, by arrowing to it.
        palette.open();
        QTest::keyClicks(palette.search(), QStringLiteral("pen"));
        const QStringList shown = w.ids(QStringLiteral("pen"));
        const int row = int(shown.indexOf(QStringLiteral("tool:pen")));
        QVERIFY(row >= 0);
        for (int step = 0; step < row; ++step)
            QTest::keyClick(palette.search(), Qt::Key_Down);
        QCOMPARE(palette.list()->currentRow(), row);
        QTest::keyClick(palette.search(), Qt::Key_Return);
        QCOMPARE(w.session().tool(), Tool::pen);
    }

    void aDisabledCommandSaysSoAndDoesNothing()
    {
        Window w;
        CommandPalette &palette = w.palette();
        palette.search()->setText(QStringLiteral("ungroup"));
        QCOMPARE(palette.results(QStringLiteral("ungroup")).front().id, QStringLiteral("action:ungroup"));
        QCOMPARE(palette.runRow(0), QStringLiteral("Ungroup isn't available right now."));
        QVERIFY(palette.isVisible());
        QCOMPARE(palette.message(), QStringLiteral("Ungroup isn't available right now."));
    }

    void recentCommandsFloatToTheTop()
    {
        CommandPalette::remember(QStringLiteral("action:showGrid"));
        CommandPalette::remember(QStringLiteral("tool:pen"));
        Window w;
        QCOMPARE(w.ids(QString(), 2), (QStringList{"tool:pen", "action:showGrid"}));
        // Among matches, the one used lately leads.
        const QStringList grid = w.ids(QStringLiteral("grid"), 2);
        QCOMPARE(grid.front(), QStringLiteral("action:showGrid"));
        CommandPalette::remember(QStringLiteral("action:snapToGrid"));
        w.palette().close();
        QCOMPARE(w.ids(QStringLiteral("grid"), 1), QStringList{"action:snapToGrid"});
        for (int index = 0; index < 20; ++index)
            CommandPalette::remember(QStringLiteral("x%1").arg(index));
        QCOMPARE(CommandPalette::recent().size(), CommandPalette::recentLimit);
    }

    void plainLanguageEditsTheSelection()
    {
        Window w;
        w.box(20);
        const std::vector<CommandPalette::Command> shown = w.palette().results(QStringLiteral("make it teal"));
        QCOMPARE(shown.front().id, QStringLiteral("ask"));
        QCOMPARE(shown.front().title, QStringLiteral("Ask sh: make it teal"));
        QCOMPARE(shown.front().where, QStringLiteral("Edit the selection"));
        CommandPalette &palette = w.palette();
        QTest::keyClicks(palette.search(), QStringLiteral("make it teal"));
        QTest::keyClick(palette.search(), Qt::Key_Return);
        QVERIFY(!palette.isVisible());
        QVERIFY(prompt().contains(QStringLiteral("make it teal")));
        QVERIFY(prompt().contains(QStringLiteral("It applies to the current selection only")));
        QVERIFY(w.view.agent()->waiting());
        QVERIFY(!CommandPalette::recent().contains(QStringLiteral("ask")));
    }

    void plainLanguageEditsTheDocumentOrGenerates()
    {
        Window w;
        w.box(20);
        w.session().deselectAll();
        const auto first = [&](const QString &text) { return w.palette().results(text).front(); };
        QCOMPARE(first(QStringLiteral("make everything warmer")).where, QStringLiteral("Edit the document"));
        QCOMPARE(first(QStringLiteral("draw a fox logo")).where, QStringLiteral("Generate new art"));
        QCOMPARE(first(QStringLiteral("a paper plane")).where, QStringLiteral("Generate new art"));
        // A command's name keeps the command first, with the agent's row last.
        const std::vector<CommandPalette::Command> named = w.palette().results(QStringLiteral("show grid"));
        QCOMPARE(named.front().id, QStringLiteral("action:showGrid"));
        QCOMPARE(named.back().id, QStringLiteral("ask"));
        // "?" asks, whatever it says.
        QCOMPARE(first(QStringLiteral("?show grid")).title, QStringLiteral("Ask sh: show grid"));

        CommandPalette &palette = w.palette();
        palette.search()->setText(QStringLiteral("make everything warmer"));
        QCOMPARE(palette.runRow(0), QString());
        QVERIFY(prompt().contains(QStringLiteral("make everything warmer")));
        QVERIFY(prompt().contains(QStringLiteral("whole document")));
        w.view.agent()->stopWaiting();

        QFile::remove(m_directory.filePath(QStringLiteral("prompt")));
        palette.open();
        palette.search()->setText(QStringLiteral("draw a fox logo"));
        QCOMPARE(palette.runRow(0), QString());
        QVERIFY(prompt().contains(QStringLiteral("draw a fox logo")));
        QVERIFY(prompt().contains(QStringLiteral("Make 3 distinct variations")));
        QCOMPARE(w.view.agent()->rounds().back().instruction, QStringLiteral("draw a fox logo"));
        w.view.agent()->stopWaiting();

        QVERIFY(CommandPalette::asksForNewArt(QStringLiteral("Please draw a cat")));
        QVERIFY(CommandPalette::asksForNewArt(QStringLiteral("make a logo for a bakery")));
        QVERIFY(!CommandPalette::asksForNewArt(QStringLiteral("make it teal")));
        QVERIFY(!CommandPalette::asksForNewArt(QStringLiteral("align everything to the left")));
    }

    void pagesAreCommands()
    {
        Window w;
        // One page: the menu's entries, but no one to go to.
        QCOMPARE(w.command(QStringLiteral("action:newPage"))->title, QStringLiteral("New Page"));
        QCOMPARE(w.command(QStringLiteral("action:newPage"))->where, QStringLiteral("Object ▸ Pages"));
        QCOMPARE(w.command(QStringLiteral("action:nextPage"))->shortcut, key(Qt::ALT | Qt::Key_PageDown));
        QVERIFY(!w.command(QStringLiteral("action:deletePage"))->enabled);
        for (const char *name : {"duplicatePage", "renamePage", "deletePage", "nextPage", "previousPage"})
            QVERIFY2(w.command(QStringLiteral("action:") + QString::fromLatin1(name)), name);
        QVERIFY(w.ids(QStringLiteral("go to page")).isEmpty() || !w.ids(QStringLiteral("go to page")).first().startsWith("page:"));
        const QUuid first = w.session().currentPage();
        w.palette().close();
        w.session().addPage(QStringLiteral("Cover"));
        w.box(30);
        // Two pages: Go to Page for each, Move to Page for the other with a selection; "page" and "canvas" find them.
        const auto go = w.command(QStringLiteral("page:") + first.toString(QUuid::WithoutBraces));
        QVERIFY(go);
        QCOMPARE(go->title, QStringLiteral("Go to Page: Page 1"));
        QVERIFY(go->enabled);
        QVERIFY(!w.command(QStringLiteral("page:") + w.session().currentPage().toString(QUuid::WithoutBraces))->enabled);
        QVERIFY(w.ids(QStringLiteral("go to cover")).contains(QStringLiteral("page:") + w.session().currentPage().toString(QUuid::WithoutBraces)));
        QVERIFY(w.ids(QStringLiteral("canvas"), 100).contains(QStringLiteral("page:") + first.toString(QUuid::WithoutBraces)));
        const auto move = w.command(QStringLiteral("moveToPage:") + first.toString(QUuid::WithoutBraces));
        QVERIFY(move);
        QCOMPARE(move->title, QStringLiteral("Move to Page: Page 1"));
        QVERIFY(!w.command(QStringLiteral("moveToPage:") + w.session().currentPage().toString(QUuid::WithoutBraces))->enabled);
        QVERIFY(go->run().isEmpty());
        QCOMPARE(w.session().currentPage(), first);
    }

    void anEmptyArtboardGenerates()
    {
        Window w;
        QCOMPARE(w.palette().results(QStringLiteral("something cheerful for a café")).front().where, QStringLiteral("Generate new art"));
    }

    void aFailedAskStaysOpenWithTheReason()
    {
        qputenv("FAKE_AGENT", "");
        Window w;
        w.box(20);
        CommandPalette &palette = w.palette();
        palette.search()->setText(QStringLiteral("make it teal"));
        QCOMPARE(palette.results(QStringLiteral("make it teal")).front().title, QStringLiteral("Ask the agent: make it teal"));
        QVERIFY(palette.runRow(0).startsWith(QLatin1String("Choose an agent")));
        QVERIFY(palette.isVisible());
        QVERIFY(palette.message().startsWith(QLatin1String("Choose an agent")));
        qputenv("FAKE_AGENT", "sh");
    }

    void showsRemappedKeys()
    {
        QHash<QString, ShortcutChord> values;
        values.insert(QStringLiteral("Menus:Group"), ShortcutChord(QStringLiteral("g"), 11));
        values.insert(QStringLiteral("Menus:Command Palette"), ShortcutChord(QStringLiteral("p"), 3));
        values.insert(QStringLiteral("Canvas & Layers:Pen tool"), ShortcutChord(QStringLiteral("b"), 0));
        QVERIFY(ShortcutSettings::shared().save(values));
        Window w;
        QCOMPARE(w.command(QStringLiteral("action:group"))->shortcut, key(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_G));
        QCOMPARE(w.command(QStringLiteral("tool:pen"))->shortcut, QStringLiteral("B"));
        QCOMPARE(w.view.menus()->action(QStringLiteral("commandPalette"))->shortcuts().front(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
        QSettings().remove(QLatin1String(ShortcutSettings::storageKey));
        ShortcutSettings::shared().reload();
    }
};

QTEST_MAIN(CommandPaletteTests)
#include "CommandPaletteTests.moc"
