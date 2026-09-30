#include "Document/EditorSession.h"
#include "Live/Browser.h"
#include "System/Library.h"
#include "System/OmarchyThemes.h"
#include "System/ProjectCode.h"
#include "System/SiteExtract.h"
#include "System/TokenFiles.h"
#include "TokenFixtures.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Where a design system lives (docs/DESIGN-SYSTEMS.md): tokens.json, Tailwind
// and CSS read and written on temporary projects, the global library and
// Omarchy themes in a temporary HOME, and a site's system read in headless
// Chromium. Every plan here is only built; nothing runs without the dialog.
namespace {
void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

const DesignToken *named(const std::vector<DesignToken> &tokens, const QString &name)
{
    return DesignTokens::named(tokens, name);
}
}

class DesignSourcesTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        // Libraries and themes live under this HOME, never the user's.
        qputenv("HOME", m_home.path().toUtf8());
        qputenv("XDG_DATA_HOME", (m_home.path() + QStringLiteral("/data")).toUtf8());
    }

    void w3cReadsAliasesModesAndComposites()
    {
        const TokenFiles::Read read = TokenFiles::readW3c(w3cFixture);
        QCOMPARE(named(read.tokens, QStringLiteral("color/brand"))->value.color, QColor("#0a84ff"));
        QCOMPARE(named(read.tokens, QStringLiteral("color/link"))->value.color, QColor("#0a84ff"));
        QCOMPARE(named(read.tokens, QStringLiteral("color/surface"))->modes.at(QStringLiteral("dark")).color, QColor("#111111"));
        QCOMPARE(read.modes, QStringList({QStringLiteral("light"), QStringLiteral("dark")}));
        QCOMPARE(named(read.tokens, QStringLiteral("space/4"))->value.number, 16.0);
        QCOMPARE(named(read.tokens, QStringLiteral("space/2"))->value.number, 8.0);
        QCOMPARE(named(read.tokens, QStringLiteral("radius/md"))->kind, TokenKind::radius);
        const TypeValue body = named(read.tokens, QStringLiteral("text/body"))->value.type;
        QCOMPARE(body.family, QStringLiteral("Inter"));
        QCOMPARE(body.lineHeight, std::optional<double>(24));
        QCOMPARE(named(read.tokens, QStringLiteral("shadow/sm"))->value.shadow.blur, 2.0);
        // Durations are read now (docs/MOTION.md, section 6): motion/fast is a token, in ms.
        QVERIFY(!read.skipped.contains(QStringLiteral("motion/fast")));
        QCOMPARE(named(read.tokens, QStringLiteral("motion/fast"))->kind, TokenKind::duration);
        QCOMPARE(named(read.tokens, QStringLiteral("motion/fast"))->value.number, 100.0);
    }

    void w3cRoundTripsAndKeepsWhatItDoesntKnow()
    {
        TokenFiles::Read read = TokenFiles::readW3c(w3cFixture);
        for (DesignToken &token : read.tokens) {
            if (token.name == QLatin1String("color/brand"))
                token.value.color = QColor("#ff3b30");
        }
        read.tokens.push_back(DesignToken::number(TokenKind::spacing, QStringLiteral("space/8"), 32));
        const QByteArray written = TokenFiles::writeW3c(read.tokens, w3cFixture);
        const QJsonObject json = QJsonDocument::fromJson(written).object();
        QCOMPARE(json["color"].toObject()["brand"].toObject()["$description"].toString(), QStringLiteral("Buttons and links"));
        QVERIFY(json["motion"].toObject().contains("fast"));
        const TokenFiles::Read again = TokenFiles::readW3c(written);
        QCOMPARE(named(again.tokens, QStringLiteral("color/brand"))->value.color, QColor("#ff3b30"));
        QCOMPARE(named(again.tokens, QStringLiteral("space/8"))->value.number, 32.0);
        QCOMPARE(named(again.tokens, QStringLiteral("color/surface"))->modes.at(QStringLiteral("dark")).color, QColor("#111111"));
        QCOMPARE(named(again.tokens, QStringLiteral("text/body"))->value.type.lineHeight, std::optional<double>(24));
        // A new file uses the 2025 object forms.
        const QJsonObject fresh = QJsonDocument::fromJson(TokenFiles::writeW3c({DesignToken::color(QStringLiteral("color/ink"), Qt::black)})).object();
        QCOMPARE(fresh["color"].toObject()["ink"].toObject()["$value"].toObject()["hex"].toString(), QStringLiteral("#000000"));
    }

    // Motion tokens (docs/MOTION.md, section 6): duration/*, ease/* and stagger/* in each file the project keeps them in.
    void motionTokensRoundTripThroughW3c()
    {
        const std::vector<DesignToken> tokens{DesignToken::number(TokenKind::duration, QStringLiteral("duration/reveal"), 480),
                                              DesignToken::number(TokenKind::duration, QStringLiteral("stagger/words"), 60),
                                              DesignToken::easing(QStringLiteral("ease/reveal"), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)")),
                                              DesignToken::easing(QStringLiteral("ease/plain"), QStringLiteral("ease-out")),
                                              DesignToken::easing(QStringLiteral("ease/table"), QStringLiteral("linear(0, 0.4, 1)"))};
        const QByteArray written = TokenFiles::writeW3c(tokens);
        const QJsonObject json = QJsonDocument::fromJson(written).object();
        // A new file uses the 2025 forms: a duration is {value, unit}, a curve is four numbers.
        const QJsonObject reveal = json["duration"].toObject()["reveal"].toObject();
        QCOMPARE(reveal["$type"].toString(), QStringLiteral("duration"));
        QCOMPARE(reveal["$value"].toObject()["value"].toDouble(), 480.0);
        QCOMPARE(reveal["$value"].toObject()["unit"].toString(), QStringLiteral("ms"));
        // A stagger is a duration in a `stagger` group.
        QCOMPARE(json["stagger"].toObject()["words"].toObject()["$type"].toString(), QStringLiteral("duration"));
        const QJsonObject curve = json["ease"].toObject()["reveal"].toObject();
        QCOMPARE(curve["$type"].toString(), QStringLiteral("cubicBezier"));
        QCOMPARE(curve["$value"].toArray().size(), 4);
        QCOMPARE(curve["$value"].toArray()[1].toDouble(), 1.0);
        // A keyword or linear() is text, not a curve of four numbers.
        QCOMPARE(json["ease"].toObject()["plain"].toObject()["$value"].toString(), QStringLiteral("ease-out"));
        QCOMPARE(json["ease"].toObject()["plain"].toObject()["$type"].toString(), QStringLiteral("string"));

        const TokenFiles::Read again = TokenFiles::readW3c(written);
        QVERIFY(again.skipped.isEmpty());
        QCOMPARE(named(again.tokens, QStringLiteral("duration/reveal"))->value.number, 480.0);
        QCOMPARE(named(again.tokens, QStringLiteral("stagger/words"))->kind, TokenKind::duration);
        QCOMPARE(named(again.tokens, QStringLiteral("ease/reveal"))->value.text, QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        QCOMPARE(named(again.tokens, QStringLiteral("ease/plain"))->value.text, QStringLiteral("ease-out"));
        QCOMPARE(named(again.tokens, QStringLiteral("ease/table"))->value.text, QStringLiteral("linear(0, 0.4, 1)"));
        // linear() reads as Custom, in the panel.
        QCOMPARE(named(again.tokens, QStringLiteral("ease/table"))->displayValue(), QStringLiteral("Custom"));
        QCOMPARE(named(again.tokens, QStringLiteral("duration/reveal"))->displayValue(), QStringLiteral("480 ms"));
    }

    void w3cDurationsInTheOlderTextFormAreReadAndKeepTheirKeys()
    {
        const QByteArray file = R"json({
  "motion": {
    "$description": "Timing",
    "reveal": { "$type": "duration", "$value": "0.48s", "$description": "Entrances", "$extensions": { "org.example": { "keep": true } } },
    "spring": { "$type": "cubicBezier", "$value": [0.34, 1.56, 0.64, 1] }
  }
})json";
        TokenFiles::Read read = TokenFiles::readW3c(file);
        QCOMPARE(named(read.tokens, QStringLiteral("motion/reveal"))->value.number, 480.0);
        QCOMPARE(named(read.tokens, QStringLiteral("motion/spring"))->kind, TokenKind::easing);
        for (DesignToken &token : read.tokens)
            if (token.name == QLatin1String("motion/reveal"))
                token.value.number = 600;
        const QJsonObject json = QJsonDocument::fromJson(TokenFiles::writeW3c(read.tokens, file)).object();
        const QJsonObject reveal = json["motion"].toObject()["reveal"].toObject();
        // The file wrote text, so it keeps writing text; the description and the extension key stay.
        QCOMPARE(reveal["$value"].toString(), QStringLiteral("600ms"));
        QCOMPARE(reveal["$description"].toString(), QStringLiteral("Entrances"));
        QVERIFY(reveal["$extensions"].toObject().contains("org.example"));
        QCOMPARE(json["motion"].toObject()["$description"].toString(), QStringLiteral("Timing"));
    }

    void motionTokensRoundTripThroughTailwindAndCss()
    {
        const QByteArray tailwind = "@import \"tailwindcss\";\n\n@theme {\n  --duration-reveal: 480ms;\n  --duration-slow: 0.6s;\n  --stagger-words: 60ms;\n"
                                    "  --ease-reveal: cubic-bezier(0.16, 1, 0.3, 1);\n  --color-ink: #111111;\n}\n\n.word { animation: nl-rise var(--duration-reveal) var(--ease-reveal); }\n";
        TokenFiles::Read read = TokenFiles::readTailwind(tailwind);
        QCOMPARE(named(read.tokens, QStringLiteral("duration/reveal"))->value.number, 480.0);
        QCOMPARE(named(read.tokens, QStringLiteral("duration/slow"))->value.number, 600.0);
        QCOMPARE(named(read.tokens, QStringLiteral("stagger/words"))->kind, TokenKind::duration);
        QCOMPARE(named(read.tokens, QStringLiteral("ease/reveal"))->value.text, QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        QVERIFY(named(read.tokens, QStringLiteral("color/ink")));
        std::vector<DesignToken> edited = read.tokens;
        for (DesignToken &token : edited) {
            if (token.name == QLatin1String("duration/reveal"))
                token.value.number = 520;
            if (token.name == QLatin1String("duration/slow"))
                token.value.number = 800;
            if (token.name == QLatin1String("ease/reveal"))
                token.value.text = QStringLiteral("cubic-bezier(0.34, 1.56, 0.64, 1)");
        }
        edited.push_back(DesignToken::number(TokenKind::duration, QStringLiteral("stagger/cards"), 120));
        const QString written = QString::fromUtf8(TokenFiles::writeTailwind(edited, tailwind));
        QVERIFY(written.contains(QStringLiteral("--duration-reveal: 520ms;")));
        // A file that writes seconds keeps writing seconds.
        QVERIFY(written.contains(QStringLiteral("--duration-slow: 0.8s;")));
        QVERIFY(written.contains(QStringLiteral("--ease-reveal: cubic-bezier(0.34, 1.56, 0.64, 1);")));
        QVERIFY(written.contains(QStringLiteral("--stagger-cards: 120ms;")));
        // What else is in the file stays as it was.
        QVERIFY(written.contains(QStringLiteral(".word { animation: nl-rise var(--duration-reveal) var(--ease-reveal); }")));
        QVERIFY(written.contains(QStringLiteral("--color-ink: #111111;")));
        QCOMPARE(written.count(QStringLiteral("@theme")), 1);

        const QByteArray css = ":root {\n  --duration-reveal: 480ms;\n  --stagger-words: 60ms;\n  --ease-out: cubic-bezier(0, 0, 0.2, 1);\n  --brand: #e11d48;\n}\n";
        read = TokenFiles::readCss(css);
        QCOMPARE(named(read.tokens, QStringLiteral("duration/reveal"))->value.number, 480.0);
        QCOMPARE(named(read.tokens, QStringLiteral("stagger/words"))->value.number, 60.0);
        QCOMPARE(named(read.tokens, QStringLiteral("ease/out"))->value.text, QStringLiteral("cubic-bezier(0, 0, 0.2, 1)"));
        edited = read.tokens;
        for (DesignToken &token : edited)
            if (token.name == QLatin1String("stagger/words"))
                token.value.number = 90;
        const QString rewritten = QString::fromUtf8(TokenFiles::writeCss(edited, css));
        QVERIFY(rewritten.contains(QStringLiteral("--stagger-words: 90ms;")));
        QVERIFY(rewritten.contains(QStringLiteral("--brand: #e11d48;")));
        // And each name is the variable it came from.
        QCOMPARE(TokenFiles::cssVariable(*named(read.tokens, QStringLiteral("stagger/words"))), QStringLiteral("--stagger-words"));
        QCOMPARE(TokenFiles::cssVariable(*named(read.tokens, QStringLiteral("duration/reveal"))), QStringLiteral("--duration-reveal"));
        QCOMPARE(TokenFiles::cssVariable(*named(read.tokens, QStringLiteral("ease/out"))), QStringLiteral("--ease-out"));
    }

    void timesAndEasingsAreParsedAsTheFilesWriteThem()
    {
        QCOMPARE(TokenFiles::parseTime(QStringLiteral("480ms")), std::optional<double>(480));
        QCOMPARE(TokenFiles::parseTime(QStringLiteral("0.48s")), std::optional<double>(480));
        QCOMPARE(TokenFiles::parseTime(QStringLiteral(" 60 ms ")), std::optional<double>(60));
        QVERIFY(!TokenFiles::parseTime(QStringLiteral("480")));
        QVERIFY(!TokenFiles::parseTime(QStringLiteral("16px")));
        QVERIFY(TokenFiles::isEasing(QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)")));
        QVERIFY(TokenFiles::isEasing(QStringLiteral("ease-in-out")));
        QVERIFY(TokenFiles::isEasing(QStringLiteral("linear(0, 0.5, 1)")));
        QVERIFY(!TokenFiles::isEasing(QStringLiteral("banana")));
        const auto curve = TokenFiles::cubicBezier(QStringLiteral("cubic-bezier( 0.16 ,1, 0.3, 1 )"));
        QVERIFY(curve);
        QCOMPARE((*curve)[0], 0.16);
        QCOMPARE(TokenFiles::cubicBezierText(*curve), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        QVERIFY(!TokenFiles::cubicBezier(QStringLiteral("ease-out")));
    }

    void tailwindV4RoundTripsInPlace()
    {
        const TokenFiles::Read read = TokenFiles::readTailwind(tailwindFixture);
        const DesignToken *brand = named(read.tokens, QStringLiteral("color/brand-500"));
        QVERIFY(brand);
        QVERIFY(brand->value.color.blue() > 200 && brand->value.color.red() < 80);
        QCOMPARE(named(read.tokens, QStringLiteral("spacing"))->value.number, 4.0);
        QCOMPARE(named(read.tokens, QStringLiteral("radius/lg"))->value.number, 8.0);
        const TypeValue small = named(read.tokens, QStringLiteral("text/sm"))->value.type;
        QCOMPARE(small.size, 14.0);
        QCOMPARE(small.lineHeight, std::optional<double>(20));
        QCOMPARE(small.family, QStringLiteral("Inter"));
        QCOMPARE(named(read.tokens, QStringLiteral("shadow/md"))->value.shadow.y, 4.0);
        std::vector<DesignToken> edited = read.tokens;
        for (DesignToken &token : edited) {
            if (token.name == QLatin1String("color/ink"))
                token.value.color = QColor("#222222");
            if (token.name == QLatin1String("radius/lg"))
                token.value.number = 12;
        }
        edited.push_back(DesignToken::color(QStringLiteral("color/accent"), QColor("#ff9f0a")));
        const QString written = QString::fromUtf8(TokenFiles::writeTailwind(edited, tailwindFixture));
        QVERIFY(written.contains(QStringLiteral("--color-ink: #222222;")));
        // A rem value stays in rem.
        QVERIFY(written.contains(QStringLiteral("--radius-lg: 0.75rem;")));
        QVERIFY(written.contains(QStringLiteral("--color-accent: #ff9f0a;")));
        QVERIFY(written.contains(QStringLiteral("/* The brand. */")));
        QVERIFY(written.contains(QStringLiteral(".card { color: var(--color-ink); }")));
        QCOMPARE(written.count(QStringLiteral("@theme")), 1);
        const TokenFiles::Read again = TokenFiles::readTailwind(written.toUtf8());
        QCOMPARE(named(again.tokens, QStringLiteral("color/ink"))->value.color, QColor("#222222"));
        QCOMPARE(named(again.tokens, QStringLiteral("color/accent"))->value.color, QColor("#ff9f0a"));
        // An empty file gets the import and a theme block.
        const QString made = QString::fromUtf8(TokenFiles::writeTailwind({DesignToken::color(QStringLiteral("color/ink"), Qt::black)}));
        QVERIFY(made.startsWith(QStringLiteral("@import \"tailwindcss\";")));
        QVERIFY(made.contains(QStringLiteral("@theme {\n  --color-ink: #000000;\n}")));
    }

    void tailwindV3ConfigIsReadAsFarAsItIsLiteral()
    {
        const TokenFiles::Read read = TokenFiles::readTailwindConfig(tailwindConfigFixture);
        QCOMPARE(named(read.tokens, QStringLiteral("color/brand"))->value.color, QColor("#0a84ff"));
        QCOMPARE(named(read.tokens, QStringLiteral("color/brand-dark"))->value.color, QColor("#0060df"));
        QVERIFY(!named(read.tokens, QStringLiteral("color/gray")));
        QCOMPARE(named(read.tokens, QStringLiteral("spacing/18"))->value.number, 72.0);
        QCOMPARE(named(read.tokens, QStringLiteral("radius/xl"))->value.number, 16.0);
        const TypeValue hero = named(read.tokens, QStringLiteral("text/hero"))->value.type;
        QCOMPARE(hero.size, 48.0);
        QCOMPARE(hero.lineHeight, std::optional<double>(48));
        QCOMPARE(hero.weight, 800);
        QCOMPARE(hero.family, QStringLiteral("Inter"));
        QCOMPARE(named(read.tokens, QStringLiteral("text/tiny"))->value.type.lineHeight, std::optional<double>(14));
        QCOMPARE(named(read.tokens, QStringLiteral("shadow/soft"))->value.shadow.blur, 3.0);
    }

    void cssVariablesRoundTripWithTheirOwnNamesAndDarkMode()
    {
        const TokenFiles::Read read = TokenFiles::readCss(cssFixture);
        const DesignToken *blue = named(read.tokens, QStringLiteral("color/brand-blue"));
        QVERIFY(blue);
        QCOMPARE(blue->modes.at(QStringLiteral("dark")).color, QColor("#409cff"));
        QCOMPARE(named(read.tokens, QStringLiteral("spacing/md"))->value.number, 12.0);
        QCOMPARE(named(read.tokens, QStringLiteral("radius/card"))->value.number, 10.0);
        QCOMPARE(named(read.tokens, QStringLiteral("shadow/card"))->value.shadow.blur, 8.0);
        QVERIFY(read.skipped.contains(QStringLiteral("--unknown-thing")));
        std::vector<DesignToken> edited = read.tokens;
        for (DesignToken &token : edited) {
            if (token.name == QLatin1String("color/brand-blue")) {
                token.value.color = QColor("#0000ff");
                token.modes[QStringLiteral("dark")].color = QColor("#8888ff");
            }
        }
        const QString written = QString::fromUtf8(TokenFiles::writeCss(edited, cssFixture, {QStringLiteral("light"), QStringLiteral("dark")}));
        // The property keeps its own name, and the dark block is updated where it is.
        QVERIFY(written.contains(QStringLiteral("--brand-blue: #0000ff;")));
        QVERIFY(written.contains(QStringLiteral("--brand-blue: #8888ff;")));
        QVERIFY(!written.contains(QStringLiteral("--color-brand-blue")));
        QVERIFY(written.contains(QStringLiteral("--unknown-thing: bold;")));
        QVERIFY(written.contains(QStringLiteral("body { margin: 0; }")));
        const TokenFiles::Read again = TokenFiles::readCss(written.toUtf8());
        QCOMPARE(named(again.tokens, QStringLiteral("color/brand-blue"))->modes.at(QStringLiteral("dark")).color, QColor("#8888ff"));
    }

    void aProjectIsDetectedPulledAndPlannedForPush()
    {
        QTemporaryDir project;
        write(project.filePath(QStringLiteral("tokens.json")), w3cFixture);
        write(project.filePath(QStringLiteral("src/app.css")), tailwindFixture);
        write(project.filePath(QStringLiteral("styles/vars.css")), cssFixture);
        write(project.filePath(QStringLiteral("tailwind.config.js")), tailwindConfigFixture);
        write(project.filePath(QStringLiteral("node_modules/pkg/theme.css")), tailwindFixture);
        const auto sources = ProjectCode::detect(project.path());
        QCOMPARE(sources.size(), size_t(4));
        const ProjectCode::Pulled pulled = ProjectCode::read(sources);
        QCOMPARE(pulled.reads.size(), 4);
        QVERIFY(named(pulled.tokens, QStringLiteral("color/brand")));
        QVERIFY(named(pulled.tokens, QStringLiteral("color/ink")));
        QVERIFY(named(pulled.tokens, QStringLiteral("color/brand-blue")));
        // The push plan names every file it writes, in full, and leaves the v3 config alone.
        std::vector<DesignToken> tokens{DesignToken::color(QStringLiteral("color/ink"), QColor("#333333"))};
        const SyncPlan plan = ProjectCode::pushPlan(project.path(), tokens, {});
        QVERIFY(plan.problem.isEmpty());
        QStringList paths;
        for (const FileWrite &write : plan.writes)
            paths.append(write.path);
        QVERIFY(paths.contains(QFileInfo(project.filePath(QStringLiteral("tokens.json"))).absoluteFilePath()));
        QVERIFY(paths.contains(QFileInfo(project.filePath(QStringLiteral("src/app.css"))).absoluteFilePath()));
        QVERIFY(paths.contains(QFileInfo(project.filePath(QStringLiteral("styles/vars.css"))).absoluteFilePath()));
        QVERIFY(!paths.contains(QFileInfo(project.filePath(QStringLiteral("tailwind.config.js"))).absoluteFilePath()));
        QVERIFY(!plan.git);
        QVERIFY(plan.destination.contains(project.path()));
        QVERIFY(plan.diffSummary().contains(QStringLiteral("changed")));
        // Building the plan wrote nothing.
        QCOMPARE(readAll(project.filePath(QStringLiteral("src/app.css"))), QByteArray(tailwindFixture));
    }

    void aPushPlanNamesTheRepositoryAndBranch()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
            QSKIP("git isn't installed");
        QTemporaryDir project;
        const auto git = [&](const QStringList &arguments) {
            QProcess process;
            process.setWorkingDirectory(project.path());
            process.start(QStringLiteral("git"), arguments);
            process.waitForFinished();
            return process.exitCode();
        };
        QCOMPARE(git({QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("design")}), 0);
        const SyncPlan plan = ProjectCode::pushPlan(project.path(), {DesignToken::color(QStringLiteral("color/ink"), Qt::black)}, {});
        QVERIFY(plan.git);
        QCOMPARE(plan.git->branch, QStringLiteral("design"));
        QCOMPARE(QFileInfo(plan.git->repository).canonicalFilePath(), QFileInfo(project.path()).canonicalFilePath());
        QCOMPARE(plan.writes.size(), size_t(1));
        QCOMPARE(plan.writes.front().state(), QStringLiteral("new"));
        QVERIFY(plan.destination.contains(QStringLiteral("branch design")));
        QVERIFY(!QFileInfo::exists(project.filePath(QStringLiteral("tokens.json"))));
    }

    void theLibraryLivesInTheDataFolder()
    {
        QCOMPARE(Library::directory(), m_home.path() + QStringLiteral("/data/omastrator/libraries"));
        EditorSession session;
        session.createDocument({400, 300});
        session.addToken(DesignToken::color(QStringLiteral("color/brand"), Qt::red));
        const SyncPlan plan = Library::pushPlan(QStringLiteral("Personal"), *session.document());
        QCOMPARE(plan.writes.size(), size_t(1));
        QCOMPARE(plan.writes.front().path, m_home.path() + QStringLiteral("/data/omastrator/libraries/personal.json"));
        QVERIFY(plan.destination.contains(plan.writes.front().path));
        QVERIFY(!QFileInfo::exists(plan.writes.front().path));
        // What it would write reads back.
        write(plan.writes.front().path, plan.writes.front().after);
        QCOMPARE(Library::names(), QStringList{QStringLiteral("Personal")});
        QCOMPARE(Library::load(QStringLiteral("Personal")).tokens.front().name, QStringLiteral("color/brand"));
        QFile::remove(plan.writes.front().path);
    }

    void anOmarchyThemeIsReadAndANewFolderPlanned()
    {
        const QString source = m_home.path() + QStringLiteral("/.config/omarchy/themes/test-theme");
        write(source + QStringLiteral("/colors.toml"), colorsTomlFixture);
        write(source + QStringLiteral("/shell.spacing.toml"), spacingTomlFixture);
        write(source + QStringLiteral("/backgrounds/1.png"), "not really a png");
        write(m_home.path() + QStringLiteral("/.local/state/omarchy/current/theme.name"), "Test Theme\n");
        QCOMPARE(OmarchyThemes::currentName(), QStringLiteral("Test Theme"));
        QCOMPARE(OmarchyThemes::directoryOf(QStringLiteral("Test Theme")), source);
        const OmarchyThemes::Theme theme = OmarchyThemes::read(source, QStringLiteral("Test Theme"));
        QCOMPARE(theme.mode, QStringLiteral("dark"));
        QCOMPARE(named(theme.tokens, QStringLiteral("color/accent"))->value.color, QColor("#0a84ff"));
        QCOMPARE(named(theme.tokens, QStringLiteral("color/hyprland-active-border"))->value.color.alpha(), 0xb3);
        QVERIFY(!named(theme.tokens, QStringLiteral("color/ignored")));
        QCOMPARE(named(theme.tokens, QStringLiteral("spacing/popup-padding"))->value.number, 16.0);
        std::vector<DesignToken> edited = theme.tokens;
        for (DesignToken &token : edited) {
            if (token.name == QLatin1String("color/accent"))
                token.value.color = QColor("#ff375f");
            if (token.name == QLatin1String("color/hyprland-active-border"))
                token.value.color = QColor(255, 55, 95, 0xb3);
            if (token.name == QLatin1String("spacing/popup-padding"))
                token.value.number = 20;
        }
        const QString colors = QString::fromUtf8(OmarchyThemes::writeColors(colorsTomlFixture, edited));
        QVERIFY(colors.contains(QStringLiteral("accent = \"#ff375f\"")));
        QVERIFY(colors.contains(QStringLiteral("hyprland_active_border = \"rgba(ff375fb3)\"")));
        QVERIFY(colors.contains(QStringLiteral("# Test theme.")));
        QVERIFY(colors.contains(QStringLiteral("ignored = \"#ffffff\"")));
        const SyncPlan plan = OmarchyThemes::savePlan(source, QStringLiteral("Hot Pink"), edited, true);
        QVERIFY2(plan.problem.isEmpty(), qPrintable(plan.problem));
        const QString target = m_home.path() + QStringLiteral("/.config/omarchy/themes/hot-pink");
        QStringList paths;
        for (const FileWrite &write : plan.writes)
            paths.append(write.path);
        paths.sort();
        QCOMPARE(paths, QStringList({target + QStringLiteral("/backgrounds/1.png"), target + QStringLiteral("/colors.toml"),
                                     target + QStringLiteral("/shell.spacing.toml")}));
        QCOMPARE(plan.command, QStringList({OmarchyThemes::omarchy(), QStringLiteral("theme"), QStringLiteral("set"), QStringLiteral("hot-pink")}));
        QVERIFY(plan.destination.contains(target));
        QVERIFY(plan.destination.contains(QStringLiteral("whole desktop")));
        QVERIFY(!QFileInfo::exists(target));
        // The same name twice is refused, not overwritten.
        QDir().mkpath(target);
        QVERIFY(!OmarchyThemes::savePlan(source, QStringLiteral("Hot Pink"), edited, false).problem.isEmpty());
        QDir(target).removeRecursively();
    }

    void aSiteIsReadIntoAProposedSystem()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed");
        QTemporaryDir profile;
        Browser browser;
        Browser::Options options;
        options.headless = true;
        options.profile = profile.path();
        const QString failed = browser.start(options);
        QVERIFY2(failed.isEmpty(), qPrintable(failed));
        QString error;
        const auto page = browser.attachPage(QUrl::fromLocalFile(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/System/fixtures/site.html")), &error);
        QVERIFY2(page, qPrintable(error));
        const QJsonObject scan = SiteExtract::scan(browser, page->sessionId, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        browser.stop();
        const SiteExtract::Proposal proposal = SiteExtract::propose(scan, QStringLiteral("file://site.html"));
        QCOMPARE(named(proposal.tokens, QStringLiteral("color/background"))->value.color, QColor("#f3f4f6"));
        QCOMPARE(named(proposal.tokens, QStringLiteral("color/accent"))->value.color, QColor("#2563eb"));
        QCOMPARE(named(proposal.tokens, QStringLiteral("text/base"))->value.type.size, 16.0);
        QCOMPARE(named(proposal.tokens, QStringLiteral("text/lg"))->value.type.size, 36.0);
        QCOMPARE(named(proposal.tokens, QStringLiteral("text/sm"))->value.type.size, 14.0);
        QVERIFY(named(proposal.tokens, QStringLiteral("spacing/16")));
        QCOMPARE(named(proposal.tokens, QStringLiteral("radius/sm"))->value.number, 6.0);
        QCOMPARE(named(proposal.tokens, QStringLiteral("radius/md"))->value.number, 12.0);
        QCOMPARE(named(proposal.tokens, QStringLiteral("shadow/sm"))->value.shadow.y, 4.0);
        QVERIFY(proposal.components().contains(QStringLiteral("Card")));
        QVERIFY(proposal.components().contains(QStringLiteral("Button")));
        // The proposal places into a document as components.
        EditorSession session;
        session.createDocument({400, 300});
        session.mergeTokens(proposal.tokens, QStringLiteral("Pull Design System"));
        session.placeFromLibrary(proposal.objects, QString(), {});
        QCOMPARE(Components::masters(*session.document()).size(), size_t(proposal.components().size()));
        const SyncPlan plan = SiteExtract::pullPlan(proposal, QStringLiteral("Untitled-1"), [](const SiteExtract::Proposal &) { return QString(); });
        QVERIFY(plan.writes.empty());
        QVERIFY(plan.inApp.contains(QStringLiteral("Card")));
    }
};

QTEST_MAIN(DesignSourcesTests)
#include "DesignSourcesTests.moc"
