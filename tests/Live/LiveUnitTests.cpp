#include "Live/DevServer.h"
#include "Live/LiveSession.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "Live/Tokens.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <thread>
#include <vector>

// Phase 5 of docs/OS-SUITE.md without a browser: tokens, dev commands, the registry, the static server.
namespace {
void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QJsonObject color(const char *rgb)
{
    return {{"kind", "color"}, {"value", rgb}, {"rgb", rgb}};
}

QJsonObject length(double px)
{
    return {{"kind", "length"}, {"value", QString::number(px) + "px"}, {"px", px}};
}

// What the overlay's scan() returns for a Tailwind v4 page with a few design tokens of its own.
QJsonObject tailwindScan()
{
    return {{"rootFontSize", 16},
            {"vars", QJsonObject{{"--color-sky-500", color("#00a6f4")}, {"--color-sky-700", color("#0069a8")},
                                 {"--color-slate-900", color("#0f172b")}, {"--spacing", length(4)}, {"--text-lg", length(18)},
                                 {"--text-lg--line-height", QJsonObject{{"kind", "number"}, {"value", "1.55556"}}},
                                 {"--text-3xl", length(30)}, {"--font-weight-bold", QJsonObject{{"kind", "number"}, {"value", "700"}}},
                                 {"--font-weight-medium", QJsonObject{{"kind", "number"}, {"value", "500"}}},
                                 {"--radius-md", length(6)}, {"--brand", color("#e11d48")}, {"--gap", length(24)}}}};
}

QByteArray get(const QUrl &url, int *status)
{
    QNetworkAccessManager network;
    QNetworkReply *reply = network.get(QNetworkRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    return body;
}
}

class LiveUnitTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
    }

    void tailwindTokensSnapAndSwapClasses()
    {
        const TokenSet tokens = TokenSet::fromScan(tailwindScan(), {{QStringLiteral("accent"), QColor(0x0a, 0x84, 0xff)}});
        QVERIFY(tokens.hasTailwind());
        QCOMPARE(tokens.spacingPx(), 4.0);

        // Padding snaps to the spacing scale, and p-4 becomes p-3.
        TokenSet::Resolution padding = tokens.resolve(QStringLiteral("padding"), QStringLiteral("13px"), {"bg-sky-500", "p-4", "rounded-md"});
        QCOMPARE(padding.value, QStringLiteral("12px"));
        QCOMPARE(padding.removeClass, QStringLiteral("p-4"));
        QCOMPARE(padding.addClass, QStringLiteral("p-3"));
        // Half steps keep Tailwind's spelling.
        QCOMPARE(tokens.resolve(QStringLiteral("padding-top"), QStringLiteral("0.6rem"), {"pt-2"}).addClass, QStringLiteral("pt-2.5"));
        // No class for the property: the value snaps, the classes stay.
        const auto margin = tokens.resolve(QStringLiteral("margin-top"), QStringLiteral("31px"), {"p-4"});
        QCOMPARE(margin.value, QStringLiteral("32px"));
        QVERIFY(margin.removeClass.isEmpty() && margin.addClass.isEmpty());

        // A colour near sky-700 becomes it; text- as a colour leaves text- as a size alone.
        const auto background = tokens.resolve(QStringLiteral("background-color"), QStringLiteral("#036aa5"), {"bg-sky-500"});
        QCOMPARE(background.token, QStringLiteral("--color-sky-700"));
        QCOMPARE(background.addClass, QStringLiteral("bg-sky-700"));
        const auto ink = tokens.resolve(QStringLiteral("color"), QStringLiteral("#101828"), {"text-3xl", "text-white"});
        QCOMPARE(ink.token, QStringLiteral("--color-slate-900"));
        QCOMPARE(ink.removeClass, QStringLiteral("text-white"));
        QCOMPARE(ink.addClass, QStringLiteral("text-slate-900"));
        const auto size = tokens.resolve(QStringLiteral("font-size"), QStringLiteral("17px"), {"text-3xl", "text-white"});
        QCOMPARE(size.value, QStringLiteral("18px"));
        QCOMPARE(size.removeClass, QStringLiteral("text-3xl"));
        QCOMPARE(size.addClass, QStringLiteral("text-lg"));
        QCOMPARE(tokens.resolve(QStringLiteral("font-weight"), QStringLiteral("650"), {"font-bold", "font-sans"}).addClass, QStringLiteral("font-bold"));
        QCOMPARE(tokens.resolve(QStringLiteral("border-radius"), QStringLiteral("5px"), {"rounded-xl"}).addClass, QStringLiteral("rounded-md"));

        // Far from every token, a colour stays the user's own; near only the Omarchy accent, it takes that.
        const auto odd = tokens.resolve(QStringLiteral("color"), QStringLiteral("#7fff00"), {});
        QVERIFY(odd.token.isEmpty());
        QCOMPARE(odd.value, QStringLiteral("#7fff00"));
        QCOMPARE(tokens.resolve(QStringLiteral("color"), QStringLiteral("#05a3f0"), {}).token, QStringLiteral("--color-sky-500"));
        const TokenSet plain = TokenSet::fromScan({}, {{QStringLiteral("accent"), QColor(0x0a, 0x84, 0xff)}});
        QCOMPARE(plain.resolve(QStringLiteral("color"), QStringLiteral("#0b86fc"), {}).token, QStringLiteral("Omarchy accent"));
        QVERIFY(!plain.hasTailwind());
        // Unknown properties pass through.
        QCOMPARE(tokens.resolve(QStringLiteral("opacity"), QStringLiteral("0.5"), {}).value, QStringLiteral("0.5"));
        QCOMPARE(tokens.toJson()["colors"].toArray().size(), 5);
    }

    void cssVariablesAreTokensToo()
    {
        const TokenSet tokens = TokenSet::fromScan(tailwindScan());
        QCOMPARE(tokens.resolve(QStringLiteral("color"), QStringLiteral("#e3204a"), {}).token, QStringLiteral("--brand"));
        // Tailwind's scale first; CSS lengths when there's no scale.
        QJsonObject scan = tailwindScan();
        QJsonObject vars = scan["vars"].toObject();
        vars.remove("--spacing");
        scan["vars"] = vars;
        QCOMPARE(TokenSet::fromScan(scan).resolve(QStringLiteral("padding"), QStringLiteral("20px"), {}).value, QStringLiteral("24px"));
        QCOMPARE(TokenSet::pixels(QStringLiteral("1.5rem")).value(), 24.0);
        QVERIFY(!TokenSet::pixels(QStringLiteral("auto")));
    }

    void devCommandsFollowTheProject()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("npm")).isEmpty())
            QSKIP("npm isn't installed, and this test detects npm projects");
        QString why;
        const QString root = m_directory.filePath(QStringLiteral("projects"));
        write(root + "/override/omastrator.json", "{\"dev\": \"make serve\", \"url\": \"http://localhost:4000/\"}");
        write(root + "/override/package.json", "{\"scripts\": {\"dev\": \"vite\"}}");
        auto override = DevCommand::detect(root + "/override", &why);
        QVERIFY(override && override->kind == DevCommand::Kind::override);
        QCOMPARE(override->arguments.last(), QStringLiteral("make serve"));
        QCOMPARE(override->url, QUrl(QStringLiteral("http://localhost:4000/")));

        write(root + "/npm/package.json", "{\"scripts\": {\"start\": \"serve\", \"build\": \"x\"}}");
        write(root + "/npm/package-lock.json", "{}");
        auto npm = DevCommand::detect(root + "/npm", &why);
        QVERIFY2(npm, qPrintable(why));
        QCOMPARE(npm->description, QStringLiteral("npm run start"));
        QVERIFY(npm->install.isEmpty());
        // A fresh clone has packages to install before its dev script can run.
        write(root + "/fresh/package.json", "{\"scripts\": {\"dev\": \"vite\"}, \"devDependencies\": {\"vite\": \"^8\"}}");
        write(root + "/fresh/package-lock.json", "{}");
        auto fresh = DevCommand::detect(root + "/fresh", &why);
        QVERIFY2(fresh, qPrintable(why));
        QCOMPARE(fresh->install, QStringList{QStringLiteral("install")});
        QVERIFY(QDir(root + "/fresh").mkdir(QStringLiteral("node_modules")));
        QVERIFY(DevCommand::detect(root + "/fresh", &why)->install.isEmpty());

        write(root + "/stat/index.html", "<h1>hi</h1>");
        QCOMPARE(DevCommand::detect(root + "/stat", &why)->kind, DevCommand::Kind::staticSite);

        write(root + "/empty/README.md", "nothing");
        QVERIFY(!DevCommand::detect(root + "/empty", &why));
        QVERIFY(why.contains(QLatin1String("nothing to open")));

        // A lockfile names a package manager that may not be installed; that is said plainly.
        write(root + "/pnpm/package.json", "{\"scripts\": {\"dev\": \"vite\"}}");
        write(root + "/pnpm/pnpm-lock.yaml", "lockfileVersion: 9");
        const auto pnpm = DevCommand::detect(root + "/pnpm", &why);
        QVERIFY(pnpm ? pnpm->description == QLatin1String("pnpm run dev") : why.contains(QLatin1String("pnpm, which isn't installed")));

        QCOMPARE(DevServer::urlIn(QStringLiteral("\x1b[32m  ➜  Local:\x1b[0m   http://localhost:\x1b[1m5173\x1b[0m/\n")),
                 QUrl(QStringLiteral("http://localhost:5173/")));
        QCOMPARE(DevServer::urlIn(QStringLiteral("Listening on http://0.0.0.0:3000")), QUrl(QStringLiteral("http://127.0.0.1:3000")));
        QVERIFY(DevServer::urlIn(QStringLiteral("see https://vite.dev for help")).isEmpty());
    }

    void registryRemembersAndSuggests()
    {
        const QUrl site(QStringLiteral("https://www.example.com/about?x=1"));
        QCOMPARE(ProjectRegistry::originOf(site), QStringLiteral("https://www.example.com"));
        QVERIFY(!ProjectRegistry::folderFor(site));
        const QString roots = m_directory.filePath(QStringLiteral("code"));
        write(roots + "/example.com/index.html", "<h1>x</h1>");
        write(roots + "/shop/.vercel/project.json", "{\"projectId\": \"p\", \"orgId\": \"o\", \"projectName\": \"shop\"}");
        write(roots + "/worker/wrangler.toml", "name = \"edge-api\"\nmain = \"src/index.ts\"\n");
        write(roots + "/blog/netlify.toml", "[build]\n  publish = \"dist\"\n# https://blog.example.org\n");
        write(roots + "/nested/landing/package.json", "{\"name\": \"landing\", \"homepage\": \"https://landing.example.net\"}");
        write(roots + "/remote/.git/config", "[remote \"origin\"]\n\turl = git@github.com:someone/portfolio.git\n");

        auto top = [&](const QString &url) {
            const auto found = ProjectRegistry::suggest(QUrl(url), {roots});
            return found.empty() ? QString() : QFileInfo(found.front().folder).fileName();
        };
        QCOMPARE(top(QStringLiteral("https://www.example.com")), QStringLiteral("example.com"));
        QCOMPARE(top(QStringLiteral("https://shop-git-main-team.vercel.app")), QStringLiteral("shop"));
        QCOMPARE(top(QStringLiteral("https://edge-api.team.workers.dev")), QStringLiteral("worker"));
        QCOMPARE(top(QStringLiteral("https://blog.example.org")), QStringLiteral("blog"));
        QCOMPARE(top(QStringLiteral("https://landing.example.net")), QStringLiteral("landing"));
        QCOMPARE(top(QStringLiteral("https://portfolio.pages.dev")), QStringLiteral("remote"));
        QVERIFY(!ProjectRegistry::suggest(QUrl(QStringLiteral("https://nothing.test")), {roots}).size());
        QVERIFY(!ProjectRegistry::suggest(QUrl(QStringLiteral("https://shop.example.com")), {roots}).front().reason.isEmpty());

        QVERIFY(ProjectRegistry::remember(site, roots + "/example.com").isEmpty());
        QCOMPARE(ProjectRegistry::folderFor(QUrl(QStringLiteral("https://www.example.com/other"))).value(),
                 QFileInfo(roots + "/example.com").canonicalFilePath());
        QVERIFY(!ProjectRegistry::folderFor(QUrl(QStringLiteral("http://www.example.com"))));
        QVERIFY(ProjectRegistry::path().startsWith(m_directory.path()));
        QVERIFY(ProjectRegistry::remember(site, roots + "/missing").contains(QLatin1String("isn't a folder")));
        QVERIFY(ProjectRegistry::forget(site).isEmpty());
        QVERIFY(!ProjectRegistry::folderFor(site));
    }

    void sitesRememberedFromSeveralThreadsAreAllKept()
    {
        const QString folder = m_directory.filePath(QStringLiteral("shared-registry-folder"));
        QDir().mkpath(folder);
        constexpr int threads = 6;
        constexpr int each = 12;
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([folder, t] {
                for (int i = 0; i < each; ++i)
                    ProjectRegistry::remember(QUrl(QStringLiteral("https://thread%1-site%2.example.test").arg(t).arg(i)), folder);
            });
        for (std::thread &worker : workers)
            worker.join();
        // Each remember is a read, a change and a write: two at once would lose one of the changes.
        int found = 0;
        for (int t = 0; t < threads; ++t)
            for (int i = 0; i < each; ++i)
                if (ProjectRegistry::folderFor(QUrl(QStringLiteral("https://thread%1-site%2.example.test").arg(t).arg(i))))
                    ++found;
        QCOMPARE(found, threads * each);
    }

    void localhostPortsNameTheirFolder()
    {
        QTcpServer listening;
        QVERIFY(listening.listen(QHostAddress::LocalHost, 0));
        const auto folder = ProjectRegistry::folderServing(listening.serverPort());
        QVERIFY(folder);
        // This test's own working folder, or the project above it.
        QVERIFY2(QDir::currentPath().startsWith(*folder) || folder->startsWith(QDir::currentPath()), qPrintable(*folder));
        QVERIFY(!ProjectRegistry::folderServing(1));
    }

    void staticServerServesOnlyTheFolder()
    {
        const QString site = m_directory.filePath(QStringLiteral("site"));
        write(site + "/index.html", "<h1>home</h1>");
        write(site + "/css/a.css", "h1{}");
        write(m_directory.filePath(QStringLiteral("secret.txt")), "no");
        StaticServer server;
        QVERIFY(server.serve(site).isEmpty());
        int status = 0;
        QCOMPARE(get(server.url(), &status), QByteArray("<h1>home</h1>"));
        QCOMPARE(status, 200);
        QCOMPARE(get(server.url().resolved(QUrl(QStringLiteral("css/a.css"))), &status), QByteArray("h1{}"));
        get(server.url().resolved(QUrl(QStringLiteral("/../secret.txt"))), &status);
        QCOMPARE(status, 404);
        get(server.url().resolved(QUrl(QStringLiteral("/%2e%2e/secret.txt"))), &status);
        QCOMPARE(status, 404);
        QCOMPARE(StaticServer::mimeType(QStringLiteral("x.svg")), QByteArray("image/svg+xml"));
    }

    void removingEditsForgetsExactlyTheNamedOnes()
    {
        const auto edit = [](const QString &selector, const QString &property, const QString &after) {
            LiveEdit made;
            made.selector = selector;
            made.property = property;
            made.before = QStringLiteral("0");
            made.after = after;
            return made;
        };
        LiveSession session;
        QSignalSpy changed(&session, &LiveSession::changed);
        session.setEdits({edit("#a", "opacity", "0.5"), edit("#b", "opacity", "0.6"), edit("#c", "opacity", "0.7")});
        changed.clear();

        // The edit made since the caller read the list (#c) and one that shares a target but not a value stay.
        session.removeEdits({edit("#a", "opacity", "0.5"), edit("#b", "opacity", "0.9")});
        QCOMPARE(session.edits().size(), size_t(2));
        QCOMPARE(session.edits()[0].selector, QStringLiteral("#b"));
        QCOMPARE(session.edits()[1].selector, QStringLiteral("#c"));
        QCOMPARE(changed.size(), 1);

        // Naming nothing that is there changes nothing and says nothing.
        session.removeEdits({edit("#z", "opacity", "1")});
        QCOMPARE(session.edits().size(), size_t(2));
        QCOMPARE(changed.size(), 1);
    }

    void viteHelperMarksSourceLocations()
    {
        const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
        if (node.isEmpty())
            QSKIP("node isn't installed.");
        QProcess run;
        run.start(node, {QStringLiteral("--input-type=module"), QStringLiteral("-e"),
                         QStringLiteral("import { markSources } from '" OMASTRATOR_SOURCE_DIR "/extras/vite-plugin-omastrator/index.js';"
                                        "process.stdout.write(markSources('<html><head><style>p<b{}</style></head>\\n<body>\\n  <p class=\"x\">Hi</p>"
                                        "<!-- <i> --></body></html>', 'src/page.html'));")});
        QVERIFY(run.waitForFinished(20'000));
        const QString out = QString::fromUtf8(run.readAllStandardOutput());
        QVERIFY2(out.contains(QLatin1String("<p data-oma-src=\"src/page.html:3:3\" class=\"x\">")), qPrintable(out + run.readAllStandardError()));
        QVERIFY(out.contains(QLatin1String("<body data-oma-src=\"src/page.html:2:1\">")));
        QVERIFY(!out.contains(QLatin1String("<head data-oma")) && !out.contains(QLatin1String("<i data-oma")) && !out.contains(QLatin1String("<b data-oma")));
    }
};

QTEST_GUILESS_MAIN(LiveUnitTests)
#include "LiveUnitTests.moc"
