#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Document/FontFeatures.h"
#include "Document/TextLayout.h"
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QTest>

// Styled runs, paragraph formats and OpenType features: the model, the layout
// they give, the session's range edits, Create Outlines and the .omai format.
class TextRunsTests : public QObject {
    Q_OBJECT

private:
    static TextContent text(const QString &words, double size = 40)
    {
        TextContent content;
        content.text = words;
        content.size = size;
        return content;
    }

    static QString boldFace(const QString &family) { return TextContent::styleFor(family, 700, false); }

    static QUuid addText(EditorSession &session, const TextContent &content, QPointF at = {20, 100})
    {
        VectorObject object = session.textObject(at, content.text);
        object.text = content;
        return session.addObject(object, QStringLiteral("Type"));
    }

    static VectorObject areaObject(const QString &words, double width)
    {
        VectorObject object;
        object.kind = ObjectKind::text;
        object.text = text(words, 16);
        object.text.area = QSizeF(width, 0);
        return object;
    }

private slots:
    void runsStayWithTheirCharacters()
    {
        TextContent content = text(QStringLiteral("Hello world"));
        content.formatCharacters(6, 11, [&](CharacterFormat &format) { format.style = boldFace(format.family); });
        QCOMPARE(int(content.runs.size()), 1);
        QCOMPARE(content.runs.front().start, 6);
        QCOMPARE(content.runs.front().length, 5);
        // Text typed before the run pushes it along.
        content.replace(0, 0, QStringLiteral("Oh "));
        QCOMPARE(content.runs.front().start, 9);
        // Typed at the run's end, text takes the run's format; at its start, the one before.
        content.replace(14, 14, QStringLiteral("s"));
        QCOMPARE(content.text, QStringLiteral("Oh Hello worlds"));
        QCOMPARE(content.runs.front().length, 6);
        content.replace(9, 9, QStringLiteral("_"));
        QCOMPARE(content.formatAt(9).style, content.style);
        QCOMPARE(content.runs.front().start, 10);
        // Replacing a run's characters keeps its format for what's typed.
        content.replace(10, 16, QStringLiteral("there"));
        QCOMPARE(content.formatAt(12).style, boldFace(content.family));
        // Deleting a run's characters deletes the run.
        content.replace(10, 15, QString());
        QVERIFY(content.runs.empty());
        // A run restyled back to the object's own format disappears.
        content.formatCharacters(0, 2, [](CharacterFormat &format) { format.size = 80; });
        QCOMPARE(int(content.runs.size()), 1);
        content.formatCharacters(0, 2, [](CharacterFormat &format) { format.size = 40; });
        QVERIFY(content.runs.empty());
    }

    void paragraphFormatsFollowTheirParagraphs()
    {
        TextContent content = text(QStringLiteral("One\nTwo\nThree"));
        content.formatParagraphs(1, 1, [](ParagraphFormat &format) { format.leftIndent = 10; });
        QCOMPARE(content.paragraphAt(1).leftIndent, 10.0);
        QCOMPARE(content.paragraphAt(2).leftIndent, 0.0);
        // A new paragraph above moves it down; one split from it takes its format.
        content.replace(0, 0, QStringLiteral("Zero\n"));
        QCOMPARE(content.paragraphAt(2).leftIndent, 10.0);
        QCOMPARE(content.paragraphAt(1).leftIndent, 0.0);
        const int inTwo = content.paragraphStart(2) + 1;
        content.replace(inTwo, inTwo, QStringLiteral("\n"));
        QCOMPARE(content.paragraphAt(2).leftIndent, 10.0);
        QCOMPARE(content.paragraphAt(3).leftIndent, 10.0);
        // Merged, a paragraph keeps the first one's format.
        const int end = content.paragraphStart(2) - 1;
        content.replace(end, end + 1, QString());
        QCOMPARE(content.paragraphAt(1).leftIndent, 0.0);
    }

