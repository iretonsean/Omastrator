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

    // A repository whose one stylesheet is `css`, for the rules motion adds.
    QString motionRepository(const QByteArray &css)
    {
        const QString folder = m_directory.filePath(QStringLiteral("repo%1").arg(++m_repos));
        write(folder + "/index.html", "<!doctype html>\n<link rel=\"stylesheet\" href=\"src/motion.css\">\n<p id=\"guji\">Guji</p>\n");
        write(folder + "/src/motion.css", css);
        run(folder, {"init", "-q", "-b", "main"});
        run(folder, {"add", "-A"});
        run(folder, {"commit", "-q", "-m", "First"});
        return folder;
    }

    static LiveEdit property(const QString &selector, const QString &name, const QString &value)
    {
        LiveEdit edit;
        edit.selector = selector;
        edit.property = name;
        edit.after = value;
        return edit;
    }

    static LiveEdit keyframe(const QString &name, const QString &frame, const QString &property, const QString &value)
    {
        LiveEdit edit;
        edit.selector = QStringLiteral("@keyframes ") + name;
        edit.property = frame + QLatin1Char(' ') + property;
        edit.after = value;
        return edit;
    }

    // Writes the plan and gives the stylesheet as it is then.
    static QByteArray applied(const QString &folder, const WriteBack::Plan &plan)
    {
        const QString failure = WriteBack::apply(plan.changes);
        if (!failure.isEmpty())
            qWarning().noquote() << failure;
        return read(folder + "/src/motion.css");
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

    // Motion (docs/MOTION.md, section 3): the two rules write-back adds, and the reduced-motion block.
    void aScopedCustomPropertyIsWrittenInTheRuleThatNamesTheElement()
    {
        const QByteArray css = "#guji  { --i: 1; }\n#huila { --i: 2; }\n#nyeri { --i: 0; }\n.card { animation-delay: calc(var(--i) * 140ms); }\n";
        const QString repo = motionRepository(css);
        // --i is declared three times, so no one declaration is "the" --i; the rule for #huila is.
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#huila", "--i", "5")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(plan.changes.size(), size_t(1));
        QCOMPARE(applied(repo, plan), QByteArray("#guji  { --i: 1; }\n#huila { --i: 5; }\n#nyeri { --i: 0; }\n.card { animation-delay: calc(var(--i) * 140ms); }\n"));
        QCOMPARE(plan.done, QStringList{"#huila: --i to 5"});
    }

    void aScopedCustomPropertyIsAddedToTheRuleThatLacksIt()
    {
        const QString repo = motionRepository("#guji  { --i: 1; }\n#huila {\n  --i: 2;\n}\n");
        WriteBack::Plan plan = WriteBack::plan(repo, {property("#guji", "--delay-extra", "200ms")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("#guji  { --i: 1; --delay-extra: 200ms; }\n#huila {\n  --i: 2;\n}\n"));
        // A rule of several lines gets a line of its own, at the same indent.
        plan = WriteBack::plan(repo, {property("#huila", "--delay-extra", "80ms")});
        QCOMPARE(applied(repo, plan), QByteArray("#guji  { --i: 1; --delay-extra: 200ms; }\n#huila {\n  --i: 2;\n  --delay-extra: 80ms;\n}\n"));
    }

    void aRuleThatOccursTwiceIsLeftForTheAgent()
    {
        const QByteArray css = "#guji { --i: 1; }\n@media (min-width: 800px) { #guji { --i: 2; } }\n";
        const QString repo = motionRepository(css);
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#guji", "--i", "4")});
        QVERIFY(plan.changes.empty());
        QCOMPARE(plan.unresolved.size(), size_t(1));
        QCOMPARE(read(repo + "/src/motion.css"), css);
    }

    void oneElementsEditNeverLandsInAnotherElementsRule()
    {
        // --i is declared once, on #guji: an edit for #huila is not that declaration.
        const QByteArray css = "#guji { --i: 1; }\n";
        const QString repo = motionRepository(css);
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#huila", "--i", "4")});
        QVERIFY(plan.changes.empty());
        QCOMPARE(plan.unresolved.size(), size_t(1));
        // And the same edit on #guji is that declaration.
        QCOMPARE(WriteBack::plan(repo, {property("#guji", "--i", "4")}).changes.size(), size_t(1));
    }

    void aMotionTokenOnRootIsWrittenWhereItIsDeclared()
    {
        const QString repo = motionRepository(":root {\n  --duration-reveal: 480ms;\n  --ease-reveal: cubic-bezier(0.16, 1, 0.3, 1);\n}\n#guji { --i: 1; }\n#huila { --i: 2; }\n");
        const WriteBack::Plan plan = WriteBack::plan(repo, {property(":root", "--duration-reveal", "600ms"), property(":root", "--ease-reveal", "cubic-bezier(0.34, 1.56, 0.64, 1)")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray(":root {\n  --duration-reveal: 600ms;\n  --ease-reveal: cubic-bezier(0.34, 1.56, 0.64, 1);\n}\n#guji { --i: 1; }\n#huila { --i: 2; }\n"));
    }

    void aKeyframeValueIsWrittenInItsOneNamedBlock()
    {
        const QByteArray css = "@keyframes nl-rise {\n  from { opacity: 0; translate: 0 24px; }\n  to { opacity: 1; }\n}\n@keyframes nl-fade { from { opacity: 0; } }\n.a { animation: nl-rise 1s; }\n";
        const QString repo = motionRepository(css);
        WriteBack::Plan plan = WriteBack::plan(repo, {keyframe("nl-rise", "from", "translate", "0 40px"), keyframe("nl-rise", "to", "opacity", "0.9")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(plan.done.size(), 2);
        QCOMPARE(applied(repo, plan), QByteArray("@keyframes nl-rise {\n  from { opacity: 0; translate: 0 40px; }\n  to { opacity: 0.9; }\n}\n@keyframes nl-fade { from { opacity: 0; } }\n.a { animation: nl-rise 1s; }\n"));
        // The same value in the other block is not touched, and 0% is what "from" is.
        plan = WriteBack::plan(repo, {keyframe("nl-fade", "0%", "opacity", "0.2")});
        QVERIFY(plan.unresolved.empty());
        QVERIFY(applied(repo, plan).contains("@keyframes nl-fade { from { opacity: 0.2; } }"));
    }

    void aKeyframeValueIsLeftWhenItsBlockOccursTwiceOrIsNotThere()
    {
        const QString twice = motionRepository("@keyframes nl-rise { from { opacity: 0; } }\n@keyframes nl-rise { from { opacity: 0; } }\n");
        QCOMPARE(WriteBack::plan(twice, {keyframe("nl-rise", "from", "opacity", "0.5")}).unresolved.size(), size_t(1));
        QCOMPARE(read(twice + "/src/motion.css"), QByteArray("@keyframes nl-rise { from { opacity: 0; } }\n@keyframes nl-rise { from { opacity: 0; } }\n"));
        const QString missing = motionRepository("@keyframes nl-rise { from { opacity: 0; } to { opacity: 1; } }\n");
        // No such block, no such frame, and a property the frame doesn't have.
        QCOMPARE(WriteBack::plan(missing, {keyframe("nl-other", "from", "opacity", "0.5")}).unresolved.size(), size_t(1));
        QCOMPARE(WriteBack::plan(missing, {keyframe("nl-rise", "50%", "opacity", "0.5")}).unresolved.size(), size_t(1));
        QCOMPARE(WriteBack::plan(missing, {keyframe("nl-rise", "from", "translate", "0 4px")}).unresolved.size(), size_t(1));
        // A block that doesn't close is not one whose braces balance.
        const QString open = motionRepository("@keyframes nl-rise { from { opacity: 0; }\n");
        QCOMPARE(WriteBack::plan(open, {keyframe("nl-rise", "from", "opacity", "0.5")}).unresolved.size(), size_t(1));
    }

    void aFramesListIsLeftForTheAgentAndItsSingleFramesAreWritten()
    {
        // The page's preview changes the one offset asked for; the code would change both frames of "0%, 100%": not the same.
        const QByteArray css = "@keyframes nl-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }\n";
        const QString repo = motionRepository(css);
        WriteBack::Plan plan = WriteBack::plan(repo, {keyframe("nl-pulse", "to", "opacity", "0.8")});
        QCOMPARE(plan.unresolved.size(), size_t(1));
        QVERIFY(plan.changes.empty());
        QCOMPARE(read(repo + "/src/motion.css"), css);
        plan = WriteBack::plan(repo, {keyframe("nl-pulse", "50%", "opacity", "0.2")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("@keyframes nl-pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.2; } }\n"));
    }

    void aValueOfSeveralLinesIsReplacedWholeInAKeyframeAndAProperty()
    {
        // Prettier writes a shadow list this way; a value that stopped at the first line end would leave "0 0 0 4px blue;" behind.
        const QByteArray css = "@keyframes nl-pulse {\n  from {\n    box-shadow: 0 0 0 0 red,\n      0 0 0 4px blue;\n    opacity: 1;\n  }\n}\n"
                               "#guji {\n  --shadow: 0 0 red,\n    0 0 blue;\n}\n";
        const QString repo = motionRepository(css);
        WriteBack::Plan plan = WriteBack::plan(repo, {keyframe("nl-pulse", "from", "box-shadow", "0 0 0 8px green")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan),
                 QByteArray("@keyframes nl-pulse {\n  from {\n    box-shadow: 0 0 0 8px green;\n    opacity: 1;\n  }\n}\n#guji {\n  --shadow: 0 0 red,\n    0 0 blue;\n}\n"));
        plan = WriteBack::plan(repo, {property("#guji", "--shadow", "0 0 green")});
        QVERIFY(plan.unresolved.empty());
        const QByteArray after = applied(repo, plan);
        QVERIFY2(after.contains("--shadow: 0 0 green;\n}"), after.constData());
        QVERIFY(!after.contains("0 0 blue"));
    }

    void aDeclarationInsideACommentIsNotTheDeclaration()
    {
        const QString repo = motionRepository("#guji { /* --i: 2; */ --i: 1; }\n#huila { /* --i: 2; */ }\n");
        // The live one is found, not the commented one; a rule with only a comment gets the property added.
        WriteBack::Plan plan = WriteBack::plan(repo, {property("#guji", "--i", "5")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("#guji { /* --i: 2; */ --i: 5; }\n#huila { /* --i: 2; */ }\n"));
        plan = WriteBack::plan(repo, {property("#huila", "--i", "7")});
        QVERIFY(plan.unresolved.empty());
        const QByteArray after = applied(repo, plan);
        QVERIFY2(after.contains("/* --i: 2; */") && after.contains("--i: 7;"), after.constData());
    }

    void aValueThatWouldBreakTheStylesheetIsLeftForTheAgent()
    {
        const QByteArray css = "@keyframes nl-rise { from { opacity: 0; } }\n#guji { --i: 1; }\n";
        const QString repo = motionRepository(css);
        for (const QString &value : {QStringLiteral("0; } body { display: none"), QStringLiteral("1; --x: 2"), QStringLiteral("{}")}) {
            WriteBack::Plan plan = WriteBack::plan(repo, {keyframe("nl-rise", "from", "opacity", value)});
            QCOMPARE(plan.unresolved.size(), size_t(1));
            QVERIFY(plan.changes.empty());
            plan = WriteBack::plan(repo, {property("#guji", "--i", value)});
            QCOMPARE(plan.unresolved.size(), size_t(1));
            QVERIFY(plan.changes.empty());
        }
        QCOMPARE(read(repo + "/src/motion.css"), css);
    }

    void anElementsValueIsNeverWrittenIntoTheOneDeclarationTheProjectHas()
    {
        // A Svelte loop writes each word's index inline: the project has one "--i:" and it is not a rule's.
        const QString svelte = m_directory.filePath(QStringLiteral("svelte%1").arg(++m_repos));
        write(svelte + "/src/Headline.svelte", "{#each words as word, i}\n  <span class=\"word\" style=\"--i: {i}\">{word}</span>\n{/each}\n");
        write(svelte + "/index.html", "<p>x</p>\n");
        run(svelte, {"init", "-q", "-b", "main"});
        run(svelte, {"add", "-A"});
        run(svelte, {"commit", "-q", "-m", "First"});
        WriteBack::Plan plan = WriteBack::plan(svelte, {property("#guji", "--i", "2"), property("#huila", "--delay-extra", "300ms")});
        QCOMPARE(plan.unresolved.size(), size_t(2));
        QVERIFY(plan.changes.empty());

        // A default on :root that every element reads: an element's edit is not the default's.
        const QByteArray css = ":root { --i: 0; --delay-extra: 0ms; }\n.card { animation-delay: calc(var(--i) * 140ms + var(--delay-extra, 0ms)); }\n";
        const QString root = motionRepository(css);
        plan = WriteBack::plan(root, {property("#guji", "--i", "1"), property("#guji", "--delay-extra", "300ms")});
        QCOMPARE(plan.unresolved.size(), size_t(2));
        QVERIFY(plan.changes.empty());
        QCOMPARE(read(root + "/src/motion.css"), css);
        // "Use the group's timing" writes an empty value: it must not blank the default for every card.
        LiveEdit back = property("#guji", "--delay-extra", QString());
        plan = WriteBack::plan(root, {back});
        QCOMPARE(plan.unresolved.size(), size_t(1));
        QVERIFY(plan.changes.empty());
        // A token on :root is still written where it is declared.
        plan = WriteBack::plan(root, {property(":root", "--delay-extra", "10ms")});
        QVERIFY(plan.unresolved.empty());
        QVERIFY(QString::fromUtf8(*plan.changes.front().after).contains(QStringLiteral("--delay-extra: 10ms;")));
    }

    void aWholeFrameIsReplacedAndAFrameWithItsBraceOnTheLastLineIsLeftForTheAgent()
    {
        // Lines that keep their indent, and the brace its own.
        const QByteArray lined = "@keyframes nl-rise {\n  from {\n    opacity: 0;\n    translate: 0 44px;\n    rotate: -3deg;\n  }\n}\n";
        const QString repo = motionRepository(lined);
        WriteBack::Plan plan = WriteBack::plan(repo, {keyframe("nl-rise", "from", "*", "opacity: 0; scale: 0.9")});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("@keyframes nl-rise {\n  from {\n    opacity: 0;\n    scale: 0.9;\n  }\n}\n"));
        // The last declaration shares its line with the brace: what the text after the last line break is cannot be told apart from
        // the brace's indent, so a declaration would stay behind. The agent writes it.
        const QByteArray shared = "@keyframes nl-rise {\n  from { opacity: 0;\n  translate: 0 44px; }\n}\n";
        const QString other = motionRepository(shared);
        plan = WriteBack::plan(other, {keyframe("nl-rise", "from", "*", "opacity: 0; scale: 0.9")});
        QCOMPARE(plan.unresolved.size(), size_t(1));
        QVERIFY(plan.changes.empty());
        QCOMPARE(read(other + "/src/motion.css"), shared);
    }

    void aDeclarationOnTheNextLineIsTakenOutWholeWhenGivenBack()
    {
        const QByteArray css = "#huila {\n  --i: 2;\n  --delay-extra:\n    300ms;\n}\n";
        const QString repo = motionRepository(css);
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#huila", "--delay-extra", QString())});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("#huila {\n  --i: 2;\n}\n"));
    }

    void aCardsOwnDelayIsTakenOffItsRuleWhenGivenBack()
    {
        // The code side of "Use the group's timing": an empty value takes the declaration out and leaves the rest of the rule.
        const QString repo = motionRepository("#huila { --i: 2; --delay-extra: 300ms; }\n");
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#huila", "--delay-extra", QString())});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), QByteArray("#huila { --i: 2; }\n"));
    }

    void aStyleBlockInMarkupIsScannedAndTheMarkupAroundItIsNot()
    {
        // The apostrophes in the title and the text are HTML's: as CSS they would open a string that swallows the braces.
        const QString repo = m_directory.filePath(QStringLiteral("markup%1").arg(++m_repos));
        write(repo + "/index.html",
              "<!doctype html>\n<title>Joe's shop</title>\n<style>\n  #guji { --i: 1; }\n  #huila { --i: 2; }\n</style>\n<p>It's done.</p>\n"
              "<style>\n  #nyeri { --i: 3; }\n</style>\n");
        run(repo, {"init", "-q", "-b", "main"});
        run(repo, {"add", "-A"});
        run(repo, {"commit", "-q", "-m", "First"});
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#guji", "--i", "8"), property("#huila", "--i", "9"), property("#nyeri", "--i", "4")});
        QVERIFY(plan.unresolved.empty());
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        QCOMPARE(read(repo + "/index.html"),
                 QByteArray("<!doctype html>\n<title>Joe's shop</title>\n<style>\n  #guji { --i: 8; }\n  #huila { --i: 9; }\n</style>\n<p>It's done.</p>\n"
                            "<style>\n  #nyeri { --i: 4; }\n</style>\n"));
    }

    void aSassLineCommentIsNotCode()
    {
        // The apostrophes in the // comments would open strings; the // after a rule ends with its line.
        const QString repo = m_directory.filePath(QStringLiteral("sass%1").arg(++m_repos));
        write(repo + "/src/motion.scss", "// it's the reveal's own rule { }\n#guji { --i: 1; }\n#huila { --i: 2; } // don't touch\n#nyeri { background: url(//cdn.example/x.png); --i: 3; }\n");
        write(repo + "/index.html", "<p>x</p>\n");
        run(repo, {"init", "-q", "-b", "main"});
        run(repo, {"add", "-A"});
        run(repo, {"commit", "-q", "-m", "First"});
        const WriteBack::Plan plan = WriteBack::plan(repo, {property("#huila", "--i", "6"), property("#nyeri", "--i", "7")});
        QVERIFY(plan.unresolved.empty());
        QVERIFY(WriteBack::apply(plan.changes).isEmpty());
        QCOMPARE(read(repo + "/src/motion.scss"),
                 QByteArray("// it's the reveal's own rule { }\n#guji { --i: 1; }\n#huila { --i: 6; } // don't touch\n#nyeri { background: url(//cdn.example/x.png); --i: 7; }\n"));
    }

    void reducedMotionIsTakenOutOfItsBlockAndPutBack()
    {
        const QByteArray rule = "@media (prefers-reduced-motion: reduce) { .word { animation: none; } }";
        const QByteArray css = "/* omastrator:motion reveal */\n.word { animation: nl-rise 1s both; }\n" + rule + "\n/* omastrator:motion end */\n.other { color: red; }\n";
        const QString repo = motionRepository(css);
        LiveEdit off;
        off.selector = "motion:reveal";
        off.property = "reduced-motion";
        off.before = QString::fromUtf8(rule);
        WriteBack::Plan plan = WriteBack::plan(repo, {off});
        QVERIFY(plan.unresolved.empty());
        const QByteArray without = applied(repo, plan);
        QCOMPARE(without, QByteArray("/* omastrator:motion reveal */\n.word { animation: nl-rise 1s both; }\n/* omastrator:motion end */\n.other { color: red; }\n"));
        QCOMPARE(plan.done, QStringList{"Reduced motion off for reveal"});
        // Put back, it is the same text on its own line: the file is what it was.
        LiveEdit on;
        on.selector = "motion:reveal";
        on.property = "reduced-motion";
        on.after = QString::fromUtf8(rule);
        plan = WriteBack::plan(repo, {on});
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(applied(repo, plan), css);
        // Off and on again in one session nets to nothing: the code is left alone.
        LiveEdit net;
        net.selector = "motion:reveal";
        net.property = "reduced-motion";
        net.before = QString::fromUtf8(rule);
        net.after = QString::fromUtf8(rule);
        plan = WriteBack::plan(repo, {net});
        QVERIFY(plan.unresolved.empty());
        QVERIFY(plan.changes.empty());
    }

    void reducedMotionIsLeftWhenTheCodeIsNotWhatWasSeen()
    {
        const QByteArray rule = "@media (prefers-reduced-motion: reduce) { .word { animation: none; } }";
        const QString repo = motionRepository("/* omastrator:motion reveal */\n.word { animation: nl-rise 1s both; }\n" + rule + "\n/* omastrator:motion end */\n");
        LiveEdit off;
        off.selector = "motion:reveal";
        off.property = "reduced-motion";
        // The text that was seen is not the text there.
        off.before = "@media (prefers-reduced-motion: reduce) { .word { animation: paused; } }";
        QCOMPARE(WriteBack::plan(repo, {off}).unresolved.size(), size_t(1));
        // A block that isn't there at all.
        off.selector = "motion:elsewhere";
        off.before = QString::fromUtf8(rule);
        QCOMPARE(WriteBack::plan(repo, {off}).unresolved.size(), size_t(1));
        // Putting back a rule the block has already.
        LiveEdit on;
        on.selector = "motion:reveal";
        on.property = "reduced-motion";
        on.after = QString::fromUtf8(rule);
        QCOMPARE(WriteBack::plan(repo, {on}).unresolved.size(), size_t(1));
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
