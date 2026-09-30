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
#include "UI/MotionTrackView.h"
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

// Groups (docs/MOTION.md, section 5): picked elements are numbered in pick order, a group row opens into one row per element, each order
// writes the indices it should, an extra delay moves one element's start and no other's, and a group change is one undo step. Headless
// Chromium on a throwaway profile; skips without it.
namespace {
constexpr int patience = 30'000;

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

class MotionGroupTests : public QObject {
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

    static double index(EditorSession &session, const QUuid &frame, const QString &selector)
    {
        return inPage(session, frame, QStringLiteral("parseInt(getComputedStyle(document.querySelector('%1')).getPropertyValue('--i'))").arg(selector)).toDouble(-99);
    }

    // The three cards' indices, in DOM order (guji, huila, nyeri).
    static QList<int> indices(EditorSession &session, const QUuid &frame)
    {
        return {int(index(session, frame, QStringLiteral("#guji"))), int(index(session, frame, QStringLiteral("#huila"))), int(index(session, frame, QStringLiteral("#nyeri")))};
    }

    static void pick(EditorSession &session, const QUuid &frame, const QStringList &selectors)
    {
        bool done = false;
        LiveFrames::of(session)->run(frame, [selectors](LiveSession &live) { return live.selectElements(selectors); }, [&done](const QString &) { done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, patience);
        QTRY_VERIFY2_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(frame).selection.size() == selectors.size(),
                                  qPrintable(QString::fromUtf8(QJsonDocument(LiveFrames::of(session)->snapshot(frame).selection).toJson(QJsonDocument::Compact)).left(300)), patience);
    }

private slots:
    // A throwaway home for everything: the user's registry, Chromium profile, git identity and agent socket are never touched.
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

    void picksAreNumberedInThePickOrder()
    {
        NEEDS_CHROMIUM;
        const auto served = site();
        QVERIFY(served);
        EditorSession session;
        Hosted hosted(session, page(*served));
        MotionTimeline timeline(session, hosted.canvas);
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        pick(session, hosted.frame, {QStringLiteral("#huila")});
        QTRY_VERIFY_WITH_TIMEOUT(hosted.canvas.editPageSelectionRect().has_value(), patience);
        // One pick needs no number.
        QVERIFY(hosted.canvas.editPageBadges().isEmpty());
        pick(session, hosted.frame, {QStringLiteral("#huila"), QStringLiteral("#guji"), QStringLiteral("#nyeri")});
        QTRY_COMPARE_WITH_TIMEOUT(hosted.canvas.editPageBadges().size(), qsizetype(3), patience);
        const QList<EditorCanvas::PickBadge> badges = hosted.canvas.editPageBadges();
        QCOMPARE(badges[0].number, 1);
        QCOMPARE(badges[2].number, 3);
        // Badge 1 is on the element picked first: huila, the middle card, so it is right of guji's.
        QVERIFY(badges[0].rect.left() > badges[1].rect.left());
        QVERIFY(badges[2].rect.left() > badges[0].rect.left());
    }

