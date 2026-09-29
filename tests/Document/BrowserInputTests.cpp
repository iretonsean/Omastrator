#include "Document/BrowserInput.h"
#include <QJsonArray>
#include <QTest>

// What the Browse tool sends a page (docs/BROWSER-VIEW.md, section 5), without a browser.
class BrowserInputTests : public QObject {
    Q_OBJECT
private slots:
    void aPointIsCssPixelsFromTheFramesCorner();
    void modifiersFollowChromiumsBits();
    void aMousePressCarriesButtonCountAndModifiers();
    void theWheelIsInvertedAndOneUnitIsOnePixel();
    void lettersDigitsAndPunctuationKeepTheirCodes();
    void namedKeysCarryTheirVirtualKeyCodes();
    void aTypedKeyCarriesItsTextAndAShortcutDoesNot();
    void controlShortcutsAskForTheirEditingCommand();
    void escapeAndControlKAreNeverTheirs();
    void keysWithoutAnAnswerAreLeftOut();
};

void BrowserInputTests::aPointIsCssPixelsFromTheFramesCorner()
{
    const QRectF box(100, 40, 1280, 800);
    QCOMPARE(BrowserInput::cssPoint(QPointF(100, 40), box), QPointF(0, 0));
    QCOMPARE(BrowserInput::cssPoint(QPointF(740, 440), box), QPointF(640, 400));
    // A preview box that is narrower maps the same way: the page fills it one to one.
    QCOMPARE(BrowserInput::cssPoint(QPointF(190, 90), QRectF(100, 40, 390, 800)), QPointF(90, 50));
    // Outside the frame the point goes negative or past the edge, so a drag can leave and come back.
    QCOMPARE(BrowserInput::cssPoint(QPointF(50, 0), box), QPointF(-50, -40));
}

void BrowserInputTests::modifiersFollowChromiumsBits()
{
    QCOMPARE(BrowserInput::cdpModifiers({}), 0);
    QCOMPARE(BrowserInput::cdpModifiers(Qt::AltModifier), 1);
    QCOMPARE(BrowserInput::cdpModifiers(Qt::ControlModifier), 2);
    QCOMPARE(BrowserInput::cdpModifiers(Qt::MetaModifier), 4);
    QCOMPARE(BrowserInput::cdpModifiers(Qt::ShiftModifier), 8);
    QCOMPARE(BrowserInput::cdpModifiers(Qt::ControlModifier | Qt::ShiftModifier), 10);
}

void BrowserInputTests::aMousePressCarriesButtonCountAndModifiers()
{
    const QJsonObject press = BrowserInput::mouseParams(QStringLiteral("mousePressed"), QPointF(12.5, 30), Qt::LeftButton, Qt::LeftButton, 2, Qt::ShiftModifier);
    QCOMPARE(press["type"].toString(), QStringLiteral("mousePressed"));
    QCOMPARE(press["x"].toDouble(), 12.5);
    QCOMPARE(press["y"].toDouble(), 30.0);
    QCOMPARE(press["button"].toString(), QStringLiteral("left"));
    QCOMPARE(press["buttons"].toInt(), 1);
    QCOMPARE(press["clickCount"].toInt(), 2);
    QCOMPARE(press["modifiers"].toInt(), 8);
    // A release has the button up in the mask but still names it.
    const QJsonObject release = BrowserInput::mouseParams(QStringLiteral("mouseReleased"), QPointF(1, 1), Qt::LeftButton, Qt::NoButton, 1, {});
    QCOMPARE(release["button"].toString(), QStringLiteral("left"));
    QCOMPARE(release["buttons"].toInt(), 0);
    // A hover names no button.
    const QJsonObject hover = BrowserInput::mouseParams(QStringLiteral("mouseMoved"), QPointF(1, 1), Qt::NoButton, Qt::NoButton, 0, {});
    QCOMPARE(hover["button"].toString(), QStringLiteral("none"));
    QCOMPARE(BrowserInput::mouseParams(QStringLiteral("mouseMoved"), {}, Qt::MiddleButton, Qt::MiddleButton | Qt::RightButton, 1, {})["buttons"].toInt(), 6);
}

