#include "Live/BrowserPool.h"
#include "Live/LiveSession.h"
#include "Live/Motion.h"
#include "Live/StaticServer.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

// The page side of the timeline (docs/MOTION.md, sections 1 and 2): the list of the page's motion, holding, seeking and
// releasing, on the motion fixture in headless Chromium and a throwaway profile. Skips without Chromium.
class MotionTimelineTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    StaticServer m_server;
    BrowserPool *m_pool = nullptr;
    LiveSession *m_live = nullptr;
    QUuid m_frame;

    template <typename F>
    void onPool(F work)
    {
        QMetaObject::invokeMethod(m_live, work, Qt::BlockingQueuedConnection);
    }

    // For a call that makes the browser fetch from the fixture's server (CSS.enable reads the style sheets again): the
    // server lives on this thread, so this thread must keep running events while the pool's thread waits.
    template <typename F>
    void onPoolAsync(F work)
    {
        bool done = false;
        QMetaObject::invokeMethod(m_live, [&done, work] {
            work();
            done = true;
        }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(done, 40'000);
    }

    QJsonValue eval(const QString &expression)
    {
        QJsonValue value;
        onPool([&] { value = m_live->evaluate(expression); });
        return value;
    }

    double number(const QString &expression) { return eval(expression).toDouble(-999); }
    double opacity(const QString &selector)
    {
        return number(QStringLiteral("parseFloat(getComputedStyle(document.querySelector('%1')).opacity)").arg(selector));
    }

    QJsonObject list()
    {
        return eval(QStringLiteral("window.__oma.motion.list()")).toObject();
    }

    // The entries of a kind, from the page's list.
    QList<QJsonObject> entries(const QJsonObject &all, const QString &kind, const QString &timeline = QString())
    {
        QList<QJsonObject> found;
        for (const QJsonValue &value : all["animations"].toArray()) {
            const QJsonObject each = value.toObject();
            if (each["kind"].toString() == kind && (timeline.isEmpty() || each["timeline"].toString() == timeline) && !each["potential"].toBool())
                found << each;
        }
        return found;
    }

    bool waitRunning()
    {
        for (int i = 0; i < 1200; ++i) {
            QTest::qWait(50);
            LiveSession::State state = LiveSession::State::off;
            onPool([&] { state = m_live->state(); });
            if (state == LiveSession::State::running)
                return true;
            if (state == LiveSession::State::failed)
                return false;
        }
        return false;
    }

    void openPage(const QString &path)
    {
        QSignalSpy opened(m_pool, &BrowserPool::opened);
        m_pool->open(m_frame);
        QVERIFY(opened.wait(60'000));
        QEventLoop loop;
        QUrl url = m_server.url();
        url.setPath(path);
        m_pool->call(m_frame, QStringLiteral("Page.navigate"), {{"url", url.toString()}}, [&](const QJsonObject &, const QString &) {
            QMetaObject::invokeMethod(&loop, &QEventLoop::quit, Qt::QueuedConnection);
        });
        QTimer::singleShot(15'000, &loop, &QEventLoop::quit);
        loop.exec();
        LiveSession::Target target;
        target.frame = m_frame;
        target.pool = m_pool;
        QString failure;
        onPool([&] { failure = m_live->start(target); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(waitRunning());
        // The overlay comes in once the page has loaded.
        for (int i = 0; i < 100 && !eval(QStringLiteral("!!(window.__oma && window.__oma.motion)")).toBool(); ++i)
            QTest::qWait(50);
        QVERIFY(eval(QStringLiteral("!!(window.__oma && window.__oma.motion)")).toBool());
    }

    QJsonObject motionNow()
    {
        QJsonObject now;
        onPool([&] { now = m_live->motion(); });
        return now;
    }

    bool heldNow()
    {
        bool held = false;
        onPool([&] { held = m_live->motionHeld(); });
        return held;
    }

    QString hold()
    {
        QString failure;
        onPool([&] { failure = m_live->motionHold(); });
        return failure;
    }

private slots:
    void initTestCase()
    {
        if (Browser::executable().isEmpty())
            QSKIP("Chromium isn't installed.");
        QVERIFY(m_directory.isValid());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qRegisterMetaType<BrowserPool::CloseReason>();
        QVERIFY(m_server.serve(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion")).isEmpty());
        BrowserPool::Options options;
        options.profile = m_directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        m_pool = new BrowserPool(options);
    }

    void cleanupTestCase()
    {
        delete m_live;
        m_live = nullptr;
        delete m_pool;
        m_pool = nullptr;
    }

    void init()
    {
        if (!m_pool)
            QSKIP("Chromium isn't installed.");
        m_frame = QUuid::createUuid();
        m_live = new LiveSession;
        m_live->moveToThread(m_pool->poolThread());
    }

    void cleanup()
    {
        if (!m_live)
            return;
        onPool([&] { m_live->stop(); });
        m_pool->close(m_frame);
        QMetaObject::invokeMethod(m_live, &QObject::deleteLater, Qt::QueuedConnection);
        m_live = nullptr;
    }

    void theListFindsEachKindWithItsTimingAndKeyframes()
    {
        openPage(QStringLiteral("/index.html"));
        const QJsonObject all = list();
        QVERIFY(!all["held"].toBool());

        // CSS keyframes on five sibling words, each a step later.
        const QList<QJsonObject> rises = [&] {
            QList<QJsonObject> found;
            for (const QJsonObject &each : entries(all, QStringLiteral("css-animation"), QStringLiteral("document")))
                if (each["name"].toString() == QLatin1String("nl-rise"))
                    found << each;
            return found;
        }();
        QCOMPARE(rises.size(), 5);
        QVERIFY(qAbs(rises[0]["duration"].toDouble() - 480) < 0.5);
        QVERIFY(qAbs(rises[3]["delay"].toDouble() - 180) < 0.5);
        QCOMPARE(rises[0]["easing"].toString(), QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        QCOMPARE(rises[0]["trigger"].toString(), QStringLiteral("load"));
        QCOMPARE(rises[0]["parent"].toObject()["selector"].toString(), QStringLiteral("#headline"));
        QCOMPARE(rises[0]["cls"].toString(), QStringLiteral("word"));
        const QJsonArray keyframes = rises[0]["keyframes"].toArray();
        QCOMPARE(keyframes.size(), 2);
        QCOMPARE(keyframes[0].toObject()["props"].toObject()["opacity"].toString(), QStringLiteral("0"));
        QCOMPARE(keyframes[0].toObject()["props"].toObject()["translate"].toString(), QStringLiteral("0px 24px"));
        QCOMPARE(keyframes[1].toObject()["props"].toObject()["opacity"].toString(), QStringLiteral("1"));
        QCOMPARE(rises[0]["properties"].toArray().size(), 2);

        // A script's Web Animation: listed, with its own timing, as motion from a script.
        const QList<QJsonObject> scripts = entries(all, QStringLiteral("script"));
        QCOMPARE(scripts.size(), 1);
        QCOMPARE(scripts[0]["name"].toString(), QStringLiteral("badge-in"));
        QCOMPARE(scripts[0]["delay"].toDouble(), 100.0);
        QCOMPARE(scripts[0]["duration"].toDouble(), 600.0);
        QCOMPARE(scripts[0]["easing"].toString(), QStringLiteral("ease-out"));
        QCOMPARE(scripts[0]["trigger"].toString(), QStringLiteral("script"));

        // Scroll-driven motion: three cards, each with the scroll positions it runs between, from the page's own timeline.
        const QList<QJsonObject> views = entries(all, QStringLiteral("css-animation"), QStringLiteral("view"));
        QCOMPARE(views.size(), 3);
        const QJsonObject range = views[0]["range"].toObject();
        QVERIFY(range["to"].toDouble() > range["from"].toDouble());
        const double start = number(QStringLiteral("(() => { const a = document.getAnimations().find(x => x.timeline instanceof ViewTimeline); return a.timeline.startOffset ? a.timeline.startOffset.value : -1; })()"));
        if (start >= 0)
            QVERIFY2(qAbs(range["from"].toDouble() - start) < 1.5, "the range starts where the page's own view timeline does");
        QCOMPARE(views[0]["trigger"].toString(), QStringLiteral("scroll"));
        QVERIFY(all["scroll"].toObject()["max"].toDouble() > range["to"].toDouble());

        // The hover transition isn't running, so it's listed as one the state would start.
        bool found = false;
        for (const QJsonValue &value : all["animations"].toArray()) {
            const QJsonObject each = value.toObject();
            if (each["potential"].toBool() && each["selector"].toString() == QLatin1String("#primary")) {
                found = true;
                QCOMPARE(each["trigger"].toString(), QStringLiteral("hover"));
                QCOMPARE(each["duration"].toDouble(), 200.0);
                QCOMPARE(each["properties"].toArray().size(), 2);
                QCOMPARE(each["kind"].toString(), QStringLiteral("css-transition"));
            }
        }
        QVERIFY(found);

        // And the rows the model makes from it are what the timeline shows.
        const Motion::Timeline timeline = Motion::parse(all);
        QStringList labels;
        for (const Motion::Track &track : timeline.tracks)
            labels << track.label;
        QVERIFY2(labels.contains(QStringLiteral("h1 .word × 5")), qPrintable(labels.join(", ")));
        QVERIFY2(labels.contains(QStringLiteral("section .card × 3")), qPrintable(labels.join(", ")));
        QVERIFY2(labels.contains(QStringLiteral("p#lede")), qPrintable(labels.join(", ")));
        QVERIFY2(labels.contains(QStringLiteral("span#badge")), qPrintable(labels.join(", ")));
        QVERIFY2(labels.contains(QStringLiteral("a#primary")), qPrintable(labels.join(", ")));
    }

    void holdingPausesALoadAnimationAndOneStartedLater()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        QVERIFY(heldNow());
        QCOMPARE(eval(QStringLiteral("document.getAnimations().filter(a => !(a.timeline instanceof ScrollTimeline)).every(a => a.playState === 'paused')")).toBool(), true);
        // Something the page starts while held is held too, and joins at the playhead.
        eval(QStringLiteral("window.__oma.motion.seek(300); document.getElementById('lede').animate([{ opacity: 0.2 }, { opacity: 0.9 }], { duration: 1000, id: 'later' }); true"));
        QTRY_VERIFY_WITH_TIMEOUT(eval(QStringLiteral("(document.getAnimations().find(a => a.id === 'later') || {}).playState")).toString() == QLatin1String("paused"), 5000);
        // It's on the timeline as its own row at the playhead: 300 ms in.
        QTRY_VERIFY_WITH_TIMEOUT(number(QStringLiteral("(document.getAnimations().find(a => a.id === 'later') || {currentTime: -1}).currentTime")) == 300.0, 5000);
        const QJsonObject after = list();
        bool later = false;
        for (const QJsonValue &value : after["animations"].toArray())
            later = later || value.toObject()["name"].toString() == QLatin1String("later");
        QVERIFY(later);
        // Held, the page's clock doesn't move: the same opacity a moment apart.
        const double first = opacity(".word:nth-child(1)");
        QTest::qWait(300);
        QCOMPARE(opacity(".word:nth-child(1)"), first);
    }

    void seekingSetsTheOpacityTheKeyframesGiveAtThatTime()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        QString failure;
        onPool([&] { failure = m_live->motionSeek(240); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // The lede fades in over 400 ms, linearly, from its own start: 240 ms is 0.6.
        QVERIFY2(qAbs(opacity("#lede") - 0.6) < 0.01, qPrintable(QString::number(opacity("#lede"))));
        // Words step in 60 ms apart, each on the soft-out curve: one step later is less far along, the last has not begun.
        const double first = opacity("#headline > span:nth-of-type(1)");
        const double second = opacity("#headline > span:nth-of-type(2)");
        const double last = opacity("#headline > span:nth-of-type(5)");
        QVERIFY(first > second);
        QVERIFY(second > last);
        QCOMPARE(last, 0.0);
        // The curve is cubic-bezier(0.16, 1, 0.3, 1): halfway in time is well past halfway in value.
        QVERIFY2(first > 0.85, qPrintable(QString::number(first)));
        // Before the start, and after the end.
        onPool([&] { m_live->motionSeek(0); });
        QCOMPARE(opacity("#lede"), 0.0);
        QCOMPARE(opacity("#headline > span:nth-of-type(1)"), 0.0);
        onPool([&] { m_live->motionSeek(5000); });
        QCOMPARE(opacity("#lede"), 1.0);
        QCOMPARE(opacity("#headline > span:nth-of-type(5)"), 1.0);
        // The script's animation is on the timeline too: its 100 ms delay, then 600 ms.
        onPool([&] { m_live->motionSeek(400); });
        const double badge = opacity("#badge");
        QVERIFY(badge > 0.2 && badge < 1.0);
    }

    void seekingAScrollRowScrollsThePage()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        const QList<QJsonObject> views = entries(list(), QStringLiteral("css-animation"), QStringLiteral("view"));
        QCOMPARE(views.size(), 3);
        const QJsonObject range = views[0]["range"].toObject();
        const double from = range["from"].toDouble();
        const double to = range["to"].toDouble();
        QCOMPARE(number(QStringLiteral("scrollY")), 0.0);
        QString failure;
        onPool([&] { failure = m_live->motionSeekScroll(from); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // Chromium keeps the scroll position on whole device pixels.
        QVERIFY2(qAbs(number(QStringLiteral("scrollY")) - from) < 1.0, qPrintable(QString::number(number(QStringLiteral("scrollY")))));
        // The scroll drives the animation, which was never paused: at the start of its range the card is not there yet,
        // at the end of it the card is.
        // The page's frame updates a scroll-driven animation, so the value comes a moment after the scroll.
        QTRY_VERIFY2_WITH_TIMEOUT(opacity("#guji") < 0.02, qPrintable(QString::number(opacity("#guji"))), 5000);
        onPool([&] { m_live->motionSeekScroll(to); });
        QTRY_VERIFY2_WITH_TIMEOUT(opacity("#guji") > 0.98, qPrintable(QString::number(opacity("#guji"))), 5000);
        onPool([&] { m_live->motionSeekScroll((from + to) / 2); });
        QTRY_VERIFY2_WITH_TIMEOUT(opacity("#guji") > 0.3 && opacity("#guji") < 0.7, qPrintable(QString::number(opacity("#guji"))), 5000);
    }

    void releasingPlaysOnAndAnEndedAnimationStaysEnded()
    {
        openPage(QStringLiteral("/index.html"));
        // Let the load animations end first: an ended animation must not start again when the timeline lets go.
        QTRY_VERIFY_WITH_TIMEOUT(eval(QStringLiteral("document.getAnimations().filter(a => a.constructor.name === 'CSSAnimation' && !(a.timeline instanceof ScrollTimeline)).every(a => a.playState === 'finished')")).toBool(), 10'000);
        QVERIFY2(hold().isEmpty(), "held");
        onPool([&] { m_live->motionSeek(100); });
        QVERIFY(opacity("#lede") < 0.5);
        QString failure;
        onPool([&] { failure = m_live->motionRelease(); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QVERIFY(!heldNow());
        QCOMPARE(eval(QStringLiteral("document.getAnimations().some(a => a.playState === 'paused')")).toBool(), false);
        // It had ended, so it is ended again and shows its end.
        QTRY_COMPARE_WITH_TIMEOUT(opacity("#lede"), 1.0, 3000);
        QCOMPARE(opacity("#headline > span:nth-of-type(5)"), 1.0);
    }

    void anAnimationThePageItselfPausedStaysPausedWhenTheHoldEnds()
    {
        openPage(QStringLiteral("/index.html"));
        // The page's own choice: one animation paused before anything is held, one running.
        eval(QStringLiteral("window.__mine = document.getElementById('lede').animate([{ marginLeft: '0px' }, { marginLeft: '40px' }], { duration: 600000, id: 'mine' });"
                            "window.__mine.pause();"
                            "window.__yours = document.getElementById('primary').animate([{ marginLeft: '0px' }, { marginLeft: '40px' }], { duration: 600000, id: 'yours' }); true"));
        QVERIFY2(hold().isEmpty(), "held");
        QCOMPARE(eval(QStringLiteral("window.__yours.playState")).toString(), QStringLiteral("paused"));
        QString failure;
        onPool([&] { failure = m_live->motionRelease(); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // The timeline lets go of what it paused, and only that.
        QCOMPARE(eval(QStringLiteral("window.__yours.playState")).toString(), QStringLiteral("running"));
        QCOMPARE(eval(QStringLiteral("window.__mine.playState")).toString(), QStringLiteral("paused"));
    }

    void anOldAnimationKeptByItsFillDoesNotMoveTheZeroOfTheRuler()
    {
        openPage(QStringLiteral("/index.html"));
        // The load animations end and are kept by their fill; a new one starts a moment later.
        QTRY_VERIFY_WITH_TIMEOUT(eval(QStringLiteral("document.getAnimations().filter(a => a.constructor.name === 'CSSAnimation' && !(a.timeline instanceof ScrollTimeline)).every(a => a.playState === 'finished')")).toBool(), 10'000);
        QTest::qWait(700);
        eval(QStringLiteral("document.getElementById('lede').animate([{ marginLeft: '0px' }, { marginLeft: '40px' }], { duration: 600000, id: 'young' }); true"));
        QVERIFY2(hold().isEmpty(), "held");
        double offset = -1;
        for (const QJsonObject &each : entries(list(), QStringLiteral("script")))
            if (each["name"].toString() == QLatin1String("young"))
                offset = each["offset"].toDouble(-1);
        // Its own start is the zero, not the load animation's, which was more than half a second earlier.
        QVERIFY2(offset >= 0 && offset < 50, qPrintable(QString::number(offset)));
    }

    void releasingPlaysAnUnfinishedAnimationOnFromWhereItWas()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        // A long animation the page starts while the timeline holds it joins at the playhead.
        onPool([&] { m_live->motionSeek(0); });
        eval(QStringLiteral("document.getElementById('lede').animate([{ opacity: 0 }, { opacity: 1 }], { duration: 3000, fill: 'both', id: 'slow' }); true"));
        QTRY_VERIFY_WITH_TIMEOUT(eval(QStringLiteral("(document.getAnimations().find(a => a.id === 'slow') || {}).playState")).toString() == QLatin1String("paused"), 5000);
        onPool([&] { m_live->motionSeek(1500); });
        QVERIFY2(qAbs(opacity("#lede") - 0.5) < 0.02, qPrintable(QString::number(opacity("#lede"))));
        onPool([&] { m_live->motionRelease(); });
        QTest::qWait(400);
        const double later = opacity("#lede");
        QVERIFY2(later > 0.55 && later < 0.9, qPrintable(QString::number(later)));
        QCOMPARE(eval(QStringLiteral("document.getAnimations().find(a => a.id === 'slow').playState")).toString(), QStringLiteral("running"));
    }

    void aHoverTransitionIsHeldInItsStateAndSeen()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        QCOMPARE(eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("none"));
        QString failure;
        onPoolAsync([&] { failure = m_live->motionForce(QStringLiteral("#primary"), QStringLiteral("hover")); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        // The transition the state started is now a running one on the list, held, and named for its state.
        QJsonObject transition;
        QTRY_VERIFY_WITH_TIMEOUT(([&] {
            for (const QJsonValue &value : list()["animations"].toArray())
                if (value.toObject()["kind"].toString() == QLatin1String("css-transition") && !value.toObject()["potential"].toBool()
                    && value.toObject()["state"].toString() == QLatin1String("paused")) {
                    transition = value.toObject();
                    return true;
                }
            return false;
        })(), 5000);
        QCOMPARE(transition["selector"].toString(), QStringLiteral("#primary"));
        QCOMPARE(transition["trigger"].toString(), QStringLiteral("hover"));
        onPool([&] { m_live->motionSeek(0); });
        const QString start = eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString();
        QVERIFY2(start == QLatin1String("none") || start == QLatin1String("matrix(1, 0, 0, 1, 0, 0)"), qPrintable(start));
        onPool([&] { m_live->motionSeek(200); });
        QCOMPARE(eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("matrix(1, 0, 0, 1, 0, -2)"));
        onPool([&] { m_live->motionSeek(100); });
        const QString middle = eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString();
        QVERIFY2(middle != QLatin1String("none") && middle != QLatin1String("matrix(1, 0, 0, 1, 0, -2)"), qPrintable(middle));

        // Letting the state go puts the page back at once, and the way back is not motion of the page.
        onPoolAsync([&] { failure = m_live->motionForce(QStringLiteral("#primary"), QString()); });
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
        QTRY_COMPARE_WITH_TIMEOUT(eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("none"), 3000);
        QTest::qWait(300);
        QCOMPARE(eval(QStringLiteral("document.getAnimations().filter(a => a.constructor.name === 'CSSTransition').length")).toInt(), 0);
        bool waiting = false;
        for (const QJsonValue &value : list()["animations"].toArray())
            waiting = waiting || (value.toObject()["potential"].toBool() && value.toObject()["selector"].toString() == QLatin1String("#primary"));
        QVERIFY2(waiting, "the state is listed as one it could start again");
    }

    void releasingLetsGoOfAForcedState()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        onPoolAsync([&] { m_live->motionForce(QStringLiteral("#primary"), QStringLiteral("hover")); });
        onPool([&] { m_live->motionSeek(200); });
        QVERIFY(eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString() != QLatin1String("none"));
        onPoolAsync([&] { m_live->motionRelease(); });
        QTRY_COMPARE_WITH_TIMEOUT(eval(QStringLiteral("getComputedStyle(document.querySelector('#primary')).transform")).toString(), QStringLiteral("none"), 3000);
    }

    void aPageThatLoadsAgainIsHeldAgain()
    {
        openPage(QStringLiteral("/index.html"));
        QVERIFY2(hold().isEmpty(), "held");
        QUrl url = m_server.url();
        url.setPath(QStringLiteral("/index.html"));
        eval(QStringLiteral("location.reload(); true"));
        QTRY_VERIFY_WITH_TIMEOUT(eval(QStringLiteral("!!(window.__oma && window.__oma.motion && window.__oma.motion.isHeld())")).toBool(), 20'000);
        QVERIFY(heldNow());
    }

    void gsapsGlobalTimelineIsHeldAndSeeked()
    {
        openPage(QStringLiteral("/gsap.html"));
        QVERIFY2(hold().isEmpty(), "held");
        QCOMPARE(eval(QStringLiteral("window.gsapCalls.includes('pause')")).toBool(), true);
        const QJsonObject gsap = motionNow()["gsap"].toObject();
        QCOMPARE(gsap["count"].toInt(), 2);
        QCOMPARE(gsap["start"].toDouble(), 100.0);
        QCOMPARE(gsap["end"].toDouble(), 1600.0);
        // The stand-in was at 0.3 s when it was held: the playhead starts there.
        QCOMPARE(motionNow()["time"].toDouble(), 300.0);
        onPool([&] { m_live->motionSeek(900); });
        QCOMPARE(number(QStringLiteral("window.gsap.globalTimeline.time()")), 0.9);
        onPool([&] { m_live->motionRelease(); });
        QCOMPARE(eval(QStringLiteral("window.gsapCalls.includes('resume')")).toBool(), true);
    }
};

QTEST_GUILESS_MAIN(MotionTimelineTests)
#include "MotionTimelineTests.moc"
