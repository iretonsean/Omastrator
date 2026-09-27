#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QFontDatabase>
#include <QTest>

// Character and paragraph styles (new, apply, redefine, overrides, delete) and
// Find/Replace Font (missing families, one-step replacement).
class TextStylesTests : public QObject {
    Q_OBJECT

private:
    static QUuid addText(EditorSession &session, const QString &words, double size = 24, QPointF at = {20, 100})
    {
        VectorObject object = session.textObject(at, words);
        object.text.size = size;
        return session.addObject(object, QStringLiteral("Type"));
    }

    static const TextContent &textOf(const EditorSession &session, const QUuid &id) { return session.document()->find(id)->text; }

private slots:
    void aNewStyleComesFromTheSelectionAndIsApplied()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, QStringLiteral("Heading"), 36);
        const QUuid style = session.newTextStyle(TextStyleKind::character);
        QCOMPARE(session.undoName(), QStringLiteral("New Character Style"));
        QCOMPARE(int(session.document()->textStyles.size()), 1);
        QCOMPARE(session.textStyle(style)->name, QStringLiteral("Character Style 1"));
        QCOMPARE(session.textStyle(style)->character.size, 36.0);
        QCOMPARE(textOf(session, id).characterStyle, style);
        bool overridden = true;
        QCOMPARE(session.shownTextStyle(TextStyleKind::character, &overridden), std::optional<QUuid>(style));
        QVERIFY(!overridden);
        // A second one gets the next number.
        session.newTextStyle(TextStyleKind::character);
        QCOMPARE(session.document()->textStyles.back().name, QStringLiteral("Character Style 2"));
        session.undo();
        session.undo();
        QVERIFY(session.document()->textStyles.empty());
        QVERIFY(textOf(session, id).characterStyle.isNull());
    }

    void applyingToARangeStylesOnlyThoseCharacters()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid source = addText(session, QStringLiteral("Loud"), 48);
        const QUuid loud = session.newTextStyle(TextStyleKind::character, QStringLiteral("Loud"));
        const QUuid id = addText(session, QStringLiteral("quiet then loud"));
        session.setTextRange(EditorSession::TextRange{id, 11, 15});
        session.applyTextStyle(loud);
        QCOMPARE(session.undoName(), QStringLiteral("Apply Style"));
        QCOMPARE(textOf(session, id).formatAt(12).size, 48.0);
        QCOMPARE(textOf(session, id).formatAt(12).characterStyle, loud);
        QCOMPARE(textOf(session, id).formatAt(2).size, 24.0);
        QVERIFY(textOf(session, id).formatAt(2).characterStyle.isNull());
        Q_UNUSED(source);
    }

    void aLocalOverrideShowsAndClears()
    {
        EditorSession session;
        session.createDocument({400, 300});
        addText(session, QStringLiteral("Body"));
        const QUuid body = session.newTextStyle(TextStyleKind::character, QStringLiteral("Body"));
        session.updateText([](TextContent &text) { text.tracking = 80; }, QStringLiteral("Tracking"));
        bool overridden = false;
        QCOMPARE(session.shownTextStyle(TextStyleKind::character, &overridden), std::optional<QUuid>(body));
        QVERIFY(overridden);
        // Colour isn't part of a style: no override.
        session.clearTextOverrides(TextStyleKind::character);
        QCOMPARE(session.undoName(), QStringLiteral("Clear Overrides"));
        session.shownTextStyle(TextStyleKind::character, &overridden);
        QVERIFY(!overridden);
        QCOMPARE(session.shownText().tracking, 0.0);
        // Detached, the look stays and the name goes.
        session.updateText([](TextContent &text) { text.size = 50; }, QStringLiteral("Font Size"));
        session.clearTextStyle(TextStyleKind::character);
        QVERIFY(!session.shownTextStyle(TextStyleKind::character).has_value());
        QCOMPARE(session.shownText().size, 50.0);
    }

    void redefiningUpdatesEveryUseInOneStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid first = addText(session, QStringLiteral("One"), 20);
        const QUuid style = session.newTextStyle(TextStyleKind::character, QStringLiteral("Label"));
        const QUuid second = addText(session, QStringLiteral("Two"), 30, {20, 200});
        session.applyTextStyle(style);
        // A third keeps its own tracking, an override the redefinition leaves alone.
        const QUuid third = addText(session, QStringLiteral("Three"), 30, {20, 250});
        session.applyTextStyle(style);
        session.updateText([](TextContent &text) { text.tracking = 40; }, QStringLiteral("Tracking"));
        QCOMPARE(textOf(session, second).size, 20.0);
        session.select({first});
        session.updateText([](TextContent &text) {
            text.size = 64;
            text.textCase = TextCase::allCaps;
        }, QStringLiteral("Font Size"));
        bool overridden = false;
        session.shownTextStyle(TextStyleKind::character, &overridden);
        QVERIFY(overridden);
        session.redefineTextStyle(style);
        QCOMPARE(session.undoName(), QStringLiteral("Redefine Style"));
        QCOMPARE(session.textStyle(style)->character.size, 64.0);
        for (const QUuid &id : {first, second, third}) {
            QCOMPARE(textOf(session, id).size, 64.0);
            QCOMPARE(textOf(session, id).textCase, TextCase::allCaps);
        }
        QCOMPARE(textOf(session, third).tracking, 40.0);
        session.shownTextStyle(TextStyleKind::character, &overridden);
        QVERIFY(!overridden);
        session.undo();
        QCOMPARE(textOf(session, second).size, 20.0);
        QCOMPARE(textOf(session, first).size, 64.0);
        QCOMPARE(session.textStyle(style)->character.size, 20.0);
    }

    void paragraphStylesSetParagraphsAndTheirUnstyledCharacters()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject made = session.textObject({20, 40}, QStringLiteral("Intro"));
        made.text.area = QSizeF(300, 0);
        made.text.size = 18;
        made.text.spaceAfter = 6;
        made.text.firstLineIndent = 10;
        session.addObject(made, QStringLiteral("Type"));
        const QUuid intro = session.newTextStyle(TextStyleKind::paragraph, QStringLiteral("Intro"));
        QCOMPARE(session.textStyle(intro)->paragraph.spaceAfter, 6.0);
        VectorObject other = session.textObject({20, 150}, QStringLiteral("First\nSecond emphasis"));
        other.text.area = QSizeF(300, 0);
        const QUuid id = session.addObject(other, QStringLiteral("Type"));
        // An emphasis character style on one word, which the paragraph style must leave alone.
        session.setTextRange(EditorSession::TextRange{id, 13, 21});
        session.updateText([](TextContent &text) { text.size = 40; }, QStringLiteral("Font Size"));
        const QUuid emphasis = session.newTextStyle(TextStyleKind::character, QStringLiteral("Emphasis"));
        session.setTextRange(EditorSession::TextRange{id, 7, 9});
        session.applyTextStyle(intro);
        const TextContent &text = textOf(session, id);
        QCOMPARE(text.paragraphAt(0).spaceAfter, 0.0);
        QCOMPARE(text.paragraphAt(1).spaceAfter, 6.0);
        QCOMPARE(text.paragraphAt(1).firstLineIndent, 10.0);
        QCOMPARE(text.paragraphAt(1).paragraphStyle, intro);
        QCOMPARE(text.formatAt(7).size, 18.0);
        QCOMPARE(text.formatAt(14).size, 40.0);
        QCOMPARE(text.formatAt(14).characterStyle, emphasis);
        QCOMPARE(text.formatAt(1).size, 24.0);
        // Redefining the paragraph style reaches the paragraph and its plain characters only.
        session.setTextRange(std::nullopt);
        session.select({session.document()->children(session.document()->layers().front()).front()});
        session.updateText([](TextContent &content) { content.size = 22; }, QStringLiteral("Font Size"));
        session.redefineTextStyle(intro);
        const TextContent &after = textOf(session, id);
        QCOMPARE(after.formatAt(7).size, 22.0);
        QCOMPARE(after.formatAt(14).size, 40.0);
        QCOMPARE(after.formatAt(1).size, 24.0);
    }

    void deletingAStyleKeepsTheLook()
    {
        EditorSession session;
        session.createDocument({400, 300});
        const QUuid id = addText(session, QStringLiteral("Kept"), 44);
        const QUuid style = session.newTextStyle(TextStyleKind::character);
        session.deleteTextStyle(style);
        QCOMPARE(session.undoName(), QStringLiteral("Delete Style"));
        QVERIFY(session.document()->textStyles.empty());
        QVERIFY(textOf(session, id).characterStyle.isNull());
        QCOMPARE(textOf(session, id).size, 44.0);
    }

    void stylesRoundTripThroughTheCodec()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject made = session.textObject({20, 40}, QStringLiteral("Styled"));
        made.text.area = QSizeF(200, 0);
        made.text.leftIndent = 5;
        made.text.features[QStringLiteral("smcp")] = 1;
        session.addObject(made, QStringLiteral("Type"));
        session.newTextStyle(TextStyleKind::paragraph, QStringLiteral("Lead"));
        session.newTextStyle(TextStyleKind::character, QStringLiteral("Caps"));
        const VectorDocument back = DocumentCodec::decode(DocumentCodec::encode(*session.document()));
        QCOMPARE(back.textStyles, session.document()->textStyles);
        QCOMPARE(back, *session.document());
    }

    void missingFontsAreListedAndReplacedInOneStep()
    {
        EditorSession session;
        session.createDocument({400, 300});
        VectorObject made = session.textObject({20, 40}, QStringLiteral("Lost font here"));
        made.text.family = QStringLiteral("Helvetica Neue Condensed Black Imaginary");
        made.text.style = QStringLiteral("Bold");
        const QUuid first = session.addObject(made, QStringLiteral("Type"));
        made.id = QUuid::createUuid();
        const QUuid second = session.addObject(made, QStringLiteral("Type"));
        const QUuid mixed = addText(session, QStringLiteral("Some lost"), 24, {20, 200});
        session.setTextRange(EditorSession::TextRange{mixed, 5, 9});
        session.updateText([&](TextContent &text) { text.family = made.text.family; }, QStringLiteral("Font"));
        session.setTextRange(std::nullopt);
        QVERIFY(session.usedFonts().contains(made.text.family));
        QCOMPARE(session.missingFonts(), QStringList{made.text.family});
        QVERIFY(EditorSession::isFontInstalled(QStringLiteral("Sans Serif")));
        const QString installed = QFontDatabase::families().front();
        const size_t before = session.document()->objects.size();
        QCOMPARE(session.replaceFont(made.text.family, installed), 3);
        QCOMPARE(session.undoName(), QStringLiteral("Replace Font"));
        QCOMPARE(session.document()->objects.size(), before);
        QVERIFY(session.missingFonts().isEmpty());
        for (const QUuid &id : {first, second})
            QCOMPARE(textOf(session, id).family, installed);
        QCOMPARE(textOf(session, mixed).formatAt(6).family, installed);
        // The face nearest the old one: bold stays bold where the family has it.
        QVERIFY(textOf(session, first).isBold());
        session.undo();
        QCOMPARE(session.missingFonts(), QStringList{made.text.family});
        QCOMPARE(textOf(session, mixed).formatAt(6).family, made.text.family);
        // Within the selection only.
        session.select({first});
        QCOMPARE(session.replaceFont(made.text.family, installed, true), 1);
        QCOMPARE(textOf(session, second).family, made.text.family);
        // Find selects every text that uses a family.
        session.selectTextsUsing(made.text.family);
        QCOMPARE(session.selection(), (std::vector<QUuid>{second, mixed}));
    }
};

QTEST_MAIN(TextStylesTests)
#include "TextStylesTests.moc"
