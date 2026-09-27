#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Document/TextLayout.h"
#include <QFontDatabase>
#include <QFontInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QTest>

// Type: the character model, point and area layout, .omai migration, and the
// session's type edits (fields, keys, conversion, area boxes, scale options).
class TypographyTests : public QObject {
    Q_OBJECT

private:
    static TextContent text(const QString &words, double size = 100)
    {
        TextContent content;
        content.text = words;
        content.size = size;
        return content;
    }

    static double width(const TextContent &content) { return content.outline().boundingRect().width(); }

    static QUuid addText(EditorSession &session, const QString &words, QPointF at = {20, 100})
    {
        return session.addObject(session.textObject(at, words), QStringLiteral("Type"));
    }

    static VectorObject areaObject(const QString &words, double boxWidth, TextAlignment alignment = TextAlignment::left)
    {
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(words, 16);
        object.text.area = QSizeF(boxWidth, 0);
        object.text.alignment = alignment;
        object.transform = QTransform::fromTranslate(10, 10);
        return object;
    }

    static const QString sentence;

private slots:
    void trackingIsInThousandthsOfAnEm()
    {
        const TextContent plain = text(QStringLiteral("HHHH"));
        TextContent tracked = plain;
        tracked.tracking = 50;
        // Three gaps, each 50/1000 of a 100 pt em.
        QVERIFY2(std::abs(width(tracked) - width(plain) - 15) < 0.5, qPrintable(QString::number(width(tracked) - width(plain))));
        // It scales with the size.
        TextContent small = tracked;
        small.size = 50;
        TextContent smallPlain = plain;
        smallPlain.size = 50;
        QVERIFY(std::abs(width(small) - width(smallPlain) - 7.5) < 0.5);
    }

    void autoLeadingIsTwelveTenthsOfTheSize()
    {
        TextContent content = text(QStringLiteral("One\nTwo"), 24);
        QVERIFY(!content.leading);
        QCOMPARE(content.effectiveLeading(), 28.8);
        const TextLayout layout(content);
        QCOMPARE(int(layout.lines().size()), 2);
        QVERIFY(std::abs(layout.lines()[1].baseline - layout.lines()[0].baseline - 28.8) < 1e-9);
        content.leading = 40;
        const TextLayout set(content);
        QVERIFY(std::abs(set.lines()[1].baseline - 40) < 1e-9);
    }

    void aStyleNamesTheFace()
    {
        // The first installed family with a Bold Italic face.
        QString family;
        for (const QString &each : QFontDatabase::families()) {
            if (QFontDatabase::styles(each).contains(QStringLiteral("Bold Italic"))) {
                family = each;
                break;
            }
        }
        if (family.isEmpty())
            QSKIP("No installed family has a Bold Italic face");
        TextContent content = text(QStringLiteral("Face"));
        content.family = family;
        content.style = QStringLiteral("Bold Italic");
        QVERIFY(content.isBold() && content.isItalic());
        const QFontInfo info(content.font());
        QVERIFY(info.italic());
        QVERIFY(info.weight() >= QFont::Bold);
        TextContent regular = content;
        regular.style = TextContent::styleFor(family, 400, false);
        QVERIFY(!regular.isBold() && !regular.isItalic());
        QVERIFY(content.outline() != regular.outline());
        // A face the family lacks is made from its name.
        TextContent madeUp = text(QStringLiteral("Face"));
        madeUp.family = QStringLiteral("No Such Family Anywhere");
        madeUp.style = QStringLiteral("SemiBold Italic");
        QVERIFY(madeUp.isBold() && madeUp.isItalic());
    }

