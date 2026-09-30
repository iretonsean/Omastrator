#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/AgentBridge.h"
#include "UI/BrowserViews.h"
#include "UI/CurveEditor.h"
#include "UI/LiveFrames.h"
#include "UI/MotionInspector.h"
#include "UI/MotionTimeline.h"
#include "UI/NumberField.h"
#include "UI/ProjectWorkspaceView.h"
#include "../Agent/FakeAgents.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <memory>

// Editing motion (docs/MOTION.md, section 3): the Motion inspector's fields preview a change on the live page as they are
// scrubbed, make one Live edit when let go, undo with Live's history, and Save writes the certain ones into a temporary
// git repository and changes nothing else. Headless Chromium on a throwaway profile; skips without it.
namespace {
constexpr int patience = 60'000;

constexpr const char *fakeOmarchy =
    "#!/bin/sh\n"
    "if [ \"$1\" = default ]; then echo \"${FAKE_AGENT:-sh}\"; exit 0; fi\n"
    "exit 2\n";

void write(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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

class MotionEditTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    int m_sites = 0;
    QStringList m_folders;

    // The cards fixture as a project in git, served from where it is written and registered as the user's own.
    std::unique_ptr<Site> site()
    {
        auto made = std::make_unique<Site>();
        made->folder = QFileInfo(m_directory.path()).canonicalFilePath() + QStringLiteral("/cards%1").arg(++m_sites);
        m_folders.append(made->folder);
        for (const char *name : {"cards.html", "cards.css"}) {
            QFile source(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion/") + QLatin1String(name));
            if (!source.open(QIODevice::ReadOnly))
                return nullptr;
            write(made->folder + QLatin1Char('/') + (QLatin1String(name) == QLatin1String("cards.html") ? QStringLiteral("index.html") : QLatin1String(name)),
                  QLatin1String(name) == QLatin1String("cards.html") ? source.readAll().replace("cards.css", "cards.css") : source.readAll());
        }
        WriteBack::git(made->folder, {"init", "-q", "-b", "main"});
        WriteBack::git(made->folder, {"add", "-A"});
        WriteBack::git(made->folder, {"commit", "-q", "-m", "First"});
        if (!made->server.serve(made->folder).isEmpty())
            return nullptr;
        if (!ProjectRegistry::remember(page(*made), made->folder).isEmpty())
            return nullptr;
        return made;
    }

    QUrl page(const Site &served) const { return QUrl(served.server.url().toString() + QStringLiteral("index.html")); }

    static QJsonValue inPage(EditorSession &session, const QUuid &frame, const QString &expression)
    {
        auto value = std::make_shared<QJsonValue>();
        bool answered = false;
        LiveFrames::of(session)->run(frame, [value, expression](LiveSession &live) {
            *value = live.evaluate(expression);
            return QString();
        }, [&answered](const QString &) { answered = true; });
        for (int i = 0; i < 600 && !answered; ++i)
            QTest::qWait(25);
        return *value;
    }

    // The timeline open on the frame, and the cards' row selected: `row` is its id.
    static void openOnCards(EditorSession &session, MotionTimeline &timeline, const QUuid &frame, QString &row)
    {
        BrowserViews *views = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(frame).isNull(), patience);
        const QString failure = timeline.open(frame);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.timeline().tracks.isEmpty(), patience);
        QStringList labels;
        for (const Motion::Track &track : timeline.timeline().tracks) {
            labels << track.label;
            if (track.label == QStringLiteral("section .bean-card × 3"))
                row = track.id;
        }
        QVERIFY2(!row.isEmpty(), qPrintable(labels.join(QStringLiteral(" | "))));
        timeline.selectRow(row);
    }

