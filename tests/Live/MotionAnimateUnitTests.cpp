#include "Live/AgentWork.h"
#include "Live/MotionCode.h"
#include "Live/MotionContract.h"
#include "Live/MotionPrompt.h"
#include "Live/MotionStack.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

// Asking for motion (docs/MOTION.md, section 4), the parts that need no browser: what the project's stack is, what the agent is
// told, and whether what it wrote holds to the output contract.
namespace {
void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

const QByteArray block = "/* omastrator:motion rise */\n:root { --duration-rise: 480ms; --ease-rise: cubic-bezier(0.16, 1, 0.3, 1); }\n"
                         "@keyframes nl-rise { from { opacity: 0; } }\n.word { animation: nl-rise var(--duration-rise) var(--ease-rise) both; }\n"
                         "@media (prefers-reduced-motion: reduce) { .word { animation: none; } }\n/* omastrator:motion end */\n";
}

class MotionAnimateUnitTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_projects = 0;

    QString project() { return m_directory.filePath(QStringLiteral("p%1").arg(++m_projects)); }

private slots:
    void initTestCase() { QVERIFY(m_directory.isValid()); }

    void aViteTailwindProjectNamesItsThemeFile()
    {
        const QString folder = project();
        write(folder + "/package.json", R"({"dependencies": {"tailwindcss": "^4.0.1"}, "devDependencies": {"vite": "^6.0.0"}})");
        write(folder + "/src/style.css", "@import \"tailwindcss\";\n@theme {\n  --color-ink: #111;\n}\n");
        write(folder + "/src/other.css", ":root { --gap: 4px; }\n");
        write(folder + "/node_modules/pkg/theme.css", "@theme { --color-x: red; }\n");
        const MotionStack::Info info = MotionStack::detect(folder);
        QCOMPARE(info.stack, QStringLiteral("Vite + Tailwind v4"));
        QVERIFY(info.tailwindV4);
        QCOMPARE(info.styleFile, QStringLiteral("src/style.css"));
        QCOMPARE(info.tokenFile, QStringLiteral("src/style.css"));
        QVERIFY(info.libraries.isEmpty());
    }

    void aPlainPageNamesItsLinkedSheetAndTheFileWithRoot()
    {
        const QString folder = project();
        write(folder + "/index.html", "<!doctype html>\n<link rel=\"stylesheet\" href=\"css/site.css\">\n<p>Hi</p>\n");
        write(folder + "/css/site.css", "p { color: red; }\n");
        write(folder + "/css/tokens.css", ":root {\n  --brand: #e11d48;\n}\n");
        const MotionStack::Info info = MotionStack::detect(folder);
        QCOMPARE(info.stack, QStringLiteral("Plain HTML and CSS"));
        QVERIFY(!info.tailwindV4);
        QCOMPARE(info.styleFile, QStringLiteral("css/site.css"));
        // The tokens are where :root already has them.
        QCOMPARE(info.tokenFile, QStringLiteral("css/tokens.css"));
    }

    void anAstroProjectAndTheMotionLibrariesItAlreadyHas()
    {
        const QString folder = project();
        write(folder + "/package.json", R"({"dependencies": {"astro": "^5.0.0", "gsap": "^3.12.0"}, "devDependencies": {"tailwindcss": "3.4.1"}})");
        write(folder + "/src/styles/global.css", ":root { --brand: #111; }\n");
        const MotionStack::Info info = MotionStack::detect(folder);
        QCOMPARE(info.stack, QStringLiteral("Astro + Tailwind v3"));
        QVERIFY(!info.tailwindV4);
        QCOMPARE(info.styleFile, QStringLiteral("src/styles/global.css"));
        QCOMPARE(info.libraries, QStringList{"gsap"});
        QVERIFY(MotionStack::detect(m_directory.filePath(QStringLiteral("missing"))).styleFile.isEmpty());
    }

    void thePromptCarriesTheSelectionTokensStackMotionAndTheContract()
    {
        AgentWork work{"/projects/site", "/data/worktrees/site-1", "omastrator/live-1", "req-1", true};
        MotionPrompt::Brief brief;
        brief.instruction = QStringLiteral("Reveal word by word on load");
        brief.elements = QJsonArray{QJsonObject{{"selector", "#headline"}, {"tag", "h1"}, {"text", "Coffee worth a slow morning"}}};
        brief.tokens = QJsonObject{{"durations", QJsonArray{QJsonObject{{"name", "--duration-quick"}, {"value", "200ms"}}}}};
        brief.motion = QJsonArray{QJsonObject{{"selector", "#lede"}, {"name", "nl-fade"}, {"duration", 400}}};
        brief.keyframeNames = {QStringLiteral("nl-fade"), QStringLiteral("nl-pop")};
        brief.stack = MotionStack::Info{QStringLiteral("Vite + Tailwind v4"), QStringLiteral("src/style.css"), QStringLiteral("src/style.css"), true, {QStringLiteral("gsap")}};
        brief.url = QStringLiteral("https://example.com/");
        brief.width = QStringLiteral("The frame is 1280 px wide.");
        brief.command = QStringLiteral("omastrator");
        brief.screenshot = QStringLiteral("/tmp/page.png");
        brief.prefix = QStringLiteral("nl");
        const QString text = MotionPrompt::animate(work, brief);
        // The task, the worktree and the rule about not committing.
        QVERIFY(text.contains(QStringLiteral("(request req-1)")));
        QVERIFY(text.contains(QStringLiteral("/data/worktrees/site-1")));
        QVERIFY(text.contains(QStringLiteral("Don't commit, push, deploy or start a dev server")));
        // What the designer asked, the selection, the tokens, the stack, the motion already there and the names in use.
        QVERIFY(text.contains(QStringLiteral("Reveal word by word on load")));
        QVERIFY(text.contains(QStringLiteral("\"selector\": \"#headline\"")));
        QVERIFY(text.contains(QStringLiteral("--duration-quick")));
        QVERIFY(text.contains(QStringLiteral("The project: Vite + Tailwind v4. Its styles are in src/style.css.")));
        QVERIFY(text.contains(QStringLiteral("gsap")));
        QVERIFY(text.contains(QStringLiteral("nl-fade")));
        QVERIFY(text.contains(QStringLiteral("(don't reuse one for something else): nl-fade, nl-pop")));
        QVERIFY(text.contains(QStringLiteral("/tmp/page.png")));
        QVERIFY(text.contains(QStringLiteral("The frame is 1280 px wide.")));
        // The output contract.
        QVERIFY(text.contains(QStringLiteral("Plain CSS in the project's own style file (src/style.css)")));
        QVERIFY(text.contains(QStringLiteral("--duration-<name>")));
        QVERIFY(text.contains(QStringLiteral("declared in the @theme block")));
        QVERIFY(text.contains(QStringLiteral("starts with \"nl-\"")));
        QVERIFY(text.contains(QStringLiteral("var(--delay-extra, 0ms)")));
        QVERIFY(text.contains(QStringLiteral("/* omastrator:motion <name> */")));
        QVERIFY(text.contains(QStringLiteral("/* omastrator:motion end */")));
        QVERIFY(text.contains(QStringLiteral("prefers-reduced-motion: reduce")));
        QVERIFY(text.contains(QStringLiteral("\"action\": \"agentDone\", \"requestId\": \"req-1\"")));
        // One element is not a group, and the reduced-motion rule can be turned off for a run.
        QVERIFY(text.contains(QStringLiteral("about the selected element")));
        brief.reducedMotion = false;
        brief.together = true;
        brief.stack.tailwindV4 = false;
        const QString group = MotionPrompt::animate(work, brief);
        QVERIFY(group.contains(QStringLiteral("to move as one group")));
        QVERIFY(group.contains(QStringLiteral("each element carries only its index")));
        QVERIFY(group.contains(QStringLiteral("declared in :root")));
        QVERIFY(group.contains(QStringLiteral("don't write one")));
        QVERIFY(!group.contains(QStringLiteral("Include, inside the block, a rule for reduced motion")));
    }

    void keyframeNamesStartWithTheSitesShortName()
    {
        QCOMPARE(MotionPrompt::keyframePrefix(QStringLiteral("Northlight Coffee")), QStringLiteral("nc"));
        QCOMPARE(MotionPrompt::keyframePrefix(QStringLiteral("north-light-coffee-co")), QStringLiteral("nlc"));
        QCOMPARE(MotionPrompt::keyframePrefix(QStringLiteral("northlight")), QStringLiteral("no"));
        QCOMPARE(MotionPrompt::keyframePrefix(QString()), QStringLiteral("oma"));
    }

    void aBlockThatHoldsToTheContractHasNoProblems()
    {
        const QString before = project();
        const QString after = project();
        write(before + "/style.css", "body { margin: 0; }\n");
        write(after + "/style.css", QByteArray("body { margin: 0; }\n") + block);
        const MotionContract::Report report = MotionContract::check(before, after, {QStringLiteral("style.css")}, true);
        QVERIFY2(report.ok(), qPrintable(report.problems.join(QStringLiteral("; "))));
        QCOMPARE(report.blocks, QStringList{"rise"});
        QVERIFY(report.notice().isEmpty());
    }

    void aResultThatBreaksTheContractIsStillReportedWithWhy()
    {
        const QString before = project();
        write(before + "/style.css", "body { margin: 0; }\n");
        const auto checked = [&](const QByteArray &css, bool reduced = true) {
            const QString after = project();
            write(after + "/style.css", css);
            return MotionContract::check(before, after, {QStringLiteral("style.css")}, reduced);
        };
        // No marked block at all.
        MotionContract::Report report = checked("body { margin: 0; }\n.word { animation: nl-rise 1s both; }\n@keyframes nl-rise { from { opacity: 0; } }\n");
        QCOMPARE(report.problems.size(), 1);
        QVERIFY(report.problems.first().contains(QStringLiteral("isn't in a block marked")));
        QVERIFY(report.notice().contains(QStringLiteral("isn't tunable here")));
        // A block that never closes.
        report = checked("/* omastrator:motion rise */\n.word { animation: nl-rise 1s; }\n");
        QVERIFY(report.problems.first().contains(QStringLiteral("isn't closed")));
        // A token the block uses and nothing declares.
        QByteArray css = block;
        css.replace("--duration-rise: 480ms; ", "");
        report = checked(css);
        QVERIFY(!report.ok());
        QVERIFY(report.problems.join(QLatin1Char(' ')).contains(QStringLiteral("uses --duration-rise and nothing declares it")));
        // The same @keyframes name twice.
        report = checked(block + QByteArray("@keyframes nl-rise { to { opacity: 1; } }\n"));
        QVERIFY(report.problems.join(QLatin1Char(' ')).contains(QStringLiteral("@keyframes nl-rise is written 2 times")));
        // No reduced-motion rule, when it was asked for, and none needed when it wasn't.
        css = block;
        css.replace("@media (prefers-reduced-motion: reduce) { .word { animation: none; } }\n", "");
        QVERIFY(checked(css).problems.join(QLatin1Char(' ')).contains(QStringLiteral("no prefers-reduced-motion rule")));
        QVERIFY(checked(css, false).ok());
    }

    void aBlockThatWasAlreadyThereIsNotTheAgents()
    {
        const QString before = project();
        const QString after = project();
        write(before + "/style.css", block);
        write(after + "/style.css", block);
        // Nothing changed: the agent wrote no block, and that is the one problem.
        const MotionContract::Report report = MotionContract::check(before, after, {}, true);
        QVERIFY(report.blocks.isEmpty());
        QCOMPARE(report.problems.size(), 1);
    }
};

QTEST_GUILESS_MAIN(MotionAnimateUnitTests)
#include "MotionAnimateUnitTests.moc"