    void scaleShiftCaseAndDecorationChangeTheOutline()
    {
        const TextContent plain = text(QStringLiteral("Type"));
        TextContent wide = plain;
        wide.horizontalScale = 200;
        QVERIFY(std::abs(width(wide) - 2 * width(plain)) < 1);
        TextContent tall = plain;
        tall.verticalScale = 200;
        QVERIFY(std::abs(tall.outline().boundingRect().height() - 2 * plain.outline().boundingRect().height()) < 1);
        TextContent raised = plain;
        raised.baselineShift = 10;
        QVERIFY(std::abs(raised.outline().boundingRect().top() - plain.outline().boundingRect().top() + 10) < 1e-6);
        TextContent caps = plain;
        caps.textCase = TextCase::allCaps;
        QVERIFY(caps.outline() != plain.outline());
        TextContent underlined = plain;
        underlined.underline = true;
        QVERIFY(underlined.outline().boundingRect().bottom() > plain.outline().boundingRect().bottom());
        TextContent struck = plain;
        struck.strikethrough = true;
        QVERIFY(struck.outline() != plain.outline());
        QCOMPARE(struck.outline().boundingRect().height(), plain.outline().boundingRect().height());
        TextContent unkerned = text(QStringLiteral("AVAVAV"));
        const double kerned = width(unkerned);
        unkerned.kerning = TextKerning::none;
        QVERIFY(width(unkerned) >= kerned);
    }

    void manualKernsMoveOnlyWhatFollows()
    {
        TextContent content = text(QStringLiteral("ABCD"));
        const TextLayout before(content);
        content.kerns[2] = 100;
        const TextLayout after(content);
        QVERIFY(std::abs(after.xAt(1) - before.xAt(1)) < 1e-6);
        QVERIFY(std::abs(after.xAt(2) - before.xAt(2) - 10) < 0.5);
        QVERIFY(std::abs(after.xAt(4) - before.xAt(4) - 10) < 0.5);
        // Typing before a kern carries it along with its character.
        content.replaceKerns(0, 0, 2);
        QVERIFY(content.kerns.count(4) && !content.kerns.count(2));
    }

    void areaTypeWrapsInsideItsBox()
    {
        TextContent content = text(sentence, 16);
        content.area = QSizeF(200, 0);
        const TextLayout layout(content);
        QVERIFY(layout.lines().size() > 2);
        for (const TextLayout::Line &line : layout.lines())
            QVERIFY(line.right <= 200 + 0.5);
        // The box's height grows with the text when it's 0.
        QCOMPARE(content.frame().width(), 200.0);
        QVERIFY(std::abs(content.frame().height() - (layout.lines().back().baseline + layout.lines().back().descent)) < 1e-6);
        // A fixed height hides what doesn't fit and says so.
        content.area = QSizeF(200, 30);
        QVERIFY(content.overflows());
        QVERIFY(TextLayout(content).lines().back().hidden);
    }

    void justifyAllFillsEveryLine()
    {
        TextContent content = text(sentence, 16);
        content.area = QSizeF(200, 0);
        content.alignment = TextAlignment::justifyAll;
        const TextLayout all(content);
        for (const TextLayout::Line &line : all.lines())
            QVERIFY2(std::abs(line.right - line.left - 200) < 0.5, qPrintable(QString::number(line.right - line.left)));
        // Justify leaves the last line flush left.
        content.alignment = TextAlignment::justify;
        const TextLayout last(content);
        QVERIFY(last.lines().back().right - last.lines().back().left < 199);
        QVERIFY(std::abs(last.lines().front().right - last.lines().front().left - 200) < 0.5);
    }

    void version1TextMigrates()
    {
        // Bold, tracking in pt and leading as a multiple, as version 1 wrote them.
        const QJsonObject old{{"string", "Hi\nThere"}, {"family", "Sans Serif"}, {"size", 20}, {"bold", true}, {"italic", false},
                              {"alignment", "center"}, {"leading", 1.5}, {"tracking", 2}};
        const TextContent read = DocumentCodec::decodeText(old);
        QVERIFY(read.isBold());
        QVERIFY(!read.isItalic());
        QCOMPARE(read.tracking, 100.0);
        QCOMPARE(read.leading, std::optional<double>(30));
        QCOMPARE(read.alignment, TextAlignment::center);
        // The same spacing as before: 2 pt after each character, 30 pt between baselines.
        QVERIFY(std::abs(read.font().letterSpacing() - 2) < 1e-9);
        QVERIFY(std::abs(TextLayout(read).lines()[1].baseline - 30) < 1e-9);
        // The old default, 1.2, is Auto.
        QJsonObject automatic = old;
        automatic["leading"] = 1.2;
        QVERIFY(!DocumentCodec::decodeText(automatic).leading);
        QCOMPARE(DocumentCodec::version, 5);
    }

