#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Document/Hyphenator.h"
#include "Document/PathOperations.h"
#include "Document/TextLayout.h"
#include "../FontSupport.h"
#include <QTest>
#include <algorithm>
#include <cmath>
#include <numbers>

// Type on a Path (P2-3), text wrap and threaded area type (P2-4), and hyphenation (P2-5).
class TextFlowTests : public QObject {
    Q_OBJECT

private:
    static TextContent text(const QString &words, double size = 24)
    {
        TextContent content;
        content.text = words;
        content.size = size;
        return content;
    }

    static VectorObject areaObject(const QString &words, QSizeF area, QPointF at = {})
    {
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(words, 16);
        object.text.area = area;
        object.transform = QTransform::fromTranslate(at.x(), at.y());
        return object;
    }

    // Every glyph's outline centre, in the text's own coordinates.
    static std::vector<QPointF> glyphCentres(const QPainterPath &outline)
    {
        std::vector<QPointF> centres;
        int start = 0;
        for (int index = 1; index <= outline.elementCount(); ++index) {
            if (index == outline.elementCount() || outline.elementAt(index).type == QPainterPath::MoveToElement) {
                QPainterPath one;
                for (int at = start; at < index; ++at) {
                    const QPainterPath::Element e = outline.elementAt(at);
                    if (e.type == QPainterPath::MoveToElement)
                        one.moveTo(e.x, e.y);
                    else if (e.type == QPainterPath::LineToElement)
                        one.lineTo(e.x, e.y);
                    else if (e.type == QPainterPath::CurveToElement) {
                        const QPainterPath::Element c1 = outline.elementAt(at + 1), c2 = outline.elementAt(at + 2);
                        one.cubicTo(QPointF(e.x, e.y), QPointF(c1.x, c1.y), QPointF(c2.x, c2.y));
                        at += 2;
                    }
                }
                if (!one.isEmpty())
                    centres.push_back(one.boundingRect().center());
                start = index;
            }
        }
        return centres;
    }

private slots:
    void typeOnAPathFollowsACircle()
    {
        const QPointF centre(200, 200);
        const double radius = 200;
        VectorPath circle = Shapes::ellipse(QRectF(centre.x() - radius, centre.y() - radius, radius * 2, radius * 2));
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(QStringLiteral("ROUND AND ROUND"), 24);
        object.text.onPath = TextPath{circle, 0, false};
        const TextLayout layout(object.text);
        QVERIFY(!layout.lines().empty());
        const auto centres = glyphCentres(layout.outline());
        QVERIFY(centres.size() > 5);
        // Every glyph sits close to the circle: within its size of the radius.
        for (const QPointF &at : centres)
            QVERIFY(std::abs(std::hypot(at.x() - centre.x(), at.y() - centre.y()) - radius) < object.text.size);
        // Later glyphs sweep further around the circle than earlier ones.
        double lastAngle = -1e9;
        for (const QPointF &at : centres) {
            double angle = std::atan2(at.y() - centre.y(), at.x() - centre.x());
            if (angle < lastAngle - std::numbers::pi)
                angle += 2 * std::numbers::pi;
            // A hollow letter's inner and outer contours can wobble a hair against each
            // other; only an actual reversal (not that jitter) should fail this.
            QVERIFY(angle > lastAngle - 0.05);
            lastAngle = angle;
        }
    }

    void slidingTheBracketMovesTheFirstGlyph()
    {
        VectorPath circle = Shapes::ellipse(QRectF(0, 0, 200, 200));
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(QStringLiteral("Hello"), 24);
        object.text.onPath = TextPath{circle, 0, false};
        const auto before = glyphCentres(TextLayout(object.text).outline());
        object.text.onPath->start = 0.25;
        const auto after = glyphCentres(TextLayout(object.text).outline());
        QVERIFY(!before.empty() && !after.empty());
        QVERIFY(QLineF(before.front(), after.front()).length() > 20);
    }

    void flippingPutsGlyphsOnTheOtherSide()
    {
        VectorPath line;
        Contour contour;
        contour.nodes = {PathNode(QPointF(0, 0)), PathNode(QPointF(300, 0))};
        line.contours.push_back(contour);
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(QStringLiteral("Flip"), 24);
        object.text.onPath = TextPath{line, 0.5, false};
        const QRectF straight = TextLayout(object.text).outline().boundingRect();
        object.text.onPath->flipped = true;
        const QRectF flipped = TextLayout(object.text).outline().boundingRect();
        // Flipping reads the reversed path, so the same visible point now sits the other way:
        // the glyphs' horizontal position about the midpoint mirrors.
        QVERIFY(std::abs((straight.center().x() - 150) + (flipped.center().x() - 150)) < 5);
    }

