#include "Live/AgentWork.h"
#include "Live/WriteBack.h"
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Phase 6 of docs/OS-SUITE.md: deterministic write-back, review and Discard,
// the agent's worktree, Save and Publish, in throwaway git repositories.
namespace {
void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString run(const QString &folder, const QStringList &arguments)
{
    QString error;
    const QString out = WriteBack::git(folder, arguments, &error);
    if (!error.isEmpty())
        qWarning().noquote() << "git" << arguments << error;
    return out;
}

LiveEdit textEdit(const QString &selector, const QString &before, const QString &after, const QString &html = QString())
{
    LiveEdit edit;
    edit.selector = selector;
    edit.property = QStringLiteral("text");
    edit.before = before;
    edit.after = after;
    edit.element = {{"selector", selector}, {"html", html}};
    return edit;
}

LiveEdit classEdit(const QString &selector, const QString &classes, const QString &property, const QString &from, const QString &to,
                   const QString &html = QString())
{
    LiveEdit edit;
    edit.selector = selector;
    edit.property = property;
    edit.removeClass = from;
    edit.addClass = to;
    edit.after = QStringLiteral("12px");
    edit.element = {{"selector", selector}, {"classes", classes}, {"html", html}};
    return edit;
}

const QByteArray page = "<!doctype html>\n<body class=\"p-4\">\n  <h1 id=\"title\" class=\"text-3xl font-bold\">Tailwind fixture</h1>\n"
                        "  <button id=\"cta\" class=\"bg-sky-500 text-white p-4 rounded-md font-bold\">Buy now</button>\n"
                        "  <p>Shared words</p>\n</body>\n";
}

class WriteBackTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_repos = 0;

    // A fresh repository holding the page, a stylesheet and a second file with repeated words.
    QString repository()
    {
        const QString folder = m_directory.filePath(QStringLiteral("repo%1").arg(++m_repos));
        write(folder + "/index.html", page);
        write(folder + "/src/style.css", ":root {\n  --brand: #e11d48;\n  --ink: #0f172a;\n}\n");
        write(folder + "/src/other.html", "<p>Shared words</p>\n");
        write(folder + "/node_modules/pkg/index.html", "<p>Buy now</p>\n");
        write(folder + "/.gitignore", "node_modules/\n");
        run(folder, {"init", "-q", "-b", "main"});
        run(folder, {"add", "-A"});
        run(folder, {"commit", "-q", "-m", "First"});
        return folder;
    }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git isn't installed.");
        QVERIFY(m_directory.isValid());
        // The user's own git settings (signing, hooks) stay out of the tests.
        qputenv("GIT_CONFIG_GLOBAL", m_directory.filePath(QStringLiteral("gitconfig")).toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        write(m_directory.filePath(QStringLiteral("gitconfig")), "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
    }

    void uniqueTextIsWrittenAndDiscardRestores()
    {
        const QString repo = repository();
        const WriteBack::Plan plan = WriteBack::plan(repo, {textEdit("#title", "Tailwind fixture", "Tailwind, edited")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(plan.changes.size(), size_t(1));
        QCOMPARE(plan.changes.front().path, repo + "/index.html");
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        QVERIFY(read(repo + "/index.html").contains(">Tailwind, edited</h1>"));
        QCOMPARE(read(repo + "/index.html").size(), page.size());
        WriteBack::Review review{"r", "Live edits", plan.done.join('\n'), plan.changes, repo, {}, QDateTime::currentDateTime()};
        QVERIFY(review.diff(repo).contains("+  <h1 id=\"title\" class=\"text-3xl font-bold\">Tailwind, edited</h1>"));
        QVERIFY(review.diff(repo).startsWith("--- index.html"));
        QVERIFY(WriteBack::restore(plan.changes).isEmpty());
        QCOMPARE(read(repo + "/index.html"), page);
        QVERIFY(WriteBack::dirtyFiles(repo).isEmpty());
    }

    void uncertainTextGoesToTheAgent()
    {
        const QString repo = repository();
        // Twice in the project, markup in the new text, or not found at all: never guessed.
        for (const LiveEdit &edit : {textEdit("p", "Shared words", "Other words"), textEdit("#title", "Tailwind fixture", "A <b>bold</b> fix"),
                                     textEdit("#x", "Nowhere", "Here")}) {
            const WriteBack::Plan plan = WriteBack::plan(repo, {edit});
            QVERIFY(plan.changes.empty());
            QCOMPARE(plan.unresolved.size(), size_t(1));
        }
        // Ignored files don't count: "Buy now" is in node_modules too, but only once where it's tracked.
        QCOMPARE(WriteBack::plan(repo, {textEdit("#cta", "Buy now", "Order")}).changes.size(), size_t(1));
        // The Vite helper's location settles a repeated string.
        const WriteBack::Plan located = WriteBack::plan(repo, {textEdit("p", "Shared words", "Other words", "<p data-oma-src=\"src/other.html:1:1\">Shared words</p>")});
        QCOMPARE(located.changes.size(), size_t(1));
        QCOMPARE(located.changes.front().path, repo + "/src/other.html");
    }

    void classSwapsAreMadeInPlace()
    {
        const QString repo = repository();
        const QString classes = QStringLiteral("bg-sky-500 text-white p-4 rounded-md font-bold");
        const WriteBack::Plan plan = WriteBack::plan(repo, {classEdit("#cta", classes, "padding", "p-4", "p-3"),
                                                            classEdit("#cta", classes, "background-color", "bg-sky-500", "bg-sky-700")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(plan.done.size(), 2);
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        QVERIFY(read(repo + "/index.html").contains("<button id=\"cta\" class=\"bg-sky-700 text-white p-3 rounded-md font-bold\">"));
        // Only the button: the body's own p-4 stays.
        QVERIFY(read(repo + "/index.html").contains("<body class=\"p-4\">"));
        // A class string the source doesn't have as a whole is left to the agent.
        const WriteBack::Plan missing = WriteBack::plan(repo, {classEdit("#title", "text-3xl font-bold extra", "font-size", "text-3xl", "text-lg")});
        QVERIFY(missing.changes.empty());
        QCOMPARE(missing.unresolved.size(), size_t(1));
        // An inline style with no class is the agent's too.
        LiveEdit style;
        style.selector = "#title";
        style.property = "margin-top";
        style.after = "12px";
        QCOMPARE(WriteBack::plan(repo, {style}).unresolved.size(), size_t(1));
    }

    void customPropertiesChangeInTheirOneStylesheet()
    {
        const QString repo = repository();
        LiveEdit brand;
        brand.selector = ":root";
        brand.property = "--brand";
        brand.after = "#be123c";
        const WriteBack::Plan plan = WriteBack::plan(repo, {brand});
        QCOMPARE(plan.changes.size(), size_t(1));
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        QCOMPARE(read(repo + "/src/style.css"), QByteArray(":root {\n  --brand: #be123c;\n  --ink: #0f172a;\n}\n"));
        brand.property = "--missing";
        QCOMPARE(WriteBack::plan(repo, {brand}).unresolved.size(), size_t(1));
    }

    void dirtyFilesAreNamed()
    {
        const QString repo = repository();
        QVERIFY(WriteBack::dirtyFiles(repo, {"index.html"}).isEmpty());
        write(repo + "/index.html", page + "<!-- mine -->\n");
        QCOMPARE(WriteBack::dirtyFiles(repo, {"index.html", "src/style.css"}), QStringList{"index.html"});
        QVERIFY(WriteBack::isGitRepository(repo));
        QVERIFY(!WriteBack::isGitRepository(m_directory.path()));
    }

    void commitsTakeOnlyOmastratorsOwnChange()
    {
        const QString repo = repository();
        write(repo + "/src/other.html", "<p>The user's own work</p>\n");
        run(repo, {"add", "src/other.html"});
        const WriteBack::Plan plan = WriteBack::plan(repo, {textEdit("#cta", "Buy now", "Order now")});
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        const QString message = WriteBack::commitMessage(plan.done);
        QCOMPARE(message, QStringLiteral("Change the text “Buy now” to “Order now”"));
        QString sha;
        QVERIFY(WriteBack::commit(repo, {{repo + "/index.html", plan.changes.front().before}}, message, &sha).isEmpty());
        QCOMPARE(run(repo, {"rev-parse", "HEAD"}).trimmed(), sha);
        QCOMPARE(run(repo, {"log", "-1", "--format=%s"}).trimmed(), message);
        QCOMPARE(run(repo, {"show", "--name-only", "--format=", "HEAD"}).trimmed(), QStringLiteral("index.html"));
        // What the user staged stays staged, not committed; the committed file is clean.
        QCOMPARE(run(repo, {"diff", "--cached", "--name-only"}).trimmed(), QStringLiteral("src/other.html"));
        QVERIFY(WriteBack::dirtyFiles(repo, {"index.html"}).isEmpty());
        QVERIFY(WriteBack::commitMessage({"a", "b"}).startsWith(QLatin1String("Live edits from Omastrator\n\n- a\n- b")));

        // The user's uncommitted edit in the same file stays on disk and out of the commit.
        const QByteArray mine = read(repo + "/index.html").replace("<body class=\"p-4\">", "<body class=\"p-4\"><!-- mine -->");
        write(repo + "/index.html", mine);
        const WriteBack::Plan second = WriteBack::plan(repo, {textEdit("p", "Shared words", "Kept words", "<p data-oma-src=\"index.html:5:3\">")});
        QCOMPARE(second.changes.size(), size_t(1));
        QVERIFY(WriteBack::apply(second.changes).isEmpty());
        QVERIFY(WriteBack::commit(repo, {{repo + "/index.html", second.changes.front().before}}, "Second", &sha).isEmpty());
        const QString committed = run(repo, {"show", "HEAD:index.html"});
        QVERIFY(committed.contains("Kept words") && !committed.contains("<!-- mine -->"));
        QVERIFY(read(repo + "/index.html").contains("<!-- mine -->") && read(repo + "/index.html").contains("Kept words"));
        QCOMPARE(WriteBack::dirtyFiles(repo, {"index.html"}), QStringList{"index.html"});
        QCOMPARE(run(repo, {"diff", "HEAD", "--", "index.html"}).count(QStringLiteral("\n+<")), 1);

        // Omastrator's change and HEAD's differ on the very same line: a real clash, and nothing is committed.
        const QString before = run(repo, {"rev-parse", "HEAD"}).trimmed();
        const QByteArray base = read(repo + "/index.html");
        write(repo + "/index.html", QByteArray(base).replace("Kept words", "Kept words, twice"));
        QVERIFY(WriteBack::commit(repo, {{repo + "/index.html", QByteArray(base).replace("<p>Kept words</p>", "<p>Mine</p>")}}, "Clash", &sha)
                    .contains(QLatin1String("overlap")));
        QCOMPARE(run(repo, {"rev-parse", "HEAD"}).trimmed(), before);
    }

    void reverseTakesOutOneChangeAroundNewerEdits()
    {
        const QString repo = repository();
        const WriteBack::Plan plan = WriteBack::plan(repo, {textEdit("#cta", "Buy now", "Order now")});
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        // A later edit elsewhere in the file survives the undo.
        write(repo + "/index.html", read(repo + "/index.html").replace("<body class=\"p-4\">", "<body class=\"p-8\">"));
        QString error;
        const auto undo = WriteBack::reverse(plan.changes, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(WriteBack::apply(undo).isEmpty());
        QVERIFY(read(repo + "/index.html").contains("Buy now") && read(repo + "/index.html").contains("p-8"));
        QVERIFY(!WriteBack::merge("a\nb\n", "a\nc\n", "a\nd\n").has_value());
        QCOMPARE(*WriteBack::merge("a\nb\nc\n", "x\nb\nc\n", "a\nb\ny\n"), QByteArray("x\nb\ny\n"));
    }

    void theAgentWorksInAWorktree()
    {
        const QString repo = repository();
        AgentWork work{repo, {}, {}, "req-1", false};
        QVERIFY(work.prepare().isEmpty());
        QVERIFY(QFileInfo(work.worktree + "/index.html").exists());
        QVERIFY(work.worktree.startsWith(m_directory.path()));
        QVERIFY(work.branch.startsWith(QLatin1String("omastrator/live-")));
        const QString prompt = work.prompt({"Make the button rounder", {textEdit("#cta", "Buy now", "Order")}, {}, "/tmp/shot.png",
                                            "http://localhost:5173/", "omastrator"});
        QVERIFY(prompt.contains(work.worktree) && prompt.contains("Make the button rounder") && prompt.contains("\"agentDone\"")
                && prompt.contains("req-1") && prompt.contains("/tmp/shot.png") && prompt.contains("Don't commit"));

        // The agent edits and adds files in its worktree; the user's checkout is untouched until collected.
        write(work.worktree + "/index.html", QByteArray(page).replace("rounded-md", "rounded-xl"));
        write(work.worktree + "/src/new.css", ".x{}\n");
        QCOMPARE(read(repo + "/index.html"), page);
        QString error;
        const auto changes = work.collect(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(changes.size(), size_t(2));
        QVERIFY(WriteBack::apply(changes).isEmpty());
        QVERIFY(read(repo + "/index.html").contains("rounded-xl"));
        QVERIFY(QFileInfo(repo + "/src/new.css").exists());
        // Discard puts back exactly what was there, and removes what wasn't.
        QVERIFY(WriteBack::restore(changes).isEmpty());
        QCOMPARE(read(repo + "/index.html"), page);
        QVERIFY(!QFileInfo(repo + "/src/new.css").exists());
        work.cleanup();
        QVERIFY(!QFileInfo(work.worktree).exists());
        QVERIFY(!run(repo, {"branch", "--list", "omastrator/*"}).contains("omastrator/"));
    }

    void theAgentWaitsForTheUsersOwnChanges()
    {
        const QString repo = repository();
        AgentWork work{repo, {}, {}, "req-2", false};
        QVERIFY(work.prepare().isEmpty());
        write(work.worktree + "/index.html", QByteArray(page).replace("Buy now", "Order"));
        // The user changed another line of the same file meanwhile.
        const QByteArray mine = QByteArray(page).replace("<body class=\"p-4\">", "<body class=\"p-6\">");
        write(repo + "/index.html", mine);
        QString error;
        QVERIFY(work.collect(&error).empty());
        QVERIFY(error.contains(QLatin1String("index.html")));
        QCOMPARE(read(repo + "/index.html"), mine);
        // Confirmed, the agent's change is merged into theirs, and restore returns theirs.
        work.confirmedDirty = true;
        const auto changes = work.collect(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(read(repo + "/index.html"), mine);
        QVERIFY(WriteBack::apply(changes).isEmpty());
        QVERIFY(read(repo + "/index.html").contains("Order") && read(repo + "/index.html").contains("p-6"));
        QVERIFY(WriteBack::restore(changes).isEmpty());
        QCOMPARE(read(repo + "/index.html"), mine);
        work.cleanup();
        // Outside git there's no agent path.
        const QString loose = m_directory.filePath(QStringLiteral("loose"));
        write(loose + "/index.html", "<p>x</p>");
        AgentWork outside{loose, {}, {}, "r", false};
        QVERIFY(outside.prepare().contains(QLatin1String("git")));
        QCOMPARE(WriteBack::trackedFiles(loose), QStringList{"index.html"});
    }
};

QTEST_GUILESS_MAIN(WriteBackTests)
#include "WriteBackTests.moc"
