#include "Document/PathOperations.h"
#include "System/Library.h"
#include "UI/CommandPalette.h"
#include "UI/DesignSystemPanel.h"
#include "UI/ProjectWorkspaceView.h"
#include "UI/SyncConfirmDialog.h"
#include <QFile>
#include <QMenuBar>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

// The Design System panel and the author's confirmation rule: every push and
// pull shows exact paths, the repository and branch first, and writes nothing
// until confirmed. Themes and libraries live in a temporary HOME; the Omarchy
// command is a fake through OMASTRATOR_OMARCHY.
namespace {
QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QString git(const QString &folder, const QStringList &arguments)
{
    QProcess process;
    process.setWorkingDirectory(folder);
    process.start(QStringLiteral("git"), arguments);
    process.waitForFinished();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

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
    DesignSystemPanel &panel()
    {
        QAction *entry = view.menus()->action(QStringLiteral("showDesignSystem"));
        entry->trigger();
        return *view.menus()->designSystem();
    }
};
}

class DesignSystemUiTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_DATA_HOME", (m_home.path() + QStringLiteral("/data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_home.path() + QStringLiteral("/config")).toUtf8());
    }

    void cleanup() { SyncConfirmDialog::setResponder({}); }

    void thePanelIsInTheWindowMenuWithThreeTabs()
    {
        Window window;
        DesignSystemPanel &panel = window.panel();
        QCOMPARE(panel.tabs()->count(), 3);
        QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("Tokens"));
        QCOMPARE(panel.tabs()->tabText(1), QStringLiteral("Components"));
        QCOMPARE(panel.tabs()->tabText(2), QStringLiteral("Sources"));
        window.session().addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        QCOMPARE(panel.tokenList()->topLevelItem(0)->child(0)->text(0), QStringLiteral("color/brand"));
        QVERIFY(window.view.menus()->action(QStringLiteral("makeComponent")));
        QVERIFY(window.view.menus()->action(QStringLiteral("detachInstance")));
    }

    void tokensAndComponentsAreInCtrlK()
    {
        Window window;
        EditorSession &session = window.session();
        const QString brand = session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const QUuid box = session.addPath(Shapes::rectangle(QRectF(20, 20, 60, 40)), QStringLiteral("Box"));
        session.select({box});
        CommandPalette *palette = window.view.menus()->commandPalette();
        palette->open();
        const auto find = [&](const QString &id) -> std::optional<CommandPalette::Command> {
            for (const CommandPalette::Command &each : palette->commands()) {
                if (each.id == id)
                    return each;
            }
            return std::nullopt;
        };
        const auto apply = find(QStringLiteral("token:") + brand);
        QVERIFY(apply);
        QCOMPARE(apply->title, QStringLiteral("Apply Token: color/brand"));
        QVERIFY(apply->run().isEmpty());
        QCOMPARE(session.document()->find(box)->fill.token, brand);
        palette->close();
        session.makeComponent(QStringLiteral("Card"));
        const QUuid master = session.selection().front();
        session.addVariant(master, QStringLiteral("size"), QStringLiteral("lg"));
        session.placeInstance(master, QPointF(200, 200));
        palette->open();
        QVERIFY(find(QStringLiteral("component:") + master.toString(QUuid::WithoutBraces)));
        const auto swap = find(QStringLiteral("variant:size=lg"));
        QVERIFY(swap);
        QVERIFY(swap->run().isEmpty());
        QCOMPARE(session.document()->find(session.selectedInstances().front())->instance->master,
                 Components::bestVariant(*session.document(), QStringLiteral("Card"), {{QStringLiteral("size"), QStringLiteral("lg")}}).value());
        palette->close();
    }

    void aPushShowsExactPathsRepositoryAndBranchAndWritesNothingUntilConfirmed()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git isn't installed");
        QTemporaryDir project;
        git(project.path(), {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("tokens")});
        git(project.path(), {QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("Test")});
        git(project.path(), {QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("test@example.invalid")});
        write(project.filePath(QStringLiteral("src/app.css")), "@import \"tailwindcss\";\n@theme {\n  --color-brand: #000000;\n}\n");
        git(project.path(), {QStringLiteral("add"), QStringLiteral(".")});
        git(project.path(), {QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("start")});
        Window window;
        window.session().addToken(DesignToken::color(QStringLiteral("color/brand"), QColor("#ff0000")));
        DesignSystemPanel &panel = window.panel();
        const QString css = QFileInfo(project.filePath(QStringLiteral("src/app.css"))).absoluteFilePath();
        const QString json = QFileInfo(project.filePath(QStringLiteral("tokens.json"))).absoluteFilePath();
        QString shown;
        bool untouched = false;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            shown = dialog.text();
            untouched = !QFileInfo::exists(json) && readAll(css).contains("#000000");
            return false;
        });
        QCOMPARE(panel.pushToCode(project.path()), QStringLiteral("Cancelled. Nothing was changed."));
        QVERIFY(shown.contains(css));
        QVERIFY(shown.contains(json));
        QVERIFY(shown.contains(QStringLiteral("branch tokens")));
        QVERIFY(shown.contains(QFileInfo(project.path()).canonicalFilePath()) || shown.contains(project.path()));
        QVERIFY(shown.contains(QStringLiteral("Nothing is pushed to a remote")));
        QVERIFY(shown.contains(QStringLiteral("Preview: 2 files: 1 changed, 1 new")));
        QVERIFY(untouched);
        // Cancelled: still nothing written or committed.
        QVERIFY(!QFileInfo::exists(json));
        QVERIFY(readAll(css).contains("#000000"));
        QCOMPARE(git(project.path(), {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}), QStringLiteral("1"));
        // Confirmed: written and committed on the branch shown.
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            return dialog.confirmButton() && dialog.confirmButton()->text() == QLatin1String("Write and Commit");
        });
        QCOMPARE(panel.pushToCode(project.path()), QStringLiteral("Tokens written and committed on tokens."));
        QVERIFY(readAll(css).contains("--color-brand: #ff0000;"));
        QVERIFY(QFileInfo::exists(json));
        QCOMPARE(git(project.path(), {QStringLiteral("rev-list"), QStringLiteral("--count"), QStringLiteral("HEAD")}), QStringLiteral("2"));
        QCOMPARE(git(project.path(), {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s")}),
                 QStringLiteral("Update design tokens from Omastrator"));
        QCOMPARE(git(project.path(), {QStringLiteral("status"), QStringLiteral("--porcelain")}), QString());
    }

    void aPullAsksFirstAndIsOneUndoStep()
    {
        QTemporaryDir project;
        write(project.filePath(QStringLiteral("tokens.json")), "{\"color\": {\"$type\": \"color\", \"ink\": {\"$value\": \"#123456\"}}}");
        Window window;
        DesignSystemPanel &panel = window.panel();
        QString shown;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            shown = dialog.text();
            return false;
        });
        panel.pullFromCode(project.path());
        QVERIFY(shown.contains(QFileInfo(project.filePath(QStringLiteral("tokens.json"))).absoluteFilePath()));
        QVERIFY(shown.contains(QStringLiteral("No files are written")));
        QVERIFY(window.session().document()->tokens.empty());
        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        panel.pullFromCode(project.path());
        QCOMPARE(window.session().document()->tokens.front().value.color, QColor("#123456"));
        QCOMPARE(window.session().undoName(), QStringLiteral("Pull Tokens from Code"));
    }

    void theLibrarySavesAfterConfirmingAndPlacesComponents()
    {
        Window window;
        EditorSession &session = window.session();
        session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const QUuid box = session.addPath(Shapes::rectangle(QRectF(20, 20, 60, 40)), QStringLiteral("Box"));
        session.select({box});
        session.makeComponent(QStringLiteral("Chip"));
        DesignSystemPanel &panel = window.panel();
        const QString path = Library::pathFor(QStringLiteral("Personal"));
        QVERIFY(path.startsWith(m_home.path()));
        QString shown;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            shown = dialog.text();
            return !QFileInfo::exists(path);
        });
        QCOMPARE(panel.saveToLibrary(QStringLiteral("Personal")), QStringLiteral("Saved to the library."));
        QVERIFY(shown.contains(path));
        QCOMPARE(Library::load(QStringLiteral("Personal")).sets(), QStringList{QStringLiteral("Chip")});
        // Another document places it from the library, tokens and all.
        window.workspace.createDocument(QSizeF(400, 300));
        panel.follow();
        panel.placeFromLibrary(QStringLiteral("Personal"), QStringLiteral("Chip"));
        EditorSession &other = window.session();
        QCOMPARE(Components::masters(*other.document()).size(), size_t(1));
        QCOMPARE(other.selectedInstances().size(), size_t(1));
        QCOMPARE(other.document()->tokens.size(), size_t(1));
        QCOMPARE(other.undoName(), QStringLiteral("Place Component"));
        QFile::remove(path);
    }

    void anOmarchyThemeIsSavedAsANewFolderAndAppliedThroughTheFakeCommand()
    {
        const QString source = m_home.path() + QStringLiteral("/.config/omarchy/themes/base");
        write(source + QStringLiteral("/colors.toml"), "mode = \"dark\"\naccent = \"#0a84ff\"\nbackground = \"#1a1a1c\"\n");
        write(source + QStringLiteral("/backgrounds/1.png"), "picture");
        write(m_home.path() + QStringLiteral("/.local/state/omarchy/current/theme.name"), "Base\n");
        const QString log = m_home.path() + QStringLiteral("/omarchy.log");
        const QString fake = m_home.path() + QStringLiteral("/fake-omarchy");
        write(fake, QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '%1'\n").arg(log).toUtf8());
        QFile::setPermissions(fake, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qputenv("OMASTRATOR_OMARCHY", fake.toUtf8());
        Window window;
        DesignSystemPanel &panel = window.panel();
        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        panel.useOmarchyTheme();
        EditorSession &session = window.session();
        const DesignToken *accent = DesignTokens::named(session.document()->tokens, QStringLiteral("color/accent"));
        QVERIFY(accent);
        TokenValue pink;
        pink.color = QColor("#ff375f");
        session.setTokenValue(accent->id, pink);
        const QString target = m_home.path() + QStringLiteral("/.config/omarchy/themes/hot-pink");
        QString shown;
        bool quiet = false;
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &dialog) {
            shown = dialog.text();
            quiet = !QFileInfo::exists(target) && !QFileInfo::exists(log);
            return false;
        });
        panel.saveOmarchyTheme(QStringLiteral("Hot Pink"), true);
        QVERIFY(quiet);
        QVERIFY(shown.contains(target + QStringLiteral("/colors.toml")));
        QVERIFY(shown.contains(target + QStringLiteral("/backgrounds/1.png")));
        QVERIFY(shown.contains(fake + QStringLiteral(" theme set hot-pink")));
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(!QFileInfo::exists(log));
        SyncConfirmDialog::setResponder([](SyncConfirmDialog &) { return true; });
        QCOMPARE(panel.saveOmarchyTheme(QStringLiteral("Hot Pink"), true), QStringLiteral("Theme saved and applied."));
        QVERIFY(readAll(target + QStringLiteral("/colors.toml")).contains("accent = \"#ff375f\""));
        QCOMPARE(readAll(target + QStringLiteral("/backgrounds/1.png")), QByteArray("picture"));
        QCOMPARE(readAll(log), QByteArray("theme set hot-pink\n"));
        // The theme it started from is untouched.
        QVERIFY(readAll(source + QStringLiteral("/colors.toml")).contains("#0a84ff"));
        qunsetenv("OMASTRATOR_OMARCHY");
    }

    void aFileChangedAfterThePreviewIsNotOverwritten()
    {
        QTemporaryDir project;
        write(project.filePath(QStringLiteral("tokens.json")), "{}");
        Window window;
        window.session().addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        DesignSystemPanel &panel = window.panel();
        SyncConfirmDialog::setResponder([&](SyncConfirmDialog &) {
            write(project.filePath(QStringLiteral("tokens.json")), "{\"edited\": true}");
            return true;
        });
        QVERIFY(panel.pushToCode(project.path()).contains(QStringLiteral("changed since the preview")));
        QCOMPARE(readAll(project.filePath(QStringLiteral("tokens.json"))), QByteArray("{\"edited\": true}"));
    }
};

QTEST_MAIN(DesignSystemUiTests)
#include "DesignSystemUiTests.moc"