void BrowserInputTests::theWheelIsInvertedAndOneUnitIsOnePixel()
{
    // A notch down is a negative angle in Qt and a positive delta on the page.
    QJsonObject wheel = BrowserInput::wheelParams(QPointF(10, 20), QPoint(0, -120), QPoint(), {});
    QCOMPARE(wheel["type"].toString(), QStringLiteral("mouseWheel"));
    QCOMPARE(wheel["deltaY"].toDouble(), 120.0);
    QCOMPARE(wheel["deltaX"].toDouble(), 0.0);
    QCOMPARE(wheel["x"].toDouble(), 10.0);
    // A trackpad's pixels win over its angle.
    wheel = BrowserInput::wheelParams(QPointF(), QPoint(30, 60), QPoint(-4, 9), Qt::ControlModifier);
    QCOMPARE(wheel["deltaX"].toDouble(), 4.0);
    QCOMPARE(wheel["deltaY"].toDouble(), -9.0);
    QCOMPARE(wheel["modifiers"].toInt(), 2);
}

void BrowserInputTests::lettersDigitsAndPunctuationKeepTheirCodes()
{
    auto key = BrowserInput::keyFor(Qt::Key_A, QStringLiteral("a"), {});
    QVERIFY(key);
    QCOMPARE(key->key, QStringLiteral("a"));
    QCOMPARE(key->code, QStringLiteral("KeyA"));
    QCOMPARE(key->windowsVirtualKeyCode, 65);
    // Shift makes the key a capital and the code stays the letter's.
    key = BrowserInput::keyFor(Qt::Key_A, QStringLiteral("A"), Qt::ShiftModifier);
    QCOMPARE(key->key, QStringLiteral("A"));
    QCOMPARE(key->code, QStringLiteral("KeyA"));
    // With Ctrl held Qt's text is a control character: the key comes from the letter.
    key = BrowserInput::keyFor(Qt::Key_C, QString(QChar(3)), Qt::ControlModifier);
    QCOMPARE(key->key, QStringLiteral("c"));
    QCOMPARE(key->windowsVirtualKeyCode, 67);
    key = BrowserInput::keyFor(Qt::Key_5, QStringLiteral("5"), {});
    QCOMPARE(key->code, QStringLiteral("Digit5"));
    QCOMPARE(key->windowsVirtualKeyCode, 53);
    // Shift+1 is "!" on the key that is Digit1.
    key = BrowserInput::keyFor(Qt::Key_Exclam, QStringLiteral("!"), Qt::ShiftModifier);
    QCOMPARE(key->key, QStringLiteral("!"));
    QCOMPARE(key->code, QStringLiteral("Digit1"));
    QCOMPARE(key->windowsVirtualKeyCode, 49);
    key = BrowserInput::keyFor(Qt::Key_Period, QStringLiteral("."), {});
    QCOMPARE(key->code, QStringLiteral("Period"));
    QCOMPARE(key->windowsVirtualKeyCode, 190);
    key = BrowserInput::keyFor(Qt::Key_Greater, QStringLiteral(">"), Qt::ShiftModifier);
    QCOMPARE(key->code, QStringLiteral("Period"));
    QCOMPARE(key->key, QStringLiteral(">"));
}

void BrowserInputTests::namedKeysCarryTheirVirtualKeyCodes()
{
    struct Row {
        int qt;
        const char *key;
        int vk;
    };
    for (const Row &row : {Row{Qt::Key_Return, "Enter", 13}, Row{Qt::Key_Backspace, "Backspace", 8}, Row{Qt::Key_Tab, "Tab", 9},
                           Row{Qt::Key_Delete, "Delete", 46}, Row{Qt::Key_Left, "ArrowLeft", 37}, Row{Qt::Key_Down, "ArrowDown", 40},
                           Row{Qt::Key_Home, "Home", 36}, Row{Qt::Key_PageDown, "PageDown", 34}, Row{Qt::Key_F5, "F5", 116},
                           Row{Qt::Key_Space, " ", 32}, Row{Qt::Key_Shift, "Shift", 16}}) {
        const auto key = BrowserInput::keyFor(row.qt, {}, {});
        QVERIFY2(key, row.key);
        QCOMPARE(key->key, QString::fromLatin1(row.key));
        QCOMPARE(key->windowsVirtualKeyCode, row.vk);
    }
}