    void everyCharacterFieldRoundTrips()
    {
        TextContent content = text(QStringLiteral("Round\ntrip"), 18);
        content.family = QStringLiteral("Serif");
        content.style = QStringLiteral("Bold");
        content.alignment = TextAlignment::justifyAll;
        content.leading = 22.5;
        content.tracking = -35;
        content.kerning = TextKerning::none;
        content.kerns = {{2, 40}, {4, -20}};
        content.horizontalScale = 90;
        content.verticalScale = 110;
        content.baselineShift = 3;
        content.textCase = TextCase::smallCaps;
        content.underline = true;
        content.strikethrough = true;
        content.area = QSizeF(240, 80);
        QCOMPARE(DocumentCodec::decodeText(DocumentCodec::encode(content)), content);
        // Whole documents too, at the new version.
        VectorDocument document = VectorDocument::blank(QSizeF(300, 200));
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = content;
        document.insert(object, document.layers().front());
        const QJsonObject json = DocumentCodec::encode(document);
        QCOMPARE(json["version"].toInt(), 5);
        QCOMPARE(DocumentCodec::decode(json).find(object.id)->text, content);
    }

    void fieldEditsAreOneNamedStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid first = addText(session, QStringLiteral("One"));
        const QUuid second = addText(session, QStringLiteral("Two"), {20, 200});
        session.select({first, second});
        session.updateText([](TextContent &content) { content.tracking = 40; }, QStringLiteral("Tracking"));
        QCOMPARE(session.document()->find(first)->text.tracking, 40.0);
        QCOMPARE(session.document()->find(second)->text.tracking, 40.0);
        QCOMPARE(session.undoName(), QStringLiteral("Tracking"));
        // The next text takes it too.
        QCOMPARE(session.defaultText.tracking, 40.0);
        session.undo();
        QCOMPARE(session.document()->find(first)->text.tracking, 0.0);
        QCOMPARE(session.document()->find(second)->text.tracking, 0.0);
    }

    void heldTypeKeysAreOneStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, QStringLiteral("Held"));
        const QString before = session.undoName();
        for (int repeat = 0; repeat < 10; ++repeat)
            session.stepText(EditorSession::TextStep::tracking, 20);
        QCOMPARE(session.document()->find(id)->text.tracking, 200.0);
        QCOMPARE(session.undoName(), QStringLiteral("Tracking"));
        session.undo();
        QCOMPARE(session.document()->find(id)->text.tracking, 0.0);
        QCOMPARE(session.undoName(), before);
        // Leading steps from Auto's value; baseline shift and size step in points.
        session.stepText(EditorSession::TextStep::leading, -2);
        QCOMPARE(session.document()->find(id)->text.leading, std::optional<double>(session.document()->find(id)->text.size * 1.2 - 2));
        session.stepText(EditorSession::TextStep::baselineShift, 2);
        QCOMPARE(session.document()->find(id)->text.baselineShift, 2.0);
        const double size = session.document()->find(id)->text.size;
        session.stepText(EditorSession::TextStep::size, 2);
        QCOMPARE(session.document()->find(id)->text.size, size + 2);
        QCOMPARE(session.undoName(), QStringLiteral("Font Size"));
    }

    void kerningAtACaretIsItsOwnStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, QStringLiteral("AV"));
        session.kernText(id, 1, -20);
        session.kernText(id, 1, -20);
        QCOMPARE(session.document()->find(id)->text.kerns.at(1), -40.0);
        QCOMPARE(session.undoName(), QStringLiteral("Kerning"));
        session.kernText(id, 1, 40);
        QVERIFY(session.document()->find(id)->text.kerns.empty());
        // Outside the text there's no pair to kern.
        session.kernText(id, 5, 20);
        QVERIFY(session.document()->find(id)->text.kerns.empty());
    }

    void pointAndAreaConvertInPlace()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, sentence);
        const QRectF glyphs = session.document()->find(id)->outline().boundingRect();
        session.convertTextType(true);
        const VectorObject *area = session.document()->find(id);
        QVERIFY(area->text.area.has_value());
        QCOMPARE(session.undoName(), QStringLiteral("Convert to Area Type"));
        QCOMPARE(area->text.text, sentence);
        // No glyph moves.
        const QRectF placed = area->outline().boundingRect();
        QVERIFY(std::abs(placed.left() - glyphs.left()) < 0.5 && std::abs(placed.top() - glyphs.top()) < 0.5);
        // A narrower box wraps; back to point type keeps each line as it was.
        session.setTextArea(id, QSizeF(150, 0));
        const int lines = int(TextLayout(session.document()->find(id)->text).lines().size());
        QVERIFY(lines > 1);
        session.convertTextType(false);
        const VectorObject *point = session.document()->find(id);
        QVERIFY(!point->text.area);
        QCOMPARE(int(point->text.text.count(QLatin1Char('\n'))) + 1, lines);
        QCOMPARE(QString(point->text.text).remove(QLatin1Char('\n')).simplified(), QString(sentence).simplified());
    }

    void resizingAreaTypeRewrapsWithoutScalingGlyphs()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = session.addObject(areaObject(sentence, 300), QStringLiteral("Type"));
        const int before = int(TextLayout(session.document()->find(id)->text).lines().size());
        session.transformSelection(QTransform::fromTranslate(-10, -10) * QTransform::fromScale(0.5, 1) * QTransform::fromTranslate(10, 10),
                                   QStringLiteral("Scale"), true);
        const VectorObject *after = session.document()->find(id);
        QCOMPARE(after->text.area->width(), 150.0);
        QCOMPARE(after->text.size, 16.0);
        QVERIFY(int(TextLayout(after->text).lines().size()) > before);
        QCOMPARE(after->transform, QTransform::fromTranslate(10, 10));
        // Without reflow, as the Scale tool does, the glyphs scale.
        session.transformSelection(QTransform::fromScale(2, 2), QStringLiteral("Scale"));
        QCOMPARE(session.document()->find(id)->text.area->width(), 150.0);
        QCOMPARE(session.document()->find(id)->transform.m11(), 2.0);
    }

    void scaleStrokesDecidesStrokeWeights()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid box = session.addPath(Shapes::rectangle({0, 0, 100, 100}), QStringLiteral("Rectangle"));
        StrokeStyle stroke;
        stroke.width = 4;
        session.setStrokeOfSelection(stroke);
        session.scaleSelection(2, 2);
        QCOMPARE(session.document()->find(box)->stroke.width, 4.0);
        session.scaleStrokes = true;
        session.scaleSelection(2, 2);
        QCOMPARE(session.document()->find(box)->stroke.width, 8.0);
    }

    void eachObjectTransformsOnItsOwn()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid a = session.addPath(Shapes::rectangle({0, 0, 10, 10}), QStringLiteral("A"));
        const QUuid b = session.addPath(Shapes::rectangle({50, 0, 20, 10}), QStringLiteral("B"));
        session.select({a, b});
        session.transformEach([](const QRectF &bounds) {
            return QTransform::fromTranslate(-bounds.left(), 0) * QTransform::fromScale(2, 1) * QTransform::fromTranslate(bounds.left(), 0);
        }, QStringLiteral("Scale"));
        QCOMPARE(session.document()->bounds(a), QRectF(0, 0, 20, 10));
        QCOMPARE(session.document()->bounds(b), QRectF(50, 0, 40, 10));
        session.undo();
        QCOMPARE(session.document()->bounds(b), QRectF(50, 0, 20, 10));
    }

    void areaTypeIsHitAnywhereInItsBox()
    {
        VectorDocument document = VectorDocument::blank(QSizeF(400, 300));
        VectorObject area = areaObject(QStringLiteral("Short"), 200);
        area.text.area = QSizeF(200, 100);
        document.insert(area, document.layers().front());
        QCOMPARE(document.hitTest({180, 90}, 1), std::optional(area.id));
        QCOMPARE(document.bounds(area.id), QRectF(10, 10, 200, 100));
    }
};

const QString TypographyTests::sentence = QStringLiteral("The quick brown fox jumps over the lazy dog and keeps running far away");

QTEST_MAIN(TypographyTests)
#include "TypographyTests.moc"
