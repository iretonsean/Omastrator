#include "Canvas/EditorCanvas.h"
#include "Canvas/ElementBar.h"
#include "Document/EditorSession.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/BrowserViews.h"
#include "UI/ElementBarActions.h"
#include "UI/LiveFrames.h"
#include "UI/NumberField.h"
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

// Editing a Browser View's page from the element bar (docs/LIVE-IN-FRAME.md, sections 3 and 4) against a real headless
// page on a throwaway profile, served from a local folder. Skips without Chromium.
namespace {
constexpr int patience = 60'000;

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

struct Site {
    QString folder;
    StaticServer server;
};

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

class LiveFrameEditTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;

    std::unique_ptr<Site> site()
    {
        auto made = std::make_unique<Site>();
        made->folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/site%1").arg(++m_sites);
        for (const char *name : {"index.html", "second.html"}) {
            QFile source(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/liveframe/") + QLatin1String(name));
            if (!source.open(QIODevice::ReadOnly))
                return nullptr;
            write(made->folder + QLatin1Char('/') + QLatin1String(name), source.readAll());
        }
        WriteBack::git(made->folder, {"init", "-q", "-b", "main"});
        WriteBack::git(made->folder, {"add", "-A"});
        WriteBack::git(made->folder, {"commit", "-q", "-m", "First"});
        if (!made->server.serve(made->folder).isEmpty())
            return nullptr;
        return made;
    }

    QUrl page(const Site &served) const { return QUrl(served.server.url().toString() + QStringLiteral("index.html")); }

    // Evaluates in the frame's page and answers its text.
    QString evalPage(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        QString answer;
        bool done = false;
        LiveFrames::of(session)->run(frame, [&answer, expression](LiveSession &live) {
            answer = live.evaluate(expression).toVariant().toString();
            return QString();
        }, [&done](const QString &) { done = true; });
        for (int i = 0; i < 600 && !done; ++i)
            QTest::qWait(25);
        return answer;
    }

    void startEditing(EditorSession &session, Hosted &hosted, const QString &folder)
    {
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::of(session)->poolKey(hosted.frame).isNull(), patience);
        QVERIFY2(frames->start(hosted.frame, folder).isEmpty(), "Live must start");
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).state, LiveSession::State::running, patience);
        bool loaded = false;
        for (int i = 0; i < 300 && !loaded; ++i) {
            loaded = evalPage(session, hosted.frame, QStringLiteral("!!document.querySelector('#title') && !!window.__oma")) == QLatin1String("true");
            if (!loaded)
                QTest::qWait(100);
        }
        QVERIFY(loaded);
        QVERIFY(hosted.canvas.enterEditPage(hosted.frame));
    }

    void pick(EditorSession &session, Hosted &hosted, const QString &selector)
    {
        evalPage(session, hosted.frame, QStringLiteral("window.__oma.select('%1', false); 1").arg(selector));
        LiveFrames *frames = LiveFrames::of(session);
        const QString name = selector.mid(1);
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).selection.size() == 1
                                     && (frames->snapshot(hosted.frame).selection.first().toObject().value("id").toString() == name
                                         || frames->snapshot(hosted.frame).selection.first().toObject().value("classes").toString().split(' ').contains(name)),
                                 15'000);
    }

    // Waits for the queued commands to finish, then reads a style off the page.
    QString style(EditorSession &session, Hosted &hosted, const QString &selector, const QString &property)
    {
        return evalPage(session, hosted.frame, QStringLiteral("getComputedStyle(document.querySelector('%1')).%2").arg(selector, property));
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
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
    }

    void init()
    {
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
    }

    void cleanup()
    {
        LiveFrames::clearPending(QString());
        BrowserViews::shutdownPool();
    }

    void aChangeToSeveralPropertiesIsOneUndoStep()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startEditing(session, hosted, served->folder);
        LiveFrames *frames = LiveFrames::of(session);
        pick(session, hosted, QStringLiteral(".card"));

        BrowserViewHost *host = BrowserViews::of(session);
        QVERIFY(host->editElements(hosted.frame, {"word-spacing", "text-indent"}, QStringLiteral("13px"), false).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(2), 15'000);
        QCOMPARE(style(session, hosted, ".card", "wordSpacing"), QStringLiteral("13px"));
        QCOMPARE(style(session, hosted, ".card", "textIndent"), QStringLiteral("13px"));

        QVERIFY(hosted.canvas.canUndoPageEdit());
        hosted.canvas.undoPageEdit();
        QTRY_VERIFY_WITH_TIMEOUT(!hosted.canvas.canUndoPageEdit(), 15'000);
        // One step took both properties back. (Lengths that name a spacing token snap to it, so these are off-scale.)
        QCOMPARE(style(session, hosted, ".card", "wordSpacing"), QStringLiteral("0px"));
        QCOMPARE(style(session, hosted, ".card", "textIndent"), QStringLiteral("0px"));
        QVERIFY(hosted.canvas.canRedoPageEdit());
        hosted.canvas.redoPageEdit();
        QTRY_VERIFY_WITH_TIMEOUT(hosted.canvas.canUndoPageEdit(), 15'000);
        QCOMPARE(style(session, hosted, ".card", "wordSpacing"), QStringLiteral("13px"));
        QCOMPARE(style(session, hosted, ".card", "textIndent"), QStringLiteral("13px"));
    }

    void aScrubRecordsOneEdit()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startEditing(session, hosted, served->folder);
        LiveFrames *frames = LiveFrames::of(session);
        pick(session, hosted, QStringLiteral("#title"));
        BrowserViewHost *host = BrowserViews::of(session);

        for (const char *value : {"0.9", "0.7", "0.5"})
            QVERIFY(host->editElements(hosted.frame, {"opacity"}, QString::fromLatin1(value), true).isEmpty());
        QCOMPARE(style(session, hosted, "#title", "opacity"), QStringLiteral("0.5"));
        // Previews are only shown: nothing recorded, nothing to undo.
        QCOMPARE(frames->snapshot(hosted.frame).edits.size(), size_t(0));
        QVERIFY(!frames->snapshot(hosted.frame).canUndo);

        QVERIFY(host->editElements(hosted.frame, {"opacity"}, QStringLiteral("0.5"), false).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), 15'000);
        QCOMPARE(style(session, hosted, "#title", "opacity"), QStringLiteral("0.5"));
        // The recorded "before" is the page's, not the last preview's.
        hosted.canvas.undoPageEdit();
        QTRY_VERIFY_WITH_TIMEOUT(!hosted.canvas.canUndoPageEdit(), 15'000);
        QCOMPARE(style(session, hosted, "#title", "opacity"), QStringLiteral("1"));
        QCOMPARE(evalPage(session, hosted.frame, QStringLiteral("String(document.querySelector('#title').getAttribute('style'))")), QStringLiteral("null"));
    }

    void pageUndoNeverTouchesTheDocument()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        session.markSaved();
        startEditing(session, hosted, served->folder);
        pick(session, hosted, QStringLiteral("#title"));
        BrowserViewHost *host = BrowserViews::of(session);
        QVERIFY(host->editElements(hosted.frame, {"opacity"}, QStringLiteral("0.4"), false).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(hosted.canvas.canUndoPageEdit(), 15'000);
        // A page edit is not a document edit: no step, and the file stays as saved.
        QVERIFY(!session.canUndo());
        QVERIFY(!session.isModified());
        hosted.canvas.undoPageEdit();
        QTRY_VERIFY_WITH_TIMEOUT(!hosted.canvas.canUndoPageEdit(), 15'000);
        QVERIFY(!session.canUndo());
        QVERIFY(!session.isModified());
    }

    void editingTextChangesThePageAndUndoes()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        startEditing(session, hosted, served->folder);
        LiveFrames *frames = LiveFrames::of(session);
        pick(session, hosted, QStringLiteral("#title"));
        BrowserViewHost *host = BrowserViews::of(session);
        QVERIFY(host->editElementText(hosted.frame, QStringLiteral("#title"), QStringLiteral("A new title")).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), 15'000);
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().property, QStringLiteral("text"));
        QCOMPARE(evalPage(session, hosted.frame, QStringLiteral("document.querySelector('#title').textContent")), QStringLiteral("A new title"));
        hosted.canvas.undoPageEdit();
        QTRY_VERIFY_WITH_TIMEOUT(!hosted.canvas.canUndoPageEdit(), 15'000);
        QCOMPARE(evalPage(session, hosted.frame, QStringLiteral("document.querySelector('#title').textContent")), QStringLiteral("Hello from a frame"));
    }

    void theBarShowsThePickedValuesAndEditsThePage()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        ElementBar *bar = ElementBarActions::attach(nullptr, hosted.canvas);
        startEditing(session, hosted, served->folder);
        pick(session, hosted, QStringLiteral(".card"));
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), 15'000);
        NumberField *padding = bar->findChild<NumberField *>(QStringLiteral("elementPaddingX"));
        QVERIFY(padding);
        QTRY_COMPARE_WITH_TIMEOUT(padding->value(), 24.0, 15'000);
        QVERIFY(!padding->isMixed());
        NumberField *radius = bar->findChild<NumberField *>(QStringLiteral("elementRadius"));
        QVERIFY(radius);
        QCOMPARE(radius->value(), 12.0);

        // A length near the scale lands on it: 13 is nearest the gap token, 24.
        padding->field->setText(QStringLiteral("13"));
        padding->commit();
        QTRY_COMPARE_WITH_TIMEOUT(style(session, hosted, ".card", "paddingLeft"), QStringLiteral("24px"), 15'000);
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).edits.size(), size_t(2), 15'000);

        // Text takes the font size, which has no scale here, exactly as typed.
        pick(session, hosted, QStringLiteral("#title"));
        NumberField *size = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((size = bar->findChild<NumberField *>(QStringLiteral("elementFontSize"))) != nullptr, 15'000);
        size->field->setText(QStringLiteral("40"));
        size->commit();
        QTRY_COMPARE_WITH_TIMEOUT(style(session, hosted, "#title", "fontSize"), QStringLiteral("40px"), 15'000);
        // The bar reads the edit back from the page.
        QTRY_COMPARE_WITH_TIMEOUT(bar->findChild<NumberField *>(QStringLiteral("elementFontSize"))->value(), 40.0, 15'000);
    }
};

QTEST_MAIN(LiveFrameEditTests)
#include "LiveFrameEditTests.moc"