    void wrapAvoidsTheBoundsAndOffset()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        const QUuid layer = document.layers().front();
        VectorObject area = areaObject(QStringLiteral("one two three four five six seven eight nine ten eleven twelve thirteen"),
                                       QSizeF(200, 200));
        document.insert(area, layer);
        // The blocker sits above the text in paint order, as Text Wrap requires.
        VectorObject blocker;
        blocker.kind = ObjectKind::path;
        blocker.path = Shapes::rectangle(QRectF(60, 0, 60, 200));
        blocker.textWrap = 8;
        document.insert(blocker, layer);
        document.reflowText();
        const VectorObject *placed = document.find(area.id);
        QVERIFY(!placed->text.flow.frames.empty());
        const TextLayout layout(placed->text);
        const QRectF excluded(60 - 8, 0 - 8, 60 + 16, 200 + 16);
        for (const QPointF &at : glyphCentres(layout.outline()))
            QVERIFY(!excluded.contains(at));
    }

    void threadedOverflowContinuesInTheSecondBox()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        const QUuid layer = document.layers().front();
        QString long_;
        for (int i = 0; i < 40; ++i)
            long_ += QStringLiteral("word ");
        VectorObject first = areaObject(long_.trimmed(), QSizeF(80, 40));
        VectorObject second = areaObject(QString(), QSizeF(80, 200), {100, 0});
        first.text.threadNext = second.id;
        document.insert(first, layer);
        document.insert(second, layer);
        document.reflowText();
        const VectorObject *headNow = document.find(first.id);
        const VectorObject *followerNow = document.find(second.id);
        QVERIFY(headNow->text.flow.story);
        QCOMPARE(followerNow->text.flow.frame, 1);
        const TextLayout headLayout(headNow->text), followerLayout(followerNow->text);
        QVERIFY(headLayout.overflows());
        // The second box shows text the first one didn't: later characters of the shared story.
        int followerStart = -1;
        for (const TextLayout::Line &line : followerLayout.lines()) {
            if (!line.hidden && line.frame == followerNow->text.flow.frame)
                followerStart = followerStart < 0 ? line.start : followerStart;
        }
        QVERIFY(followerStart > 0);
    }

    void relinkAndUnlinkThreading()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject first = areaObject(QStringLiteral("Some rather long story text that will overflow a small box easily"), QSizeF(80, 30));
        VectorObject second = areaObject(QStringLiteral("Already here"), QSizeF(100, 100), {150, 0});
        const QUuid firstID = session.addObject(first, QStringLiteral("Type"));
        const QUuid secondID = session.addObject(second, QStringLiteral("Type"));
        session.linkThread(firstID, secondID);
        QCOMPARE(session.document()->find(firstID)->text.threadNext, secondID);
        // The second box's own words joined the story as a paragraph, not lost.
        QVERIFY(session.document()->find(firstID)->text.flow.story->text.contains(QStringLiteral("Already here")));
        QVERIFY(session.document()->find(secondID)->text.text.isEmpty());
        session.removeThreading(firstID);
        QVERIFY(session.document()->find(firstID)->text.threadNext.isNull());
        QVERIFY(!session.document()->find(firstID)->text.flow.story);
        // Between them, every character of the story is kept somewhere.
        const QString rejoined = session.document()->find(firstID)->text.text + session.document()->find(secondID)->text.text;
        QVERIFY(rejoined.contains(QStringLiteral("Some rather")));
    }

    void aSoftHyphenShowsOnlyAtALineEnd()
    {
        if (!haveInstalledFonts())
            QSKIP("No fonts installed (a bare container): line widths mean nothing without a font");
        VectorObject wide = areaObject(QStringLiteral("super­califragilisticexpialidocious word"), QSizeF(600, 200));
        VectorObject narrow = areaObject(QStringLiteral("super­califragilisticexpialidocious word"), QSizeF(90, 200));
        const TextLayout wideLayout(wide.text), narrowLayout(narrow.text);
        // Wide enough that the soft hyphen sits mid-line: it draws nothing.
        QCOMPARE(int(wideLayout.lines().size()), 1);
        QVERIFY(!wideLayout.lines().front().hyphenated);
        // Narrow enough that the word breaks there: a hyphen shows, so there are more glyphs.
        QVERIFY(narrowLayout.lines().size() >= 2);
        const bool anyHyphenated = std::any_of(narrowLayout.lines().begin(), narrowLayout.lines().end(), [](const auto &l) { return l.hyphenated; });
        QVERIFY(anyHyphenated);
        QVERIFY(narrowLayout.glyphCount() > wideLayout.glyphCount());
    }

    void automaticHyphenationBreaksInsideTheWord()
    {
        const std::vector<int> breaks = Hyphenator::breakPoints(QStringLiteral("hyphenation"), 2, 3);
        QVERIFY(!breaks.empty());
        // "hy-phen-a-tion": at least the two syllable breaks the handoff's research found.
        QVERIFY(std::find(breaks.begin(), breaks.end(), 2) != breaks.end());
        QVERIFY(std::find(breaks.begin(), breaks.end(), 6) != breaks.end());
        for (int p : breaks)
            QVERIFY(p >= 2 && p <= int(QStringLiteral("hyphenation").size()) - 3);

        VectorObject narrow = areaObject(QStringLiteral("hyphenation"), QSizeF(45, 200));
        narrow.text.hyphenate = true;
        const TextLayout hyphenated(narrow.text);
        narrow.text.hyphenate = false;
        const TextLayout plain(narrow.text);
        // Off, the whole word squeezes or overflows one line; on, it breaks inside the word.
        QVERIFY(hyphenated.lines().size() >= plain.lines().size());
        QVERIFY(hyphenated.lines().size() >= 2);
    }
};

QTEST_MAIN(TextFlowTests)
#include "TextFlowTests.moc"
