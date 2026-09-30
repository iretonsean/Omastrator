#include "Live/Motion.h"
#include "Live/MotionCode.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

// The timeline's rows, made from what the page reports (docs/MOTION.md, section 2), and the marked blocks the Code tab
// reads. No browser: the page's list is built here.
namespace {
QJsonObject who(const QString &tag, const QString &id, const QString &cls, const QString &selector)
{
    return {{"tag", tag}, {"id", id}, {"cls", cls}, {"selector", selector}};
}

QJsonObject animation(const QString &kind, const QString &name, const QJsonObject &element, const QJsonObject &parent, const QStringList &properties,
                      double delay, double duration, const QString &trigger = QStringLiteral("load"))
{
    QJsonObject each = element;
    each["kind"] = kind;
    each["name"] = name;
    each["timeline"] = QStringLiteral("document");
    each["parent"] = parent;
    each["properties"] = QJsonArray::fromStringList(properties);
    each["keyframes"] = QJsonArray{QJsonObject{{"offset", 0}, {"easing", "linear"}, {"props", QJsonObject{{"opacity", "0"}}}},
                                   QJsonObject{{"offset", 1}, {"easing", "linear"}, {"props", QJsonObject{{"opacity", "1"}}}}};
    each["easing"] = QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)");
    each["delay"] = delay;
    each["duration"] = duration;
    each["iterations"] = 1;
    each["offset"] = 0;
    each["trigger"] = trigger;
    return each;
}

QJsonObject words(int count)
{
    QJsonArray all;
    const QJsonObject h1 = who(QStringLiteral("h1"), QStringLiteral("headline"), {}, QStringLiteral("#headline"));
    for (int i = 0; i < count; ++i)
        all.append(animation(QStringLiteral("css-animation"), QStringLiteral("nl-rise"),
                             who(QStringLiteral("span"), {}, QStringLiteral("word"), QStringLiteral("#headline > span:nth-of-type(%1)").arg(i + 1)), h1,
                             {QStringLiteral("opacity"), QStringLiteral("translate")}, i * 60, 480));
    return {{"animations", all}, {"held", true}};
}
}

class MotionModelTests : public QObject {
    Q_OBJECT

private slots:
    void siblingsWithTheSameAnimationAreOneRow()
    {
        const Motion::Timeline timeline = Motion::parse(words(5));
        QCOMPARE(timeline.tracks.size(), 1);
        const Motion::Track &track = timeline.tracks.first();
        QCOMPARE(track.label, QStringLiteral("h1 .word × 5"));
        QCOMPARE(track.detail, QStringLiteral("opacity, translate"));
        QCOMPARE(track.bars.size(), 5);
        QCOMPARE(track.bars[3].start, 180.0);
        QCOMPARE(track.bars[3].length, 480.0);
        QCOMPARE(track.stagger, 60.0);
        QCOMPARE(track.duration, 480.0);
        QCOMPARE(track.keyframes.size(), 2);
        QCOMPARE(timeline.duration, 720.0);
        QCOMPARE(timeline.trigger(), QStringLiteral("On load"));
        QCOMPARE(track.selectors.size(), 5);
    }

    void oneElementIsNamedForItsTagAndIdOrClass()
    {
        QJsonArray all;
        const QJsonObject body = who(QStringLiteral("main"), {}, {}, QStringLiteral("main"));
        all.append(animation(QStringLiteral("css-animation"), QStringLiteral("nl-fade"), who(QStringLiteral("p"), QStringLiteral("lede"), QStringLiteral("lede"), QStringLiteral("#lede")), body,
                             {QStringLiteral("opacity")}, 0, 400));
        all.append(animation(QStringLiteral("css-animation"), QStringLiteral("nl-pop"), who(QStringLiteral("p"), {}, QStringLiteral("note"), QStringLiteral("main > p.note")), body,
                             {QStringLiteral("opacity")}, 0, 400));
        const Motion::Timeline timeline = Motion::parse({{"animations", all}});
        QCOMPARE(timeline.tracks.size(), 2);
        // An id names the element better than its class.
        QCOMPARE(timeline.tracks[0].label, QStringLiteral("p#lede"));
        QCOMPARE(timeline.tracks[1].label, QStringLiteral("p.note"));
    }

