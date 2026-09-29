#include "Live/Breakpoints.h"
#include <QJsonArray>
#include <QTest>
#ifdef OMASTRATOR_HAVE_QML
#include <QJSEngine>
#include <QJsonDocument>
#endif

// Which widths the breakpoint buttons offer (docs/BROWSER-VIEW.md, section 7): the scan's answer turned into widths.
namespace {
QJsonObject scan(const QStringList &media, const QJsonObject &vars = {}, double root = 16)
{
    QJsonArray queries;
    for (const QString &each : media)
        queries.append(each);
    return {{"rootFontSize", root}, {"media", queries}, {"vars", vars}};
}
}

class BreakpointsTests : public QObject {
    Q_OBJECT

private slots:
    void aPageWithNoQueriesGetsTheDefaults()
    {
        QCOMPARE(Breakpoints::fromScan(scan({})), (QList<int>{390, 768, 1280, 1440}));
        QCOMPARE(Breakpoints::fromScan({}), Breakpoints::defaults());
    }

    void minAndMaxWidthInPixelsAndRems()
    {
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(min-width: 768px)"), QStringLiteral("(max-width: 48rem)"),
                                             QStringLiteral("(min-width:64em)")})),
                 (QList<int>{768, 1024}));
    }

    void rangeSyntaxReadsBothOrders()
    {
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(width >= 40rem)"), QStringLiteral("(48rem <= width < 80rem)")})),
                 (QList<int>{640, 768, 1280}));
    }

    void tailwindVariablesCount()
    {
        QCOMPARE(Breakpoints::fromScan(scan({}, {{"--breakpoint-md", "48rem"}, {"--breakpoint-xl", "80rem"}, {"--breakpoint-x", "calc(1px)"}})),
                 (QList<int>{768, 1280}));
    }

    void theRootFontSizeScalesRems()
    {
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(min-width: 40rem)")}, {}, 20)), (QList<int>{800}));
        // A silly root size is taken as 16.
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(min-width: 40rem)")}, {}, 900)), (QList<int>{640}));
    }

    void widthsOutsideThePhoneAndTheBigScreenAreLeftOut()
    {
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(min-width: 100px)"), QStringLiteral("(min-width: 5000px)"),
                                             QStringLiteral("(min-width: 320px)"), QStringLiteral("(min-width: 2560px)")})),
                 (QList<int>{320, 2560}));
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(min-width: 100px)")})), Breakpoints::defaults());
    }

    void onlyTheFiveMostUsedSurvive()
    {
        QStringList media;
        for (const int width : {400, 500, 600, 700, 800, 900})
            media << QStringLiteral("(min-width: %1px)").arg(width);
        // 900 is used twice and 400 once more than the rest: with ties, the smaller width wins.
        media << QStringLiteral("(min-width: 900px)") << QStringLiteral("(min-width: 400px)");
        QCOMPARE(Breakpoints::fromScan(scan(media)), (QList<int>{400, 500, 600, 700, 900}));
    }

    void otherMediaFeaturesAreIgnored()
    {
        QCOMPARE(Breakpoints::fromScan(scan({QStringLiteral("(prefers-color-scheme: dark)"), QStringLiteral("(min-height: 900px)"),
                                             QStringLiteral("print")})),
                 Breakpoints::defaults());
    }

    void theScanScriptIsOneExpression()
    {
        const QString script = Breakpoints::scanScript();
        QVERIFY(script.contains(QLatin1String("--breakpoint-")));
        QVERIFY(script.contains(QLatin1String("JSON.stringify")));
    }

    void theScanScriptStopsAtTwoThousandQueries()
    {
#ifndef OMASTRATOR_HAVE_QML
        QSKIP("Qt Qml isn't installed");
#else
        // A page whose one stylesheet holds 5000 media rules.
        QJSEngine engine;
        engine.evaluate(QStringLiteral(R"JS(
            const rules = [];
            for (let i = 0; i < 5000; ++i) rules.push({ media: { mediaText: "(min-width: 700px)" }, cssRules: [] });
            var document = { styleSheets: [{ cssRules: rules }], documentElement: {} };
            var getComputedStyle = () => ({ fontSize: "16px" });
        )JS"));
        const QJSValue answer = engine.evaluate(Breakpoints::scanScript());
        QVERIFY2(answer.isString(), qPrintable(answer.toString()));
        const QJsonObject scanned = QJsonDocument::fromJson(answer.toString().toUtf8()).object();
        QCOMPARE(scanned["media"].toArray().size(), 2000);
#endif
    }
};

QTEST_MAIN(BreakpointsTests)
#include "BreakpointsTests.moc"
