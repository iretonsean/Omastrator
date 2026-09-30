#include "Canvas/EditorCanvas.h"
#include "Document/EditorSession.h"
#include "Live/AgentWork.h"
#include "Live/Registry.h"
#include "Live/StaticServer.h"
#include "Live/WriteBack.h"
#include "UI/BrowserViews.h"
#include "UI/LiveFrames.h"
#include "UI/MotionInspector.h"
#include "UI/MotionTimeline.h"
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <memory>

// Reduced motion and what starts a row (docs/MOTION.md, sections 3, 4 and 7): Preview reduced plays the page as for a visitor who asked
// for less motion, the inspector warns about motion that has no rule for them, and changing what starts a row is shown where the page
// can show it and is an edit the agent finishes. Headless Chromium on a throwaway profile; skips without it.
namespace {
constexpr int patience = 60'000;

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

class ReducedMotionTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_motion;
    StaticServer m_cards;

    QUrl page(StaticServer &server, const QString &path) const
    {
        QUrl url = server.url();
        url.setPath(path);
        return url;
    }

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

    // The timeline open on the frame, and the row with this label picked.
    static void openAndSelect(EditorSession &session, MotionTimeline &timeline, const QUuid &frame, const QString &label)
    {
        BrowserViews *views = BrowserViews::of(session);
        QTRY_VERIFY_WITH_TIMEOUT(!views->poolKey(frame).isNull(), patience);
        const QString failure = timeline.open(frame);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.timeline().tracks.isEmpty(), patience);
        QString id;
        QStringList labels;
        for (const Motion::Track &track : timeline.timeline().tracks) {
            labels << track.label;
            if (track.label == label)
                id = track.id;
        }
        QVERIFY2(!id.isEmpty(), qPrintable(labels.join(QStringLiteral(" | "))));
        timeline.selectRow(id);
        QCOMPARE(timeline.selectedId(), id);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_STATE_HOME", m_directory.filePath(QStringLiteral("state")).toUtf8());
        qputenv("OMASTRATOR_SOCKET", m_directory.filePath(QStringLiteral("o.sock")).toUtf8());
        QVERIFY(m_motion.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion")).isEmpty());
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

    void cleanup() { BrowserViews::shutdownPool(); }

    void previewReducedPlaysThePageForSomeoneWhoAskedForLess()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_motion, QStringLiteral("/cards.html")));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        openAndSelect(session, timeline, hosted.frame, QStringLiteral("section .bean-card × 3"));
        const QString cards = QStringLiteral("document.getAnimations().filter(a => a.animationName === 'nl-cascade').length");
        QCOMPARE(inPage(session, hosted.frame, cards).toInt(), 3);
        auto *button = inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorPreviewReduced"));
        QVERIFY(button && !button->isChecked());
        button->click();
        QVERIFY(timeline.previewReduced());
        // The page's own rule for reduced motion takes the cards' animation away, so none is left running.
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("matchMedia('(prefers-reduced-motion: reduce)').matches")).toBool(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, cards).toInt(), 0, patience);
        QTRY_VERIFY_WITH_TIMEOUT(timeline.timeline().tracks.isEmpty(), patience);
        QCOMPARE(timeline.status(), QStringLiteral("No motion when reduced."));
        // The inspector says so, and the switch stays, on, to turn it off with.
        QTRY_VERIFY_WITH_TIMEOUT(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorEmpty"))
                                     && inspector.findChild<QLabel *>(QStringLiteral("motionInspectorEmpty"))->text() == QLatin1String("No motion when reduced."),
                                 patience);
        button = inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorPreviewReduced"));
        QVERIFY(button && button->isChecked());
        button->click();
        QVERIFY(!timeline.previewReduced());
        QTRY_VERIFY_WITH_TIMEOUT(!inPage(session, hosted.frame, QStringLiteral("matchMedia('(prefers-reduced-motion: reduce)').matches")).toBool(), patience);
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, cards).toInt(), 3, patience);
        QTRY_VERIFY_WITH_TIMEOUT(!timeline.timeline().tracks.isEmpty(), patience);

        // Closing the timeline lets the page be itself again, even from a preview.
        timeline.setPreviewReduced(true);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, QStringLiteral("matchMedia('(prefers-reduced-motion: reduce)').matches")).toBool(), patience);
        timeline.close();
        QTRY_VERIFY_WITH_TIMEOUT(!inPage(session, hosted.frame, QStringLiteral("matchMedia('(prefers-reduced-motion: reduce)').matches")).toBool(), patience);
        QVERIFY(!timeline.previewReduced());
    }

    void theWarningShowsForMotionWithNoRuleForPeopleWhoAskedForLess()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_motion, QStringLiteral("/norule.html")));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        openAndSelect(session, timeline, hosted.frame, QStringLiteral("div#dot"));
        QVERIFY(!timeline.timeline().reducedRule);
        auto *warning = inspector.findChild<QLabel *>(QStringLiteral("motionInspectorReducedWarning"));
        QVERIFY(warning);
        QCOMPARE(warning->text(), QStringLiteral("This motion plays for people who asked for less motion."));
        // Ask… goes to the agent through the project's window, and only for the user's own sites: with no window it says so.
        QSignalSpy notices(&timeline, &MotionTimeline::notice);
        inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorAskReduced"))->click();
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains(QStringLiteral("Open the project's window to ask.")));
        // A loop is a loop: drawn once, with its mark.
        QVERIFY(timeline.selectedTrack()->loops);
    }

    void aPageWithARuleForLessMotionHasNoWarning()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_motion, QStringLiteral("/cards.html")));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        openAndSelect(session, timeline, hosted.frame, QStringLiteral("section .bean-card × 3"));
        QVERIFY(timeline.timeline().reducedRule);
        QVERIFY(!inspector.findChild<QLabel *>(QStringLiteral("motionInspectorReducedWarning")));
    }

    void changingWhatStartsARowShowsWhereItCanAndIsAnEditForTheAgent()
    {
        NEEDS_CHROMIUM;
        EditorSession session;
        Hosted hosted(session, page(m_motion, QStringLiteral("/cards.html")));
        MotionTimeline timeline(session, hosted.canvas);
        MotionInspector inspector(timeline);
        openAndSelect(session, timeline, hosted.frame, QStringLiteral("section .bean-card × 3"));
        LiveFrames *frames = LiveFrames::of(session);
        const QString onView = QStringLiteral("document.getAnimations().filter(a => a.animationName === 'nl-cascade').every(a => a.timeline instanceof ViewTimeline)");
        const QString onClock = QStringLiteral("document.getAnimations().filter(a => a.animationName === 'nl-cascade').every(a => a.timeline === document.timeline)");
        QVERIFY(inPage(session, hosted.frame, onClock).toBool());
        QVERIFY(inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:load"))->isChecked());

        // On scroll: the cards' animations run on view timelines now, and each is an edit for the agent.
        inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:scroll"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(3), patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, onView).toBool(), patience);
        const LiveEdit edit = frames->snapshot(hosted.frame).edits.front();
        QCOMPARE(edit.property, QStringLiteral("motion-trigger"));
        QCOMPARE(edit.before, QStringLiteral("load"));
        QCOMPARE(edit.after, QStringLiteral("scroll"));
        QCOMPARE(edit.element["animation"].toString(), QStringLiteral("nl-cascade"));
        const QString words = AgentWork::describe(frames->snapshot(hosted.frame).edits);
        QVERIFY2(words.contains(QStringLiteral("to start as it scrolls into view (it starts when the page loads now)")), qPrintable(words));
        // The row is the same row, now driven by scrolling, and the control follows.
        QTRY_VERIFY_WITH_TIMEOUT(timeline.selectedTrack() && timeline.selectedTrack()->isScroll(), patience);
        QTRY_VERIFY_WITH_TIMEOUT(inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:scroll")) && inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:scroll"))->isChecked(), patience);

        // Undo puts them back on the clock, held.
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(0), patience);
        QTRY_VERIFY_WITH_TIMEOUT(inPage(session, hosted.frame, onClock).toBool(), patience);
        QCOMPARE(inPage(session, hosted.frame, QStringLiteral("document.getAnimations().filter(a => a.animationName === 'nl-cascade').every(a => a.playState === 'paused')")).toBool(), true);

        // On hover, and on click: nothing to show on the page, so only the edit.
        QTRY_VERIFY_WITH_TIMEOUT(timeline.selectedTrack() && !timeline.selectedTrack()->isScroll(), patience);
        inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorStarts:hover"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(3), patience);
        QVERIFY(inPage(session, hosted.frame, onClock).toBool());
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().after, QStringLiteral("hover"));
        QVERIFY(AgentWork::describe(frames->snapshot(hosted.frame).edits).contains(QStringLiteral("when the pointer is over it")));
    }
};

QTEST_MAIN(ReducedMotionTests)
#include "ReducedMotionTests.moc"