    // Drags a number field's handle by `dx` px, and lets go, as a designer scrubs it.
    static void scrub(NumberField *field, int dx, bool release = true)
    {
        QLabel *handle = field->handle();
        const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButtons buttons) {
            QMouseEvent event(type, at, handle->mapToGlobal(at), Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(handle, &event);
        };
        send(QEvent::MouseButtonPress, QPointF(2, 2), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(2 + dx / 2, 2), Qt::LeftButton);
        send(QEvent::MouseMove, QPointF(2 + dx, 2), Qt::LeftButton);
        if (release)
            send(QEvent::MouseButtonRelease, QPointF(2 + dx, 2), Qt::NoButton);
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
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        const QString gitconfig = m_directory.filePath(QStringLiteral("gitconfig"));
        write(gitconfig, "[user]\n\tname = Omastrator Tests\n\temail = tests@example.invalid\n[init]\n\tdefaultBranch = main\n");
        qputenv("GIT_CONFIG_GLOBAL", gitconfig.toUtf8());
        qputenv("GIT_CONFIG_NOSYSTEM", "1");
        write(m_directory.filePath(QStringLiteral("omarchy")), fakeOmarchy, true);
        qputenv("OMASTRATOR_OMARCHY", m_directory.filePath(QStringLiteral("omarchy")).toUtf8());
        QDir().mkpath(m_directory.filePath(QStringLiteral("agents")));
        qputenv("FAKE_OUT", m_directory.filePath(QStringLiteral("agents")).toUtf8());
        qputenv("FAKE_AGENT", "claude");
        qputenv("FAKE_MODE", "quiet");
        qputenv("OMASTRATOR_GH", "/bin/false");
        const QString bin = FakeAgents::install(m_directory.path());
        QVERIFY(!bin.isEmpty());
        qputenv("PATH", (bin + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
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

    void theInspectorShowsTheRowsTokensAsFields()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.bindings().duration.isEmpty(), patience);
        // The code holds each value in a token, and the fields are those tokens.
        QCOMPARE(timeline.bindings().duration, QStringLiteral("--duration-cascade"));
        QCOMPARE(timeline.bindings().easing, QStringLiteral("--ease-cascade"));
        QCOMPARE(timeline.bindings().stagger, QStringLiteral("--stagger-cascade"));
        auto *duration = inspector.findChild<NumberField *>(QStringLiteral("motionInspectorDuration"));
        auto *stagger = inspector.findChild<NumberField *>(QStringLiteral("motionInspectorStagger"));
        QVERIFY(duration && stagger);
        QCOMPARE(duration->value(), 640.0);
        QCOMPARE(stagger->value(), 140.0);
        QVERIFY(stagger->isEnabled());
        QVERIFY(inspector.findChild<CurveEditor *>());
        auto *presets = inspector.findChild<QComboBox *>(QStringLiteral("motionInspectorEasingPreset"));
        QVERIFY(presets);
        QCOMPARE(presets->currentText(), QStringLiteral("Ease out quart"));
        // The block's own tokens are listed, editable.
        QVERIFY(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorToken:--stagger-cascade")));
        QVERIFY(inspector.findChild<QLineEdit *>(QStringLiteral("motionInspectorToken:--ease-cascade")));
        QVERIFY(inspector.findChild<QCheckBox *>(QStringLiteral("motionInspectorReduced"))->isChecked());
    }

    void aDurationScrubShowsOnThePageAndRecordsOneEdit()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.bindings().duration.isEmpty(), patience);
        LiveFrames *frames = LiveFrames::of(session);
        auto *duration = inspector.findChild<NumberField *>(QStringLiteral("motionInspectorDuration"));
        QVERIFY(duration);
        QSignalSpy pending(frames, &LiveFrames::changed);
        // Held down and moved: the page shows it, and nothing is recorded yet.
        scrub(duration, 60, false);
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("document.documentElement.style.getPropertyValue('--duration-cascade')")).toString(), QStringLiteral("700ms"), patience);
        QCOMPARE(inPage(session, hosted.frame, QStringLiteral("document.documentElement.style.getPropertyValue('--duration-cascade')")).toString(), QStringLiteral("700ms"));
        QVERIFY(frames->snapshot(hosted.frame).edits.empty());
        // Let go: one edit, on :root, and the timeline reads the new duration back from the page.
        QLabel *handle = duration->handle();
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(62, 2), handle->mapToGlobal(QPointF(62, 2)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(handle, &release);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        const LiveEdit edit = frames->snapshot(hosted.frame).edits.front();
        QCOMPARE(edit.selector, QStringLiteral(":root"));
        QCOMPARE(edit.property, QStringLiteral("--duration-cascade"));
        QCOMPARE(edit.after, QStringLiteral("700ms"));
        QCOMPARE(edit.before, QStringLiteral("640ms"));
        QTRY_COMPARE_WITH_TIMEOUT(timeline.selectedTrack() ? timeline.selectedTrack()->duration : 0.0, 700.0, patience);
        // The page's animations run on it: seeking to the end of the last card's delay plus 700 ms ends them.
        QVERIFY(frames->snapshot(hosted.frame).canUndo);
        // Scrubbing is not a document edit.
        QVERIFY(!session.isModified());
    }

    void ctrlZBringsBackTheValueTheCurveAndTheKeyframes()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.bindings().easing.isEmpty(), patience);
        LiveFrames *frames = LiveFrames::of(session);
        const auto keyframeOpacity = [&] {
            return inPage(session, hosted.frame, QStringLiteral("document.getAnimations().find(a => a.animationName === 'nl-cascade').effect.getKeyframes()[0].opacity")).toString();
        };
        QCOMPARE(keyframeOpacity(), QStringLiteral("0"));

        // The curve: drag its first handle and let go; the token's value on the page is the new curve.
        auto *curve = inspector.findChild<CurveEditor *>();
        QVERIFY(curve);
        const QPointF from = curve->handle(1);
        const QPointF to = curve->pointOf(0.5, 0.75);
        QTest::mousePress(curve, Qt::LeftButton, Qt::NoModifier, from.toPoint());
        QTest::mouseMove(curve, to.toPoint());
        QTest::mouseRelease(curve, Qt::LeftButton, Qt::NoModifier, to.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        const QString curved = frames->snapshot(hosted.frame).edits.front().after;
        QVERIFY2(curved.startsWith(QStringLiteral("cubic-bezier(0.5, 0.75, ")), qPrintable(curved));
        QCOMPARE(inPage(session, hosted.frame, QStringLiteral("document.documentElement.style.getPropertyValue('--ease-cascade')")).toString(), curved);

        // A keyframe's value, typed: the running animations take it, and it is a second edit.
        auto *toggle = inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorKeyframesToggle"));
        QVERIFY(toggle);
        toggle->click();
        auto *opacity = inspector.findChild<QLineEdit *>(QStringLiteral("motionInspectorKeyframe:from:opacity"));
        QVERIFY(opacity);
        opacity->setText(QStringLiteral("0.4"));
        QTest::keyClick(opacity, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(2), patience);
        QTRY_COMPARE_WITH_TIMEOUT(keyframeOpacity(), QStringLiteral("0.4"), patience);
        const LiveEdit keyframe = frames->snapshot(hosted.frame).edits.back();
        QCOMPARE(keyframe.selector, QStringLiteral("@keyframes nl-cascade"));
        QCOMPARE(keyframe.property, QStringLiteral("from opacity"));
        QCOMPARE(keyframe.before, QStringLiteral("0"));
        // The page shows it: held at the start, the cards are at 0.4.
        timeline.scrubTo(0);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.busy(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(inPage(session, hosted.frame, QStringLiteral("parseFloat(getComputedStyle(document.querySelector('#nyeri')).opacity)")).toDouble() - 0.4) < 0.01, patience);

        // Ctrl+Z, twice: the keyframes come back, then the curve.
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        QTRY_COMPARE_WITH_TIMEOUT(keyframeOpacity(), QStringLiteral("0"), patience);
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(0), patience);
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("document.documentElement.style.getPropertyValue('--ease-cascade')")).toString(), QString(), patience);
        // And redo does the curve and then the keyframe again, from the states it saved.
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).canRedo, patience);
        hosted.canvas.redoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        QTRY_VERIFY_WITH_TIMEOUT(frames->snapshot(hosted.frame).canRedo, patience);
        hosted.canvas.redoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(keyframeOpacity(), QStringLiteral("0.4"), patience);
        QVERIFY(!session.isModified());
    }

    void savingWritesTheTokenAndTheScopedIndexAndNothingElse()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        EditorSession &session = workspace.current().session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.bindings().duration.isEmpty(), patience);
        LiveFrames *frames = LiveFrames::of(session);
        const QByteArray before = read(served->folder + "/cards.css");
        const QByteArray html = read(served->folder + "/index.html");

        // A duration token, an easing token, a keyframe value, and one card's own index.
        timeline.setToken(QStringLiteral("--duration-cascade"), QStringLiteral("800ms"), false);
        timeline.setToken(QStringLiteral("--ease-cascade"), QStringLiteral("cubic-bezier(0.34, 1.56, 0.64, 1)"), false);
        timeline.setKeyframe(QStringLiteral("from"), QStringLiteral("opacity"), QStringLiteral("0.2"));
        bool done = false;
        frames->run(hosted.frame, [](LiveSession &live) { return live.motionSetProperty(QStringLiteral("#huila"), QStringLiteral("--i"), QStringLiteral("5")); },
                    [&done](const QString &error) {
                        QVERIFY2(error.isEmpty(), qPrintable(error));
                        done = true;
                    });
        QTRY_VERIFY_WITH_TIMEOUT(done, patience);
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(4), patience);
        // Nothing is written before Save.
        QCOMPARE(read(served->folder + "/cards.css"), before);

        QString request;
        const QString failure = bridge.liveWriteBack(&request, served->folder);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // Every one of them was certain: nothing was left for the agent.
        QVERIFY2(request.isEmpty(), qPrintable(request));
        const QByteArray after = read(served->folder + "/cards.css");
        QByteArray expected = before;
        expected.replace("--duration-cascade: 640ms;", "--duration-cascade: 800ms;");
        expected.replace("--ease-cascade: cubic-bezier(0.25, 1, 0.5, 1);", "--ease-cascade: cubic-bezier(0.34, 1.56, 0.64, 1);");
        expected.replace("@keyframes nl-cascade { from { opacity: 0;", "@keyframes nl-cascade { from { opacity: 0.2;");
        expected.replace("#huila { --i: 2; }", "#huila { --i: 5; }");
        QCOMPARE(after, expected);
        // Only that file changed.
        QCOMPARE(read(served->folder + "/index.html"), html);
        QCOMPARE(WriteBack::git(served->folder, {"status", "--porcelain", "--untracked-files=no"}).trimmed(), QStringLiteral("M cards.css"));
        QVERIFY(bridge.pendingEdits(served->folder).empty());
    }

    void reducedMotionOffThenOnAgainLeavesTheCodeAsItWasAndOffWritesItOut()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        ProjectWorkspace workspace;
        ProjectWorkspaceView window(workspace);
        AgentBridge &bridge = *window.agent();
        EditorSession &session = workspace.current().session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        QTRY_VERIFY_WITH_TIMEOUT(inspector.findChild<QCheckBox *>(QStringLiteral("motionInspectorReduced")) != nullptr, patience);
        LiveFrames *frames = LiveFrames::of(session);
        const QByteArray before = read(served->folder + "/cards.css");
        const auto box = [&] { return inspector.findChild<QCheckBox *>(QStringLiteral("motionInspectorReduced")); };
        QVERIFY(box()->isChecked());
        QVERIFY(timeline.reducedMotionOn());

        box()->click();
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.reducedMotionOn(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(!box()->isChecked(), patience);
        QCOMPARE(frames->snapshot(hosted.frame).edits.size(), size_t(1));
        // Nothing is written until Save; then on again nets to nothing.
        QCOMPARE(read(served->folder + "/cards.css"), before);
        box()->click();
        QTRY_VERIFY_WITH_TIMEOUT(timeline.reducedMotionOn(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(box()->isChecked(), patience);
        QString request;
        QVERIFY(bridge.liveWriteBack(&request, served->folder).isEmpty());
        QVERIFY(request.isEmpty());
        QCOMPARE(read(served->folder + "/cards.css"), before);

        // Off, and Save: the rule is out of the block, and only that.
        box()->click();
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.reducedMotionOn(), patience);
        QVERIFY(bridge.liveWriteBack(&request, served->folder).isEmpty());
        QVERIFY(request.isEmpty());
        QByteArray expected = before;
        expected.replace("@media (prefers-reduced-motion: reduce) { .bean-card { animation: none; } }\n", "");
        QCOMPARE(read(served->folder + "/cards.css"), expected);
        // The code is read again after a Save: the box shows what the file has.
        QTRY_VERIFY_WITH_TIMEOUT(!box()->isChecked() && !timeline.reducedMotionOn(), patience);
    }

    void aDurationTheCodeHoldsInNoTokenGoesToTheAgentAndShowsAtOnce()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        // The block loses its tokens: durations and easings are written out where they are used.
        QByteArray css = read(served->folder + "/cards.css");
        css.replace("animation: nl-cascade var(--duration-cascade) var(--ease-cascade) both;", "animation: nl-cascade 640ms linear both;");
        write(served->folder + "/cards.css", css);
        EditorSession session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        LiveFrames *frames = LiveFrames::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorDuration")) != nullptr, patience);
        QVERIFY(timeline.bindings().duration.isEmpty());
        auto *duration = inspector.findChild<NumberField *>(QStringLiteral("motionInspectorDuration"));
        QCOMPARE(duration->value(), 640.0);
        duration->field->setText(QStringLiteral("900"));
        duration->commit();
        // One edit for each card, in the page's own terms, for the agent: the code has nothing certain to change.
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(3), patience);
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().property, QStringLiteral("animation-duration"));
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().after, QStringLiteral("900ms"));
        const WriteBack::Plan plan = WriteBack::plan(served->folder, frames->snapshot(hosted.frame).edits);
        QCOMPARE(plan.unresolved.size(), size_t(3));
        QVERIFY(plan.changes.empty());
        // It is on the page at once, and one undo takes all three back.
        QTRY_COMPARE_WITH_TIMEOUT(timeline.selectedTrack() ? timeline.selectedTrack()->duration : 0.0, 900.0, patience);
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(0), patience);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.selectedTrack() ? timeline.selectedTrack()->duration : 0.0, 640.0, patience);
        QVERIFY(!session.isModified());
    }
};

QTEST_MAIN(MotionEditTests)
#include "MotionEditTests.moc"