    void aLargerRunMakesItsCharactersLarger()
    {
        TextContent plain = text(QStringLiteral("HHHH"));
        TextContent mixed = plain;
        mixed.formatCharacters(2, 4, [](CharacterFormat &format) { format.size = 80; });
        const TextLayout before(plain), after(mixed);
        // The first two letters don't move; the last two are twice as wide and tall.
        QVERIFY(std::abs(after.xAt(2) - before.xAt(2)) < 0.01);
        const double small = before.xAt(4) - before.xAt(2), large = after.xAt(4) - after.xAt(2);
        QVERIFY2(std::abs(large - 2 * small) < 0.5, qPrintable(QString("%1 %2").arg(small).arg(large)));
        QVERIFY(mixed.outline().boundingRect().height() > plain.outline().boundingRect().height() * 1.8);
        // Fractional sizes are exact too.
        TextContent fraction = plain;
        fraction.formatCharacters(0, 4, [](CharacterFormat &format) { format.size = 40.5; });
        TextContent whole = plain;
        whole.size = 40.5;
        QVERIFY(std::abs(TextLayout(fraction).xAt(4) - TextLayout(whole).xAt(4)) < 0.05);
    }

    void autoLeadingFollowsTheLargestSizeOnALine()
    {
        TextContent content = text(QStringLiteral("One\nTwo"), 20);
        content.formatCharacters(4, 5, [](CharacterFormat &format) { format.size = 50; });
        const TextLayout layout(content);
        QCOMPARE(int(layout.lines().size()), 2);
        QVERIFY(std::abs(layout.lines()[1].baseline - layout.lines()[0].baseline - 60) < 1e-6);
    }

    void kerningBetweenTwoLettersShiftsOnlyWhatFollows()
    {
        // Letters without kern pairs, which a manual kern's format would break.
        TextContent content = text(QStringLiteral("HIHIH"));
        content.formatCharacters(3, 5, [&](CharacterFormat &format) { format.style = boldFace(format.family); });
        const TextLayout before(content);
        content.kerns[2] = 250;
        const TextLayout after(content);
        QVERIFY(std::abs(after.xAt(1) - before.xAt(1)) < 0.01);
        for (int index = 2; index <= 5; ++index)
            QVERIFY2(std::abs(after.xAt(index) - before.xAt(index) - 10) < 0.05, qPrintable(QString::number(after.xAt(index) - before.xAt(index))));
    }

    void runsCarryTheirOwnColourAndDecoration()
    {
        TextContent content = text(QStringLiteral("red and plain"));
        content.formatCharacters(0, 3, [](CharacterFormat &format) {
            format.fill = QColor(Qt::red);
            format.underline = true;
        });
        const auto fills = content.fills();
        QCOMPARE(int(fills.size()), 2);
        QVERIFY(!fills[0].first.has_value());
        QCOMPARE(fills[1].first, std::optional<QColor>(QColor(Qt::red)));
        // The underline sits under the red word only.
        const QRectF red = fills[1].second.boundingRect(), rest = fills[0].second.boundingRect();
        QVERIFY(red.bottom() > rest.bottom());
        QVERIFY(red.right() <= rest.left() + 1);
    }