    void aGroupRowOpensIntoOneRowPerElementAndEachCanBePicked()
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
        auto *tracks = timeline.findChild<MotionTrackView *>(QStringLiteral("motionTracks"));
        tracks->resize(900, tracks->contentHeight());
        QVERIFY(!timeline.isExpanded(row));
        QVERIFY(!tracks->childRect(row, 0).isValid());
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, tracks->expanderRect(row).center());
        QVERIFY(timeline.isExpanded(row));
        tracks->resize(900, tracks->contentHeight());
        for (int bar = 0; bar < 3; ++bar)
            QVERIFY(tracks->childRect(row, bar).isValid());
        // The group's own name, and one element picked alone.
        QCOMPARE(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorName"))->text(), QStringLiteral("Group · 3 bean cards"));
        const Motion::Track *track = timeline.timeline().find(row);
        int huila = -1;
        for (int i = 0; i < track->bars.size(); ++i)
            if (track->bars[i].selector == QLatin1String("#huila"))
                huila = i;
        QVERIFY(huila >= 0);
        QCOMPARE(track->bars[huila].label, QStringLiteral("article#huila"));
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(40, tracks->childRect(row, huila).center().y()));
        QCOMPARE(timeline.selectedBar(), huila);
        // The page reports a pick of several one element at a time: the group's first element alone is a selection of one
        // too, on the way to three. So this waits for huila alone, not for any one element.
        const auto selected = [&] {
            QStringList out;
            for (const QJsonValue &each : LiveFrames::of(session)->snapshot(hosted.frame).selection)
                out << each.toObject()["selector"].toString();
            return out;
        };
        QTRY_COMPARE_WITH_TIMEOUT(selected(), QStringList{QStringLiteral("#huila")}, patience);
        QTRY_COMPARE_WITH_TIMEOUT(inspector.findChild<QLabel *>(QStringLiteral("motionInspectorName")) ? inspector.findChild<QLabel *>(QStringLiteral("motionInspectorName"))->text() : QString(), QStringLiteral("article#huila"), patience);
        // And the group row picks them all again.
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, QPoint(40, tracks->rowRect(row).center().y()));
        QCOMPARE(timeline.selectedBar(), -1);
        QTRY_COMPARE_WITH_TIMEOUT(LiveFrames::of(session)->snapshot(hosted.frame).selection.size(), qsizetype(3), patience);
        // Closing it takes the element rows away.
        QTest::mouseClick(tracks, Qt::LeftButton, Qt::NoModifier, tracks->expanderRect(row).center());
        QVERIFY(!timeline.isExpanded(row));
    }

    void eachOrderWritesTheIndicesItShould()
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
        QCOMPARE(indices(session, hosted.frame), (QList<int>{1, 2, 0}));
        // Picked huila, guji, nyeri: that order is 0, 1, 2 in the order they were picked.
        pick(session, hosted.frame, {QStringLiteral("#huila"), QStringLiteral("#guji"), QStringLiteral("#nyeri")});
        QVERIFY(timeline.setOrder(Motion::Order::picked).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(indices(session, hosted.frame), (QList<int>{1, 0, 2}), patience);
        // From their boxes: left to right is DOM order here; centre out starts in the middle, ties going left.
        QVERIFY(timeline.setOrder(Motion::Order::leftToRight).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(indices(session, hosted.frame), (QList<int>{0, 1, 2}), patience);
        QVERIFY(timeline.setOrder(Motion::Order::centreOut).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(indices(session, hosted.frame), (QList<int>{1, 0, 2}), patience);
        // Shuffle is a permutation, the same for the same group and press.
        QVERIFY(timeline.setOrder(Motion::Order::shuffle).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            QList<int> got = indices(session, hosted.frame);
            std::sort(got.begin(), got.end());
            return got == QList<int>{0, 1, 2};
        })(), patience);
        const QList<int> first = indices(session, hosted.frame);
        // The rows show it: each element's start is its index times the stagger.
        QTRY_VERIFY_WITH_TIMEOUT(timeline.selectedTrack() && timeline.selectedTrack()->bars.size() == 3, patience);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.selectedTrack()->bars[0].index, first[0], patience);
        QVERIFY(timeline.setOrder(Motion::Order::leftToRight).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(indices(session, hosted.frame), (QList<int>{0, 1, 2}), patience);
    }

    void aGroupChangeIsOneUndoStepAndSaveWritesEachIndex()
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
        QString row;
        openOnCards(session, timeline, hosted.frame, row);
        LiveFrames *frames = LiveFrames::of(session);
        QVERIFY(timeline.setOrder(Motion::Order::leftToRight).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(3), patience);
        QCOMPARE(indices(session, hosted.frame), (QList<int>{0, 1, 2}));
        // One Ctrl+Z takes back all three.
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(0), patience);
        QTRY_COMPARE_WITH_TIMEOUT(indices(session, hosted.frame), (QList<int>{1, 2, 0}), patience);
        QVERIFY(!session.isModified());
        // Again, and Save: each index is written in its own rule, and nothing else changes.
        const QByteArray before = read(served->folder + "/cards.css");
        QVERIFY(timeline.setOrder(Motion::Order::leftToRight).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(3), patience);
        QString request;
        QVERIFY2(bridge.liveWriteBack(&request, served->folder).isEmpty(), "written");
        QVERIFY2(request.isEmpty(), qPrintable(request));
        QByteArray expected = before;
        expected.replace("#guji  { --i: 1; }", "#guji  { --i: 0; }");
        expected.replace("#huila { --i: 2; }", "#huila { --i: 1; }");
        expected.replace("#nyeri { --i: 0; }", "#nyeri { --i: 2; }");
        QCOMPARE(read(served->folder + "/cards.css"), expected);
    }

    void anExtraDelayMovesOneElementsStartAndGivingItBackRestoresIt()
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
        LiveFrames *frames = LiveFrames::of(session);
        const auto starts = [&] {
            QMap<QString, double> out;
            if (const Motion::Track *track = timeline.timeline().find(row))
                for (const Motion::Bar &bar : track->bars)
                    out[bar.selector] = bar.start;
            return out;
        };
        const QMap<QString, double> before = starts();
        QCOMPARE(before.size(), 3);
        QCOMPARE(before["#huila"], 280.0);
        int huila = -1;
        const Motion::Track *track = timeline.timeline().find(row);
        for (int i = 0; i < track->bars.size(); ++i)
            if (track->bars[i].selector == QLatin1String("#huila"))
                huila = i;
        timeline.selectBar(row, huila);
        QTRY_VERIFY_WITH_TIMEOUT(inspector.findChild<NumberField *>(QStringLiteral("motionInspectorExtra")) != nullptr, patience);
        auto *extra = inspector.findChild<NumberField *>(QStringLiteral("motionInspectorExtra"));
        extra->field->setText(QStringLiteral("300"));
        extra->commit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        const LiveEdit edit = frames->snapshot(hosted.frame).edits.front();
        QCOMPARE(edit.selector, QStringLiteral("#huila"));
        QCOMPARE(edit.property, QStringLiteral("--delay-extra"));
        QCOMPARE(edit.after, QStringLiteral("300ms"));
        // Only that element's start moved.
        // Within qFuzzyCompare, as QCOMPARE takes doubles: the page's times carry float noise (580.00000000000011 with
        // Chromium 153 on arm64), and a QTRY loop's own test is ==, so an exact wait ran out and only its last QCOMPARE passed.
        QTRY_VERIFY2_WITH_TIMEOUT(qFuzzyCompare(starts()["#huila"], 580.0), qPrintable(QString::number(starts()["#huila"], 'g', 17)), patience);
        QCOMPARE(starts()["#guji"], before["#guji"]);
        QCOMPARE(starts()["#nyeri"], before["#nyeri"]);
        // Written into its own rule.
        const WriteBack::Plan plan = WriteBack::plan(served->folder, frames->snapshot(hosted.frame).edits);
        QVERIFY(plan.unresolved.empty());
        QCOMPARE(plan.changes.size(), size_t(1));
        QVERIFY(QString::fromUtf8(*plan.changes.front().after).contains(QStringLiteral("#huila { --i: 2; --delay-extra: 300ms; }")));
        // Use the group's timing: it comes off, and the start is the group's again.
        auto *back = inspector.findChild<QToolButton *>(QStringLiteral("motionInspectorUseGroupTiming"));
        QVERIFY(back && back->isEnabled());
        back->click();
        QTRY_VERIFY2_WITH_TIMEOUT(qFuzzyCompare(starts()["#huila"], 280.0), qPrintable(QString::number(starts()["#huila"], 'g', 17)), patience);
        QCOMPARE(frames->snapshot(hosted.frame).edits.size(), size_t(1));
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().after, QString());
        // Saved, that is nothing to write, so the file is as it was.
        QString request;
        QVERIFY(bridge.liveWriteBack(&request, served->folder).isEmpty());
        QVERIFY(request.isEmpty());
        QVERIFY(WriteBack::git(served->folder, {"status", "--porcelain", "--untracked-files=no"}).trimmed().isEmpty());
    }

    void anEffectReplacesTheFirstKeyframeAndUndoesAndIsWritten()
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
        LiveFrames *frames = LiveFrames::of(session);
        // The fixture's first keyframe also turns (`rotate: -3deg`), so it is no preset: picking Rise would drop the rotation.
        QCOMPARE(timeline.effectOf(), QStringLiteral("custom"));
        const QString scale = QStringLiteral("String(document.getAnimations().find(a => a.animationName === 'nl-cascade').effect.getKeyframes()[0].scale)");
        timeline.setEffect(QStringLiteral("grow"));
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        QTRY_COMPARE_WITH_TIMEOUT(inPage(session, hosted.frame, scale).toString(), QStringLiteral("0.85"), patience);
        QCOMPARE(frames->snapshot(hosted.frame).edits.front().property, QStringLiteral("from *"));
        QTRY_COMPARE_WITH_TIMEOUT(timeline.effectOf(), QStringLiteral("grow"), patience);
        hosted.canvas.undoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(0), patience);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.effectOf(), QStringLiteral("custom"), patience);
        // Written: the frame's declarations are the effect's, and the rest of the file stays.
        hosted.canvas.redoPageEdit();
        QTRY_COMPARE_WITH_TIMEOUT(frames->snapshot(hosted.frame).edits.size(), size_t(1), patience);
        const QByteArray before = read(served->folder + "/cards.css");
        QString request;
        QVERIFY(bridge.liveWriteBack(&request, served->folder).isEmpty());
        QVERIFY(request.isEmpty());
        QByteArray expected = before;
        expected.replace("from { opacity: 0; translate: 0 44px; rotate: -3deg; }", "from { opacity: 0; scale: 0.85; }");
        QCOMPARE(read(served->folder + "/cards.css"), expected);
    }
};

QTEST_MAIN(MotionGroupTests)
#include "MotionGroupTests.moc"