    void siblingsWithoutAClassReadTheirTagTimesTheirNumber()
    {
        QJsonArray all;
        const QJsonObject section = who(QStringLiteral("section"), {}, QStringLiteral("cards"), QStringLiteral("section"));
        for (int i = 0; i < 3; ++i)
            all.append(animation(QStringLiteral("css-animation"), QStringLiteral("nl-fade"), who(QStringLiteral("article"), {}, {}, QStringLiteral("article:nth-of-type(%1)").arg(i + 1)),
                                 section, {QStringLiteral("opacity")}, 0, 400));
        QCOMPARE(Motion::parse({{"animations", all}}).tracks.first().label, QStringLiteral("article × 3"));
    }

    void theSameNameOnDifferentParentsIsTwoRows()
    {
        QJsonArray all = words(2)["animations"].toArray();
        all.append(animation(QStringLiteral("css-animation"), QStringLiteral("nl-rise"), who(QStringLiteral("span"), {}, QStringLiteral("word"), QStringLiteral("#other > span")),
                             who(QStringLiteral("h2"), QStringLiteral("other"), {}, QStringLiteral("#other")), {QStringLiteral("opacity")}, 0, 480));
        const Motion::Timeline timeline = Motion::parse({{"animations", all}});
        QCOMPARE(timeline.tracks.size(), 2);
        QCOMPARE(timeline.tracks[0].bars.size(), 2);
        QCOMPARE(timeline.tracks[1].bars.size(), 1);
    }

    void aTransitionIsOneRowAndKeepsItsIdOnceAStateHoldsIt()
    {
        const QJsonObject link = who(QStringLiteral("a"), QStringLiteral("primary"), {}, QStringLiteral("#primary"));
        const QJsonObject parent = who(QStringLiteral("p"), {}, {}, QStringLiteral("p"));
        QJsonObject waiting = animation(QStringLiteral("css-transition"), QStringLiteral("transform, background-color"), link, parent,
                                        {QStringLiteral("transform"), QStringLiteral("background-color")}, 0, 200, QStringLiteral("hover"));
        waiting["potential"] = true;
        waiting["keyframes"] = QJsonArray();
        const Motion::Timeline before = Motion::parse({{"animations", QJsonArray{waiting}}});
        QCOMPARE(before.tracks.size(), 1);
        QVERIFY(before.tracks.first().potential);
        QCOMPARE(before.tracks.first().state(), QStringLiteral("hover"));
        QCOMPARE(before.trigger(), QStringLiteral("On hover"));

        // Held, the page reports one transition per property; they are still the one row, with the same id.
        QJsonArray running;
        for (const char *property : {"transform", "background-color"})
            running.append(animation(QStringLiteral("css-transition"), QString::fromLatin1(property), link, parent, {QString::fromLatin1(property)}, 0, 200,
                                     QStringLiteral("hover")));
        const Motion::Timeline after = Motion::parse({{"animations", running}});
        QCOMPARE(after.tracks.size(), 1);
        QVERIFY(!after.tracks.first().potential);
        QCOMPARE(after.tracks.first().id, before.tracks.first().id);
        QCOMPARE(after.tracks.first().properties, (QStringList{"transform", "background-color"}));
        QCOMPARE(after.tracks.first().label, QStringLiteral("a#primary"));
    }

    void rowsDrivenByScrollComeAfterTheOnesOnTimeAndUseThePagesAxis()
    {
        QJsonArray all;
        QJsonObject card = animation(QStringLiteral("css-animation"), QStringLiteral("nl-fade"), who(QStringLiteral("article"), QStringLiteral("guji"), QStringLiteral("card"), QStringLiteral("#guji")),
                                     who(QStringLiteral("section"), {}, QStringLiteral("cards"), QStringLiteral("section")), {QStringLiteral("opacity")}, 0, 0);
        card["timeline"] = QStringLiteral("view");
        card["trigger"] = QStringLiteral("scroll");
        card["range"] = QJsonObject{{"from", 693.0}, {"to", 934.0}, {"axis", "y"}};
        all.append(card);
        all.append(words(2)["animations"].toArray().first());
        const Motion::Timeline timeline = Motion::parse({{"animations", all}, {"scroll", QJsonObject{{"y", 10}, {"max", 2400}, {"viewport", 600}}}});
        QCOMPARE(timeline.tracks.size(), 2);
        QVERIFY(!timeline.tracks[0].isScroll());
        QVERIFY(timeline.tracks[1].isScroll());
        QCOMPARE(timeline.tracks[1].bars.first().start, 693.0);
        QCOMPARE(timeline.tracks[1].bars.first().length, 241.0);
        QVERIFY(timeline.hasScroll() && timeline.hasTime());
        // Scrolling isn't time: the duration is the rows on time.
        QCOMPARE(timeline.duration, 480.0);
        QCOMPARE(timeline.scrollMax, 2400.0);
        QCOMPARE(timeline.trigger(), QStringLiteral("On load"));
    }

