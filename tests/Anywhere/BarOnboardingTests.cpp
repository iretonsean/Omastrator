#include "Anywhere/AnywhereSettings.h"
#include "Anywhere/Bar.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

// The OS-wide floating bar's actions per surface kind, and the suggestions
// onboarding tunes (docs/ANYWHERE.md). Answers are kept in anywhere.json.
namespace {
QStringList ids(const QJsonArray &list)
{
    QStringList result;
    for (const QJsonValue &each : list)
        result << each.toObject()["id"].toString();
    return result;
}
}

class BarOnboardingTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
    }

    void eachSurfaceKindHasItsLikeliestActions()
    {
        QCOMPARE(ids(Bar::actions(QStringLiteral("web"))), (QStringList{"inspect", "lift", "mockup", "measure", "extractSystem"}));
        // Lift is on, for pages and windows alike.
        const QJsonObject lift = Bar::actions(QStringLiteral("web"))[1].toObject();
        QCOMPARE(lift["enabled"].toBool(true), true);
        QCOMPARE(lift["label"].toString(), QStringLiteral("Lift"));
        QCOMPARE(ids(Bar::actions(QStringLiteral("window"))), (QStringList{"capture", "lift", "measure"}));
        QCOMPARE(ids(Bar::actions(QStringLiteral("browser"))), (QStringList{"capture", "lift", "measure", "openInBrowser"}));
        QCOMPARE(ids(Bar::actions(QStringLiteral("desktop"))), (QStringList{"capture", "measure"}));
        // Art on the overlay: the in-app task bar's actions for that kind of selection.
        QCOMPARE(ids(Bar::actions(QStringLiteral("art:paths"))), (QStringList{"unite", "group", "makeComponent", "designSystem", "duplicate", "delete", "undo"}));
        QCOMPARE(ids(Bar::actions(QStringLiteral("art:group"))), (QStringList{"ungroup", "makeComponent", "designSystem", "duplicate", "delete", "undo"}));
        QCOMPARE(ids(Bar::actions(QStringLiteral("art:text:point"))).first(), QStringLiteral("createOutlines"));
        QCOMPARE(ids(Bar::actions(QStringLiteral("art:clipGroup"))).first(), QStringLiteral("releaseClippingMask"));
        QVERIFY(Bar::actions(QStringLiteral("nothing")).isEmpty());
        QCOMPARE(Bar::kindOf(QStringLiteral("window"), true), QStringLiteral("browser"));
        QCOMPARE(Bar::kindOf(QStringLiteral("web"), false), QStringLiteral("web"));
        // Labels stay literal.
        for (const QString &kind : {QStringLiteral("web"), QStringLiteral("window"), QStringLiteral("art:path")}) {
            for (const QJsonValue &action : Bar::actions(kind)) {
                QVERIFY(!action.toObject()["label"].toString().contains(QLatin1Char('!')));
                QVERIFY(!action.toObject()["tip"].toString().isEmpty());
            }
        }
    }

    void onboardingAnswersAreKeptAndChecked()
    {
        QVERIFY(AnywhereSettings::needsOnboarding());
        QVERIFY(AnywhereSettings::privacyNote().contains(QLatin1String("stay on this machine")));
        QVERIFY(AnywhereSettings::privacyNote().contains(QLatin1String("unless you ask")));
        QCOMPARE(AnywhereSettings::questions().size(), size_t(3));
        QCOMPARE(AnywhereSettings::setAnswer(QStringLiteral("makes"), {QStringLiteral("web"), QStringLiteral("rice")}), QString());
        QCOMPARE(AnywhereSettings::setAnswer(QStringLiteral("from"), {QStringLiteral("figma")}), QString());
        QVERIFY(AnywhereSettings::setAnswer(QStringLiteral("makes"), {QStringLiteral("spreadsheets")}).contains(QLatin1String("isn't an answer")));
        QVERIFY(AnywhereSettings::setAnswer(QStringLiteral("ai"), {QStringLiteral("quiet"), QStringLiteral("lots")}).contains(QLatin1String("one")));
        QVERIFY(AnywhereSettings::setAnswer(QStringLiteral("shoeSize"), {QStringLiteral("9")}).contains(QLatin1String("no onboarding question")));
        QCOMPARE(AnywhereSettings::setAnswer(QStringLiteral("ai"), {QStringLiteral("lots")}), QString());
        QVERIFY(AnywhereSettings::needsOnboarding());
        QCOMPARE(AnywhereSettings::finish(false), QString());
        QVERIFY(!AnywhereSettings::needsOnboarding());
        const AnywhereSettings::Answers answers = AnywhereSettings::answers();
        QCOMPARE(answers.makes, (QStringList{"web", "rice"}));
        QCOMPARE(answers.from, QStringList{"figma"});
        QCOMPARE(answers.ai, QStringLiteral("lots"));
        QVERIFY(answers.done);
        QVERIFY(QFile::exists(m_directory.filePath(QStringLiteral("config/omastrator/anywhere.json"))));
        QVERIFY(AnywhereSettings::path().startsWith(m_directory.path()));
    }

    void suggestionsChangeWithTheAnswers()
    {
        AnywhereSettings::Answers webDesigner;
        webDesigner.makes = {QStringLiteral("web")};
        webDesigner.ai = QStringLiteral("lots");
        AnywhereSettings::Answers ricer;
        ricer.makes = {QStringLiteral("rice")};
        ricer.ai = QStringLiteral("lots");
        AnywhereSettings::Answers marketer;
        marketer.makes = {QStringLiteral("marketing")};
        // A web designer is offered spacing and tokens; a ricer the window's palette; marketing captures first.
        const QJsonArray forWeb = Bar::suggestions(QStringLiteral("web"), webDesigner);
        QCOMPARE(forWeb.size(), 3);
        QCOMPARE(ids(forWeb).first(), QStringLiteral("measure"));
        QVERIFY(ids(forWeb).contains(QStringLiteral("tokens")));
        QCOMPARE(ids(Bar::suggestions(QStringLiteral("window"), ricer)).first(), QStringLiteral("palette"));
        QCOMPARE(ids(Bar::suggestions(QStringLiteral("window"), webDesigner)).first(), QStringLiteral("measure"));
        QCOMPARE(ids(Bar::suggestions(QStringLiteral("web"), marketer)).first(), QStringLiteral("capture"));
        QCOMPARE(ids(Bar::suggestions(QStringLiteral("desktop"), ricer)).first(), QStringLiteral("palette"));
        // "A few" is two; AI kept quiet is one chip and never an AI one.
        QCOMPARE(Bar::suggestions(QStringLiteral("web"), marketer).size(), 2);
        AnywhereSettings::Answers quiet = webDesigner;
        quiet.ai = QStringLiteral("quiet");
        const QJsonArray few = Bar::suggestions(QStringLiteral("window"), quiet);
        QCOMPARE(few.size(), 1);
        QVERIFY(!few[0].toObject()["ai"].toBool());
        // AI chips carry the prompt Ask runs.
        for (const QJsonValue &chip : Bar::suggestions(QStringLiteral("art:path"), ricer)) {
            const QJsonObject object = chip.toObject();
            QCOMPARE(object["action"].toString() == QLatin1String("ask"), !object["prompt"].toString().isEmpty());
        }
    }

    void whereWorkGoesIsRememberedPerSurface()
    {
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("window:foot")), QStringLiteral("overlay"));
        QCOMPARE(AnywhereSettings::setDestination(QStringLiteral("window:foot"), QStringLiteral("desk")), QString());
        QCOMPARE(AnywhereSettings::setDestination(QStringLiteral("web:https://example.com"), QStringLiteral("agent")), QString());
        QVERIFY(!AnywhereSettings::setDestination(QStringLiteral("window:foot"), QStringLiteral("printer")).isEmpty());
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("window:foot")), QStringLiteral("desk"));
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("web:https://example.com")), QStringLiteral("agent"));
        QCOMPARE(AnywhereSettings::destination(QStringLiteral("window:thunar")), QStringLiteral("overlay"));
        // The answers are still there beside them.
        QCOMPARE(AnywhereSettings::answers().ai, QStringLiteral("lots"));
        QCOMPARE(AnywhereSettings::deskWorkspace(), QStringLiteral("special:omastrator-desk"));
        QJsonObject settings = AnywhereSettings::read();
        settings["deskWorkspace"] = QStringLiteral("9");
        AnywhereSettings::write(settings);
        QCOMPARE(AnywhereSettings::deskWorkspace(), QStringLiteral("9"));
    }
};

QTEST_GUILESS_MAIN(BarOnboardingTests)
#include "BarOnboardingTests.moc"
