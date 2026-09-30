#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/DevServers.h"
#include "Live/Registry.h"
#include "Rendering/VectorRenderer.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <memory>

// A frame's picture follows its page (docs/BROWSER-VIEW.md, section 3; docs/LIVE-IN-FRAME.md, section 2): after a
// pause, and once Live runs on the project's dev server, after the move to the server, edits, a file the agent wrote,
// Reload and a resize. What is checked is what the canvas draws. The dev command is Python's static server, so the app
// runs a real command and waits for its address. "Production" is a tiny server on 127.0.0.2, which isn't loopback as far
// as Live is concerned. Skips without Chromium, and the dev server's test without python3.
namespace {
constexpr int patience = 60'000;
constexpr int follow = 15'000;

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

// Answers every request with a grey page.
class Production : public QObject {
public:
    Production()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->readAll();
                    const QByteArray body = "<!doctype html><title>p</title><style>html,body{margin:0;height:100%;background:#777}</style>"
                                            "<h1 id=\"where\">production</h1>";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    bool listen() { return m_server.listen(QHostAddress(QStringLiteral("127.0.0.2")), 0); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.2:%1/index.html").arg(m_server.serverPort())); }

private:
    QTcpServer m_server;
};

// The project's page, in CSS px: #where on top, #title and #panel below it, a #band whose colour only the file sets,
// and the body's colour from style.css. The script stands in for a dev server's hot reload of the stylesheet.
QByteArray pageWithBand(const char *band)
{
    return QByteArray("<!doctype html><meta charset=\"utf-8\"><title>dev</title>\n"
                      "<link id=\"style\" rel=\"stylesheet\" href=\"style.css\">\n"
                      "<style>\n"
                      "html, body { margin: 0; height: 100%; overflow: hidden; }\n"
                      "#where { position: absolute; left: 0; top: 0; width: 100%; height: 60px; margin: 0; font: 40px/60px sans-serif; }\n"
                      "#title { position: absolute; left: 0; top: 80px; width: 380px; height: 100px; margin: 0; font: bold 80px/100px sans-serif; color: #000; }\n"
                      "#panel { position: absolute; left: 420px; top: 80px; width: 150px; height: 100px; }\n"
                      "#band { position: absolute; left: 0; top: 200px; width: 100%; height: 60px; }\n"
                      "</style>\n"
                      "<h1 id=\"where\">dev</h1><h2 id=\"title\">i</h2><div id=\"panel\"></div>\n"
                      "<div id=\"band\" style=\"background: ")
        + band
        + "\"></div>\n"
          "<script>\n"
          "let last = null;\n"
          "window.polls = 0;\n"
          "setInterval(async () => {\n"
          "  const text = await (await fetch('style.css', {cache: 'no-store'})).text();\n"
          "  if (last !== null && text !== last) document.getElementById('style').href = 'style.css?' + Date.now();\n"
          "  last = text;\n"
          "  window.polls++;\n"
          "}, 150);\n"
          "</script>\n";
}

// The body's colour at the frame's width, and another one below 500 CSS px.
QByteArray styleWith(const char *wide)
{
    return QByteArray("body { background: ") + wide + "; }\n@media (max-width: 500px) { body { background: #93c; } }\n";
}

bool near(const QColor &seen, const QColor &wanted)
{
    return seen.isValid() && qAbs(seen.red() - wanted.red()) < 48 && qAbs(seen.green() - wanted.green()) < 48 && qAbs(seen.blue() - wanted.blue()) < 48;
}

struct Hosted {
    EditorCanvas canvas;
    QUuid frame;
    Hosted(EditorSession &session, const QUrl &url) : canvas(session)
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({20, 20, 600, 400}, QStringLiteral("Site"));
        view.browser = BrowserView{url, {}, {}};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
        canvas.resize(1000, 800);
        canvas.show();
        BrowserViews::of(session)->attach(&canvas);
    }
};
}

#define NEEDS_CHROMIUM \
    if (Browser::executable().isEmpty()) \
        QSKIP("Chromium isn't installed.")

class LiveFramePictureTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QStringList m_folders;

    // The document as the canvas draws it, with the frame's live picture, at 1 px per pt.
    static QImage drawn(EditorSession &session)
    {
        QImage image(1000, 800, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        VectorRenderer::Options options;
        BrowserViews *views = BrowserViews::of(session);
        options.livePicture = [views](const QUuid &id) { return views->picture(id); };
        VectorRenderer::draw(painter, *session.document(), options);
        return image;
    }

    // The drawn colour at a point of the page, in CSS px from the frame's top left corner.
    static QColor at(EditorSession &session, const QUuid &frame, QPointF css)
    {
        const QPointF corner = session.document()->bounds(frame).topLeft();
        return drawn(session).pixelColor((corner + css).toPoint());
    }

    // How many dark pixels the canvas draws inside a rectangle of the page.
    static int ink(EditorSession &session, const QUuid &frame, QRectF css)
    {
        const QImage image = drawn(session);
        const QRect area = css.translated(session.document()->bounds(frame).topLeft()).toRect();
        int dark = 0;
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                dark += qGray(image.pixel(x, y)) < 80;
        return dark;
    }

    // Runs `expression` in the frame's tab straight off the pool, whether or not Live is on it; its value, or null.
    static QJsonValue inTab(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        const QUuid key = BrowserViews::of(session)->poolKey(frame);
        if (key.isNull())
            return {};
        auto answered = std::make_shared<std::atomic<bool>>(false);
        auto value = std::make_shared<QJsonValue>();
        BrowserViews::pool()->call(key, QStringLiteral("Runtime.evaluate"), {{"expression", expression}, {"returnByValue", true}},
                                   [answered, value](const QJsonObject &result, const QString &error) {
                                       if (error.isEmpty())
                                           *value = result.value("result").toObject().value("value");
                                       answered->store(true);
                                   });
        for (int i = 0; i < 200 && !answered->load(); ++i)
            QTest::qWait(25);
        return answered->load() ? *value : QJsonValue();
    }

    // The tab's own width in CSS px: it doesn't depend on the picture.
    static int tabWidth(EditorSession &session, const QUuid &frame) { return inTab(session, frame, QStringLiteral("innerWidth")).toInt(); }

    // The frame leaves the current page and comes back: its tab is paused (frozen), then woken.
    static void pauseAndResume(EditorSession &session, const QUuid &frame)
    {
        BrowserViews *views = BrowserViews::of(session);
        session.addPage(QStringLiteral("Other"));
        QTRY_COMPARE_WITH_TIMEOUT(views->state(frame), BrowserViews::State::paused, follow);
        session.undo();
        QTRY_COMPARE_WITH_TIMEOUT(views->state(frame), BrowserViews::State::live, follow);
    }

    void edit(EditorSession &session, const QUuid &frame, const QString &selector, const QString &property, const QString &value)
    {
        LiveFrames *frames = LiveFrames::of(session);
        QString failure = QStringLiteral("pending");
        frames->edit(frame, selector, property, value, [&](const QString &error) { failure = error; });
        QTRY_VERIFY_WITH_TIMEOUT(failure != QLatin1String("pending"), patience);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    // Running is the tab's; the dev server's page and its overlay may still be loading.
    void waitForOverlay(EditorSession &session, const QUuid &frame)
    {
        LiveFrames *frames = LiveFrames::of(session);
        // The question runs on the pool's thread.
        auto loaded = std::make_shared<std::atomic<bool>>(false);
        for (int i = 0; i < 300 && !loaded->load(); ++i) {
            bool answered = false;
            frames->run(frame, [loaded](LiveSession &live) {
                loaded->store(live.evaluate(QStringLiteral("!!document.querySelector('#panel') && !!window.__oma")).toBool());
                return QString();
            }, [&answered](const QString &) { answered = true; });
            QTRY_VERIFY_WITH_TIMEOUT(answered, 15'000);
            if (!loaded->load())
                QTest::qWait(100);
        }
        QVERIFY(loaded->load());
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
        QFile::remove(ProjectRegistry::path());
    }

    void cleanup()
    {
        for (const QString &folder : std::as_const(m_folders))
            LiveFrames::clearPending(folder);
        m_folders.clear();
        BrowserViews::shutdownPool();
    }

    void aFramesPictureFollowsThePageAfterItWasPaused()
    {
        NEEDS_CHROMIUM;
        Production production;
        QVERIFY(production.listen());
        EditorSession session;
        Hosted hosted(session, production.url());
        const QUuid frame = hosted.frame;
        const QPointF body(200, 320);
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x77, 0x77, 0x77)), "the page never showed", patience);
        // Twice: each pause freezes the tab, and each return must wake it fully.
        for (const char *colour : {"#e11d48", "#0cc"}) {
            pauseAndResume(session, frame);
            QVERIFY(inTab(session, frame, QStringLiteral("document.body.style.background = '%1'").arg(QLatin1String(colour))).isString());
            QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(QLatin1String(colour))), qPrintable(QStringLiteral("the canvas kept the picture from before the pause, not %1").arg(QLatin1String(colour))), follow);
        }
    }

    void aFramesPictureFollowsThePageOnItsDevServer()
    {
        NEEDS_CHROMIUM;
        if (QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty())
            QSKIP("python3 isn't installed, and the fixture's dev server is Python's.");
        Production production;
        QVERIFY(production.listen());
        const QString folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/project");
        m_folders.append(folder);
        write(folder + QStringLiteral("/index.html"), pageWithBand("#888"));
        write(folder + QStringLiteral("/style.css"), styleWith("#3a3"));
        write(folder + QStringLiteral("/omastrator.json"), QJsonDocument(QJsonObject{{"dev", "exec python3 -u -m http.server 0 --bind 127.0.0.1"}}).toJson());
        QVERIFY(ProjectRegistry::remember(production.url(), folder).isEmpty());

        EditorSession session;
        Hosted hosted(session, production.url());
        const QUuid frame = hosted.frame;
        BrowserViews *views = BrowserViews::of(session);
        LiveFrames *frames = LiveFrames::of(session);
        const QPointF body(200, 320);
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x77, 0x77, 0x77)), "the production page never showed", patience);

        // First what the session that found the stale picture did before Edit Page: another page shown and back, then
        // a width preview dragged to a phone's width and let go.
        pauseAndResume(session, frame);
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x77, 0x77, 0x77)), "the production page didn't come back", follow);
        session.select({frame});
        session.beginPreview(QStringLiteral("Preview Width"));
        session.previewFrameBox(frame, {20, 20, 768, 400});
        QTRY_COMPARE_WITH_TIMEOUT(tabWidth(session, frame), 768, follow);
        session.previewFrameBox(frame, {20, 20, 390, 400});
        QTRY_COMPARE_WITH_TIMEOUT(tabWidth(session, frame), 390, follow);
        session.cancelInteraction();
        QTRY_COMPARE_WITH_TIMEOUT(tabWidth(session, frame), 600, follow);

        // Edit Page starts the project's dev command, and the tab moves to it: the canvas shows the dev page.
        QVERIFY2(views->beginEditPage(frame).isEmpty(), "Edit Page must start");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(frame).state, LiveSession::State::running, patience);
        QTRY_VERIFY_WITH_TIMEOUT(!frames->snapshot(frame).serverUrl.isEmpty(), patience);
        QVERIFY2(views->bar(frame).devTip.contains(QLatin1String("http.server")), qPrintable(views->bar(frame).devTip));
        QCOMPARE(DevServers::shared().holders(folder), 1);
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x33, 0xaa, 0x33)), "the canvas kept production's picture after the move to the dev server", patience);
        QTRY_VERIFY_WITH_TIMEOUT(near(at(session, frame, {300, 230}), QColor(0x88, 0x88, 0x88)), follow);
        waitForOverlay(session, frame);
        // Paused and woken again while Live runs.
        pauseAndResume(session, frame);

        // A text edit.
        const QRectF title(0, 80, 380, 100);
        const int before = ink(session, frame, title);
        edit(session, frame, QStringLiteral("#title"), QStringLiteral("text"), QStringLiteral("MMMM"));
        QTRY_VERIFY2_WITH_TIMEOUT(ink(session, frame, title) > before + 1500, "the canvas kept the picture from before the text edit", follow);

        // A style edit.
        edit(session, frame, QStringLiteral("#panel"), QStringLiteral("background-color"), QStringLiteral("#e11d48"));
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, {495, 130}), QColor(0xe1, 0x1d, 0x48)), "the canvas kept the picture from before the style edit", follow);

        // Build It's result: the agent writes the stylesheet, and the server's hot reload brings it into the page.
        write(folder + QStringLiteral("/style.css"), styleWith("#33e"));
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x33, 0x33, 0xee)), "the canvas kept the picture from before the file changed", follow);

        // Reload: a change the page picks up only when it loads again.
        write(folder + QStringLiteral("/index.html"), pageWithBand("#f90"));
        // The page's hot reload polls the stylesheet, never this file. Waiting isn't proof it looked: the poll has to have run
        // twice since the write (the first pass may have begun before it), and the canvas is checked after that.
        const int polls = inTab(session, frame, QStringLiteral("window.polls")).toInt();
        QVERIFY2(polls > 0, "the page's poll never ran, so it can't show that it leaves this file alone");
        QTRY_VERIFY_WITH_TIMEOUT(inTab(session, frame, QStringLiteral("window.polls")).toInt() >= polls + 2, follow);
        QVERIFY(near(at(session, frame, {300, 230}), QColor(0x88, 0x88, 0x88)));
        views->act(frame, BrowserViewHost::Action::reload);
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, {300, 230}), QColor(0xff, 0x99, 0x00)), "the canvas kept the picture from before Reload", follow);
        QTRY_VERIFY_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x33, 0x33, 0xee)), follow);

        // A resize to a phone's width: the page's narrow layout, as wide as the frame.
        session.setDesignBox(frame, {20, 20, 390, 400});
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, body), QColor(0x99, 0x33, 0xcc)), "the canvas kept the wide layout after the resize", follow);
        QTRY_VERIFY2_WITH_TIMEOUT(qAbs(views->picture(frame).deviceIndependentSize().width() - 390) <= 2, "the picture isn't 390 CSS px wide", follow);
        QVERIFY(near(at(session, frame, {370, 320}), QColor(0x99, 0x33, 0xcc)));

        // And the stream carries on after all of that.
        waitForOverlay(session, frame);
        edit(session, frame, QStringLiteral("#where"), QStringLiteral("background-color"), QStringLiteral("#0cc"));
        QTRY_VERIFY2_WITH_TIMEOUT(near(at(session, frame, {300, 30}), QColor(0x00, 0xcc, 0xcc)), "the canvas stopped following the page", follow);

        frames->stop(frame);
        QCOMPARE(DevServers::shared().holders(folder), 0);
    }
};

QTEST_MAIN(LiveFramePictureTests)
#include "LiveFramePictureTests.moc"
