#include "Live/Browser.h"
#include <QTest>

// The Chromium command line, checked without starting a browser (docs/BROWSER-FRAMES.md).
class BrowserArgumentsTests : public QObject {
    Q_OBJECT
private slots:
    void theBrowserWindowCapsItsDiskCache()
    {
        const QStringList arguments = Browser::chromiumArguments({}, QStringLiteral("/profile"));
        QVERIFY(arguments.contains(QStringLiteral("--user-data-dir=/profile")));
        QVERIFY(arguments.contains(QStringLiteral("--disk-cache-size=67108864")));
        QVERIFY(arguments.contains(QStringLiteral("--media-cache-size=67108864")));
        QVERIFY(!arguments.contains(QStringLiteral("--headless=new")));
    }

    void headlessRunsKeepTheProfileTiny()
    {
        Browser::Options options;
        options.headless = true;
        const QStringList arguments = Browser::chromiumArguments(options, QStringLiteral("/profile"));
        QVERIFY(arguments.contains(QStringLiteral("--disk-cache-size=1")));
        QVERIFY(!arguments.contains(QStringLiteral("--disk-cache-size=67108864")));
    }

    void extraArgumentsComeLastSoTheyCanOverride()
    {
        Browser::Options options;
        options.extraArguments = {QStringLiteral("--disk-cache-size=1000")};
        QCOMPARE(Browser::chromiumArguments(options, QStringLiteral("/p")).last(), QStringLiteral("--disk-cache-size=1000"));
    }

    void anElectronAppGetsOnlyTheDebuggingPortAndProfile()
    {
        Browser::Options options;
        options.program = QStringLiteral("/usr/bin/electron");
        const QStringList arguments = Browser::chromiumArguments(options, QStringLiteral("/p"));
        QCOMPARE(arguments.size(), 3);
        QVERIFY(!arguments.contains(QStringLiteral("--no-first-run")));
    }
};

QTEST_GUILESS_MAIN(BrowserArgumentsTests)
#include "BrowserArgumentsTests.moc"