void BrowserInputTests::aTypedKeyCarriesItsTextAndAShortcutDoesNot()
{
    QJsonObject down = *BrowserInput::keyParams(true, Qt::Key_H, QStringLiteral("h"), {}, false);
    QCOMPARE(down["type"].toString(), QStringLiteral("keyDown"));
    QCOMPARE(down["text"].toString(), QStringLiteral("h"));
    QCOMPARE(down["key"].toString(), QStringLiteral("h"));
    QVERIFY(!down.contains("autoRepeat"));
    QJsonObject up = *BrowserInput::keyParams(false, Qt::Key_H, QStringLiteral("h"), {}, false);
    QCOMPARE(up["type"].toString(), QStringLiteral("keyUp"));
    QVERIFY(!up.contains("text"));
    // Enter types a carriage return; Backspace and Tab type nothing.
    down = *BrowserInput::keyParams(true, Qt::Key_Return, QStringLiteral("\r"), {}, false);
    QCOMPARE(down["text"].toString(), QStringLiteral("\r"));
    down = *BrowserInput::keyParams(true, Qt::Key_Backspace, QStringLiteral("\b"), {}, false);
    QCOMPARE(down["type"].toString(), QStringLiteral("rawKeyDown"));
    QVERIFY(!down.contains("text"));
    down = *BrowserInput::keyParams(true, Qt::Key_Tab, QStringLiteral("\t"), {}, false);
    QCOMPARE(down["type"].toString(), QStringLiteral("rawKeyDown"));
    // With Ctrl or Alt held a letter is a shortcut, so it carries no text.
    down = *BrowserInput::keyParams(true, Qt::Key_B, QString(QChar(2)), Qt::ControlModifier, false);
    QCOMPARE(down["type"].toString(), QStringLiteral("rawKeyDown"));
    QVERIFY(!down.contains("text"));
    QCOMPARE(down["modifiers"].toInt(), 2);
    down = *BrowserInput::keyParams(true, Qt::Key_H, QStringLiteral("h"), {}, true);
    QVERIFY(down["autoRepeat"].toBool());
}

void BrowserInputTests::controlShortcutsAskForTheirEditingCommand()
{
    struct Row {
        int qt;
        Qt::KeyboardModifiers mods;
        const char *command;
    };
    for (const Row &row : {Row{Qt::Key_A, Qt::ControlModifier, "selectAll"}, Row{Qt::Key_C, Qt::ControlModifier, "copy"},
                           Row{Qt::Key_X, Qt::ControlModifier, "cut"}, Row{Qt::Key_V, Qt::ControlModifier, "paste"},
                           Row{Qt::Key_Z, Qt::ControlModifier, "undo"}, Row{Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier, "redo"},
                           Row{Qt::Key_Y, Qt::ControlModifier, "redo"}}) {
        const QJsonObject down = *BrowserInput::keyParams(true, row.qt, QString(), row.mods, false);
        QCOMPARE(down["commands"].toArray().at(0).toString(), QString::fromLatin1(row.command));
    }
    // A plain letter asks for nothing, and neither does Alt+A.
    QVERIFY(!BrowserInput::keyParams(true, Qt::Key_A, QStringLiteral("a"), {}, false)->contains("commands"));
    QVERIFY(!BrowserInput::keyParams(true, Qt::Key_A, QString(), Qt::AltModifier | Qt::ControlModifier, false)->contains("commands"));
}

void BrowserInputTests::escapeAndControlKAreNeverTheirs()
{
    QVERIFY(BrowserInput::reserved(Qt::Key_Escape, {}));
    QVERIFY(BrowserInput::reserved(Qt::Key_Escape, Qt::ShiftModifier));
    QVERIFY(BrowserInput::reserved(Qt::Key_K, Qt::ControlModifier));
    QVERIFY(BrowserInput::reserved(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier));
    QVERIFY(!BrowserInput::reserved(Qt::Key_K, {}));
    QVERIFY(!BrowserInput::reserved(Qt::Key_K, Qt::ShiftModifier));
    QVERIFY(!BrowserInput::reserved(Qt::Key_Z, Qt::ControlModifier));
    QVERIFY(!BrowserInput::reserved(Qt::Key_Return, {}));
}

void BrowserInputTests::keysWithoutAnAnswerAreLeftOut()
{
    QVERIFY(!BrowserInput::keyFor(Qt::Key_MediaPlay, {}, {}));
    QVERIFY(!BrowserInput::keyParams(true, Qt::Key_MediaPlay, {}, {}, false));
}

QTEST_MAIN(BrowserInputTests)
#include "BrowserInputTests.moc"