    void anInfiniteAnimationDrawsOneIterationAndSaysItLoops()
    {
        QJsonObject spin = animation(QStringLiteral("css-animation"), QStringLiteral("nl-spin"), who(QStringLiteral("div"), QStringLiteral("dot"), {}, QStringLiteral("#dot")),
                                     who(QStringLiteral("main"), {}, {}, QStringLiteral("main")), {QStringLiteral("rotate")}, 100, 1000);
        spin["iterations"] = -1;
        const Motion::Track track = Motion::parse({{"animations", QJsonArray{spin}}}).tracks.first();
        QVERIFY(track.loops);
        QVERIFY(track.bars.first().loops);
        QCOMPARE(track.bars.first().start, 100.0);
        QCOMPARE(track.bars.first().length, 1000.0);
    }

    void aScriptsGsapTimelineIsOneScrubbableRow()
    {
        const Motion::Timeline timeline = Motion::parse({{"animations", QJsonArray()}, {"gsap", QJsonObject{{"count", 4}, {"start", 100}, {"end", 1600}}}});
        QCOMPARE(timeline.tracks.size(), 1);
        QCOMPARE(timeline.tracks.first().label, QStringLiteral("GSAP timeline"));
        QCOMPARE(timeline.tracks.first().kind, QStringLiteral("gsap"));
        QCOMPARE(timeline.duration, 1600.0);
        QCOMPARE(timeline.trigger(), QStringLiteral("From a script"));
    }

    void somethingThatIsNotAListGivesNoRows()
    {
        QVERIFY(Motion::parse({}).tracks.isEmpty());
        QVERIFY(Motion::parse({{"type", "select"}}).tracks.isEmpty());
        QVERIFY(Motion::parse({{"animations", QJsonArray()}}).trigger().isEmpty());
    }

    void everyOrderGivesEachElementAnIndexFromTheBoxes()
    {
        // Picked huila, guji, nyeri; laid out guji, huila, nyeri from left to right.
        const QList<Motion::Element> elements{{"#huila", QRectF(200, 0, 100, 50)}, {"#guji", QRectF(0, 0, 100, 50)}, {"#nyeri", QRectF(400, 0, 100, 50)}};
        QCOMPARE(Motion::order(Motion::Order::picked, elements), (QList<int>{0, 1, 2}));
        QCOMPARE(Motion::order(Motion::Order::leftToRight, elements), (QList<int>{1, 0, 2}));
        // The middle one first; the other two are the same distance away, so the left one goes first.
        QCOMPARE(Motion::order(Motion::Order::centreOut, elements), (QList<int>{0, 1, 2}));
        // Shuffle: a permutation, the same for the same seed on any machine, and another order for another seed.
        QList<int> a = Motion::order(Motion::Order::shuffle, elements, 1);
        QCOMPARE(a, Motion::order(Motion::Order::shuffle, elements, 1));
        QList<int> sorted = a;
        std::sort(sorted.begin(), sorted.end());
        QCOMPARE(sorted, (QList<int>{0, 1, 2}));
        bool differs = false;
        for (quint32 seed = 2; seed < 12 && !differs; ++seed)
            differs = Motion::order(Motion::Order::shuffle, elements, seed) != a;
        QVERIFY(differs);
        QVERIFY(Motion::order(Motion::Order::picked, {}).isEmpty());
        QCOMPARE(Motion::orderName(Motion::Order::centreOut), QStringLiteral("Centre out"));
    }

    void aGroupIsNamedForItsElementsClass()
    {
        Motion::Timeline timeline = Motion::parse(words(5));
        QCOMPARE(Motion::groupName(timeline.tracks.first()), QStringLiteral("Group · 5 words"));
        QVERIFY(Motion::groupName(Motion::parse(words(1)).tracks.first()).isEmpty());
        QCOMPARE(timeline.tracks.first().bars[2].label, QStringLiteral("span.word"));
    }