    void aRangeBeingEditedIsWhatTypeEditsChange()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, text(QStringLiteral("Hello world")));
        session.setTextRange(EditorSession::TextRange{id, 11, 6});
        session.updateText([](TextContent &content) { content.size = 60; }, QStringLiteral("Font Size"));
        const TextContent &content = session.document()->find(id)->text;
        QCOMPARE(content.size, 40.0);
        QCOMPARE(int(content.runs.size()), 1);
        QCOMPARE(content.runs.front().start, 6);
        QCOMPARE(content.runs.front().format.size, 60.0);
        QCOMPARE(session.undoName(), QStringLiteral("Font Size"));
        // What's shown is the range's format; the whole text reads as two.
        QCOMPARE(int(session.shownTexts().size()), 1);
        QCOMPARE(session.shownText().size, 60.0);
        session.setTextRange(std::nullopt);
        QCOMPARE(int(session.shownTexts().size()), 2);
        // A step on the whole text keeps each run's own size.
        session.stepText(EditorSession::TextStep::size, 2);
        QCOMPARE(session.document()->find(id)->text.size, 42.0);
        QCOMPARE(session.document()->find(id)->text.runs.front().format.size, 62.0);
        // An absolute value reaches every run.
        session.updateText([](TextContent &content) { content.size = 30; }, QStringLiteral("Font Size"));
        QVERIFY(session.document()->find(id)->text.runs.empty());
        session.undo();
        session.undo();
        session.undo();
        QVERIFY(session.document()->find(id)->text.runs.empty());
        QCOMPARE(session.document()->find(id)->text.size, 40.0);
    }

    void aFillWithCharactersSelectedColoursJustThem()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, text(QStringLiteral("Hello world")));
        session.setTextRange(EditorSession::TextRange{id, 0, 5});
        session.setFillOfSelection(Paint::solid(Qt::red));
        const VectorObject *object = session.document()->find(id);
        QCOMPARE(object->text.formatAt(0).fill, std::optional<QColor>(QColor(Qt::red)));
        QVERIFY(!object->text.formatAt(6).fill.has_value());
        QVERIFY(object->fill != Paint::solid(Qt::red));
        QCOMPARE(session.undoName(), QStringLiteral("Fill"));
        // The whole text's fill reaches the coloured run too.
        session.setTextRange(std::nullopt);
        session.setFillOfSelection(Paint::solid(Qt::blue));
        object = session.document()->find(id);
        QCOMPARE(object->fill, Paint::solid(Qt::blue));
        QVERIFY(object->text.runs.empty());
    }

    void createOutlinesKeepsEachRunsLook()
    {
        EditorSession session;
        session.createDocument({600, 300});
        TextContent content = text(QStringLiteral("Big red"));
        content.formatCharacters(0, 3, [](CharacterFormat &format) { format.size = 80; });
        content.formatCharacters(4, 7, [](CharacterFormat &format) { format.fill = QColor(Qt::red); });
        const QUuid id = addText(session, content);
        const QRectF before = session.document()->bounds(id);
        const QRectF redGlyphs = session.document()->find(id)->transform.mapRect(content.fills()[1].second.boundingRect());
        session.convertTextToPaths();
        QCOMPARE(session.undoName(), QStringLiteral("Create Outlines"));
        const VectorObject *result = session.document()->find(id);
        QCOMPARE(result->kind, ObjectKind::group);
        const std::vector<QUuid> pieces = session.document()->children(id);
        QCOMPARE(int(pieces.size()), 2);
        const VectorObject *red = session.document()->find(pieces[1]);
        QCOMPARE(red->fill, Paint::solid(Qt::red));
        QVERIFY(std::abs(red->path.bounds().left() - redGlyphs.left()) < 0.01);
        // The big run stays big: the outlines fill the same box as the type did.
        const QRectF after = session.document()->bounds(id);
        QVERIFY(std::abs(after.height() - before.height()) < 0.5);
        QVERIFY(std::abs(after.width() - before.width()) < 0.5);
        session.undo();
        QCOMPARE(session.document()->find(id)->kind, ObjectKind::text);
    }

    void aFirstLineIndentBelowZeroHangs()
    {
        VectorObject object = areaObject(QStringLiteral("One two three four five six seven eight nine ten"), 120);
        object.text.leftIndent = 12;
        object.text.firstLineIndent = -12;
        const TextLayout layout(object.text);
        QVERIFY(layout.lines().size() > 2);
        QVERIFY(std::abs(layout.lines()[0].left - 0) < 0.01);
        QVERIFY(std::abs(layout.lines()[1].left - 12) < 0.01);
        QVERIFY(std::abs(layout.lines()[2].left - 12) < 0.01);
    }

    void spaceAfterSeparatesParagraphs()
    {
        VectorObject object = areaObject(QStringLiteral("One\nTwo"), 200);
        const TextLayout plain(object.text);
        object.text.spaceAfter = 6;
        const TextLayout spaced(object.text);
        QVERIFY(std::abs((spaced.lines()[1].baseline - spaced.lines()[0].baseline) - (plain.lines()[1].baseline - plain.lines()[0].baseline) - 6) < 1e-6);
        // Per paragraph: space before on the second alone.
        object.text.spaceAfter = 0;
        object.text.formatParagraphs(1, 1, [](ParagraphFormat &format) { format.spaceBefore = 10; });
        const TextLayout before(object.text);
        QVERIFY(std::abs((before.lines()[1].baseline - before.lines()[0].baseline) - (plain.lines()[1].baseline - plain.lines()[0].baseline) - 10) < 1e-6);
    }

    void justifiedLastLinesCanCentreOrSitRight()
    {
        VectorObject object = areaObject(QStringLiteral("One two three four five six seven eight nine ten eleven"), 150);
        object.text.alignment = TextAlignment::justifyCenter;
        const TextLayout centred(object.text);
        const TextLayout::Line &last = centred.lines().back();
        QVERIFY(std::abs((last.left + last.right) / 2 - 75) < 0.5);
        // Lines above still meet both edges.
        QVERIFY(std::abs(centred.lines().front().right - 150) < 0.5);
        object.text.alignment = TextAlignment::justifyRight;
        QVERIFY(std::abs(TextLayout(object.text).lines().back().right - 150) < 0.5);
        // Paragraphs align apart.
        object.text.alignment = TextAlignment::left;
        object.text.text = QStringLiteral("Left\nRight");
        object.text.formatParagraphs(1, 1, [](ParagraphFormat &format) { format.alignment = TextAlignment::right; });
        const TextLayout apart(object.text);
        QVERIFY(std::abs(apart.lines()[0].left) < 0.5);
        QVERIFY(std::abs(apart.lines()[1].right - 150) < 0.5);
    }

    void paragraphEditsWithARangeChangeItsParagraphs()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject object = areaObject(QStringLiteral("One\nTwo\nThree"), 200);
        const QUuid id = session.addObject(object, QStringLiteral("Type"));
        session.setTextRange(EditorSession::TextRange{id, 5, 6});
        session.updateText([](TextContent &content) { content.spaceBefore = 8; }, QStringLiteral("Space Before"));
        const TextContent &content = session.document()->find(id)->text;
        QCOMPARE(content.paragraphAt(0).spaceBefore, 0.0);
        QCOMPARE(content.paragraphAt(1).spaceBefore, 8.0);
        QCOMPARE(content.paragraphAt(2).spaceBefore, 0.0);
    }

    void openTypeFeaturesChangeTheGlyphs()
    {
        if (!FontFeatures::applicable())
            QSKIP("Qt before 6.7 can't apply OpenType features");
        // A family whose "ffi" in "office" is one ligature glyph.
        for (const QString &family : QFontDatabase::families()) {
            TextContent content = text(QStringLiteral("office"));
            content.family = family;
            if (!FontFeatures::supported(content.font()).contains(QStringLiteral("liga")))
                continue;
            const int joined = TextLayout(content).glyphCount();
            const QPainterPath ligature = content.outline();
            FontFeatures::set(content.features, QStringLiteral("liga"), false);
            const int apart = TextLayout(content).glyphCount();
            if (joined >= apart)
                continue;
            QVERIFY(content.outline() != ligature);
            QCOMPARE(apart, 6);
            // On one word only, as a run.
            TextContent two = text(QStringLiteral("office office"));
            two.family = family;
            two.formatCharacters(7, 13, [](CharacterFormat &format) { FontFeatures::set(format.features, QStringLiteral("liga"), false); });
            QCOMPARE(TextLayout(two).glyphCount(), joined + 1 + 6);
            return;
        }
        QSKIP("no installed font has an ffi ligature");
    }

    void featuresTheFontLacksAreKnown()
    {
        // Every font with a GSUB table lists some features; a made-up tag is never one.
        bool found = false;
        for (const QString &family : QFontDatabase::families()) {
            TextContent content = text(QStringLiteral("x"));
            content.family = family;
            const QSet<QString> tags = FontFeatures::supported(content.font());
            QVERIFY(!tags.contains(QStringLiteral("zzzz")));
            found = found || !tags.isEmpty();
            if (found)
                break;
        }
        QVERIFY(found);
        // Defaults: ligatures on, small caps off; setting a default leaves no entry.
        std::map<QString, int> features;
        QVERIFY(FontFeatures::isOn(features, QStringLiteral("liga")));
        QVERIFY(!FontFeatures::isOn(features, QStringLiteral("smcp")));
        FontFeatures::set(features, QStringLiteral("liga"), true);
        QVERIFY(features.empty());
        FontFeatures::set(features, QStringLiteral("ss03"), true);
        QCOMPARE(FontFeatures::fromCss(FontFeatures::css(features)), features);
    }

    void runsAndParagraphsRoundTripThroughTheCodec()
    {
        VectorDocument document = VectorDocument::blank({400, 300});
        VectorObject object;
        object.kind = ObjectKind::text;
        object.parentID = document.layers().front();
        object.text = text(QStringLiteral("Bold word here\nSecond"));
        object.text.formatCharacters(0, 4, [&](CharacterFormat &format) {
            format.style = boldFace(format.family);
            format.fill = QColor(Qt::red);
            format.features[QStringLiteral("liga")] = 0;
        });
        object.text.formatParagraphs(1, 1, [](ParagraphFormat &format) {
            format.firstLineIndent = -12;
            format.leading = 30;
        });
        object.text.features[QStringLiteral("ss01")] = 1;
        document.objects.push_back(object);
        const QJsonObject json = DocumentCodec::encode(document);
        QCOMPARE(json["version"].toInt(), 4);
        const VectorDocument back = DocumentCodec::decode(json);
        QCOMPARE(back.find(object.id)->text, object.text);
        QCOMPARE(back, document);
    }

    void version2TextOpensUnchanged()
    {
        const QJsonObject json{{"format", "omastrator"}, {"version", 2}, {"width", 400}, {"height", 300},
                               {"objects", QJsonArray{QJsonObject{{"id", "7d3b1b44-5c1c-4a1e-9a57-7b0f9f7d9a01"}, {"kind", "layer"}, {"name", "Layer 1"}},
                                                      QJsonObject{{"id", "7d3b1b44-5c1c-4a1e-9a57-7b0f9f7d9a02"},
                                                                  {"kind", "text"},
                                                                  {"parent", "7d3b1b44-5c1c-4a1e-9a57-7b0f9f7d9a01"},
                                                                  {"text", QJsonObject{{"string", "Hello"}, {"family", "Sans Serif"}, {"style", "Regular"},
                                                                                       {"size", 30}, {"alignment", "center"}, {"trackingEm", 20},
                                                                                       {"leadingPt", 40}, {"spaceAfter", 4}}}}}}};
        const VectorDocument document = DocumentCodec::decode(json);
        const TextContent &content = document.find(QUuid::fromString(QStringLiteral("7d3b1b44-5c1c-4a1e-9a57-7b0f9f7d9a02")))->text;
        QVERIFY(content.runs.empty());
        QVERIFY(content.paragraphFormats.empty());
        QCOMPARE(content.alignment, TextAlignment::center);
        QCOMPARE(content.tracking, 20.0);
        QCOMPARE(content.leading, std::optional<double>(40));
        QCOMPARE(content.spaceAfter, 4.0);
        QVERIFY(content.characterStyle.isNull());
    }
};

QTEST_MAIN(TextRunsTests)
#include "TextRunsTests.moc"