    void timesReadInSecondsAndEasingsHaveNames()
    {
        QCOMPARE(Motion::seconds(1400), QStringLiteral("1.40 s"));
        QCOMPARE(Motion::seconds(-5), QStringLiteral("0.00 s"));
        QCOMPARE(Motion::easingName(QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)")), QStringLiteral("Soft out"));
        QCOMPARE(Motion::easingName(QStringLiteral("cubic-bezier(0.16,  1, 0.3, 1)")), QStringLiteral("Soft out"));
        QCOMPARE(Motion::easingName(QStringLiteral("linear")), QStringLiteral("Linear"));
        QCOMPARE(Motion::easingName(QStringLiteral("linear(0, 0.5, 1)")), QStringLiteral("Custom"));
    }

    void theMarkedBlockIsFoundWithItsLinesAndTokens()
    {
        const QList<MotionCode::Block> all = MotionCode::blocks(QStringLiteral(OMASTRATOR_SOURCE_DIR "/tests/Live/fixtures/motion"));
        // The folder has two: the headline's in style.css and the cards' in cards.css, in the order of their files.
        QCOMPARE(all.size(), 2);
        QCOMPARE(all.first().name, QStringLiteral("beans-cascade"));
        QList<MotionCode::Block> found;
        for (const MotionCode::Block &each : all)
            if (each.name == QLatin1String("headline-reveal"))
                found.append(each);
        QCOMPARE(found.size(), 1);
        const MotionCode::Block &block = found.first();
        QCOMPARE(block.name, QStringLiteral("headline-reveal"));
        QCOMPARE(block.file, QStringLiteral("style.css"));
        QVERIFY(block.text.startsWith(QLatin1String("/* omastrator:motion headline-reveal */")));
        QVERIFY(block.text.endsWith(QLatin1String("/* omastrator:motion end */")));
        QVERIFY(block.lastLine > block.firstLine);
        QVERIFY(block.reducedMotion);
        const auto tokens = MotionCode::tokens(block);
        QCOMPARE(tokens.size(), 3);
        QCOMPARE(tokens[0], (QPair<QString, QString>{"--duration-reveal", "480ms"}));
        QCOMPARE(tokens[1].first, QStringLiteral("--stagger-words"));
        QCOMPARE(tokens[2].second, QStringLiteral("cubic-bezier(0.16, 1, 0.3, 1)"));
        // Named by its keyframes, or all when nothing is named.
        QCOMPARE(MotionCode::relevant(all, {QStringLiteral("nl-rise")}, {}).size(), 1);
        QCOMPARE(MotionCode::relevant(all, {QStringLiteral("nl-cascade")}, {}).first().name, QStringLiteral("beans-cascade"));
        QCOMPARE(MotionCode::relevant(all, {QStringLiteral("nl-elsewhere")}, {}).size(), 0);
        QCOMPARE(MotionCode::relevant(all, {}, {}).size(), 2);
        // The custom properties a row takes its values from, read from the rule that runs the animation.
        const MotionCode::Bindings rise = MotionCode::bindings(block, QStringLiteral("nl-rise"));
        QCOMPARE(rise.duration, QStringLiteral("--duration-reveal"));
        QCOMPARE(rise.easing, QStringLiteral("--ease-reveal"));
        QCOMPARE(rise.stagger, QStringLiteral("--stagger-words"));
        // A lede's animation writes its time out: nothing is bound.
        const MotionCode::Bindings fade = MotionCode::bindings(block, QStringLiteral("nl-fade"));
        QVERIFY(fade.duration.isEmpty() && fade.easing.isEmpty() && fade.stagger.isEmpty());
        QVERIFY(MotionCode::bindings(block, QStringLiteral("nl-elsewhere")).duration.isEmpty());
        // The reduced-motion rule, whole; and the one a block would need when it lacks it.
        QCOMPARE(MotionCode::reducedRule(block), QStringLiteral("@media (prefers-reduced-motion: reduce) { .word, .lede, .card { animation: none; } }"));
        MotionCode::Block without = block;
        without.text.remove(QStringLiteral("@media (prefers-reduced-motion: reduce) { .word, .lede, .card { animation: none; } }\n"));
        QVERIFY(MotionCode::reducedRule(without).isEmpty());
        QCOMPARE(MotionCode::defaultReducedRule(without), QStringLiteral("@media (prefers-reduced-motion: reduce) { .word, .lede, .card { animation: none; } }"));
    }

    void buildFoldersAndAMarkerWithoutItsEndAreLeftAlone()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto write = [&](const QString &path, const QByteArray &text) {
            QDir().mkpath(QFileInfo(directory.filePath(path)).absolutePath());
            QFile file(directory.filePath(path));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(text);
        };
        write(QStringLiteral("node_modules/pkg/style.css"), "/* omastrator:motion inside */ a{} /* omastrator:motion end */");
        write(QStringLiteral("dist/app.css"), "/* omastrator:motion built */\na{}\n/* omastrator:motion end */");
        write(QStringLiteral("src/open.css"), "/* omastrator:motion never-closed */\na{}\n");
        write(QStringLiteral("src/two.css"), "/* omastrator:motion a */\nx{}\n/* omastrator:motion end */\n/* omastrator:motion b */\ny{}\n/* omastrator:motion end */\n");
        const QList<MotionCode::Block> found = MotionCode::blocks(directory.path());
        QCOMPARE(found.size(), 2);
        QCOMPARE(found[0].name, QStringLiteral("a"));
        QCOMPARE(found[1].name, QStringLiteral("b"));
        QCOMPARE(found[1].firstLine, 4);
        QVERIFY(!found[0].reducedMotion);
        QVERIFY(MotionCode::blocks(QString()).isEmpty());
        QVERIFY(MotionCode::blocks(directory.filePath(QStringLiteral("missing"))).isEmpty());
    }
    void aBlockMentionsAnAnimationAsAWholeWord()
    {
        MotionCode::Block block;
        block.text = QStringLiteral("@keyframes nl-sunrise { from { opacity: 0; } }\n.a { animation: nl-sunrise 1s, nl-rise-2 2s; }");
        QVERIFY(!MotionCode::mentions(block, QStringLiteral("rise")));
        QVERIFY(!MotionCode::mentions(block, QStringLiteral("nl-rise")));
        QVERIFY(MotionCode::mentions(block, QStringLiteral("nl-sunrise")));
        QVERIFY(MotionCode::mentions(block, QStringLiteral("nl-rise-2")));
        QVERIFY(!MotionCode::mentions(block, QString()));
        block.text += QStringLiteral(" .b { animation-name: rise; }");
        QVERIFY(MotionCode::mentions(block, QStringLiteral("rise")));
    }

    void theCodeTabNeverEntersNodeModulesAndReadsAgainOnlyWhenAFileChanged()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto write = [&](const QString &path, const QByteArray &text) {
            QDir().mkpath(QFileInfo(directory.filePath(path)).absolutePath());
            QFile file(directory.filePath(path));
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(text);
        };
        write(QStringLiteral("src/one.css"), "/* omastrator:motion one */\na{}\n/* omastrator:motion end */\n");
        // A hundred files under node_modules would spend the limit if the walk went in.
        for (int i = 0; i < 120; ++i)
            write(QStringLiteral("node_modules/pkg/f%1.css").arg(i), "a{}\n");
        write(QStringLiteral("node_modules/pkg/zz.css"), "/* omastrator:motion inside */\na{}\n/* omastrator:motion end */\n");
        write(QStringLiteral("src/zz.css"), "b{}\n");
        const QList<MotionCode::Block> first = MotionCode::blocks(directory.path(), 10);
        QCOMPARE(first.size(), 1);
        QCOMPARE(first[0].name, QStringLiteral("one"));
        // The same files: the answer is the same, and a file that changed is read again.
        QCOMPARE(MotionCode::blocks(directory.path(), 10).size(), 1);
        write(QStringLiteral("src/zz.css"), "/* omastrator:motion two */\nb{}\n/* omastrator:motion end */\n");
        const QList<MotionCode::Block> second = MotionCode::blocks(directory.path(), 10);
        QCOMPARE(second.size(), 2);
        QCOMPARE(second[1].name, QStringLiteral("two"));
    }
};

QTEST_GUILESS_MAIN(MotionModelTests)
#include "MotionModelTests.moc"
