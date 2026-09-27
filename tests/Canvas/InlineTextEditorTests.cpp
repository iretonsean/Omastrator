#include "Canvas/EditorCanvas.h"
#include "Canvas/InlineTextEditor.h"
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTest>

// Type edited in place: word moves and deletes, selections by click count, and the preedit.
namespace {
InlineTextEditor editorWith(const QString &text, int caret)
{
    VectorObject object;
    object.kind = ObjectKind::text;
    object.text.text = text;
    InlineTextEditor editor(object);
    editor.caret = editor.anchor = caret;
    return editor;
}

InlineTextEditor::Result press(InlineTextEditor &editor, int key, Qt::KeyboardModifiers modifiers)
{
    const QKeyEvent event(QEvent::KeyPress, key, modifiers);
    return editor.keyPress(event);
}
}

class InlineTextEditorTests : public QObject {
    Q_OBJECT

private slots:
    void ctrlArrowsJumpWords()
    {
        InlineTextEditor editor = editorWith(QStringLiteral("one two, three"), 0);
        press(editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(editor.caret, 4);
        press(editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(editor.caret, 7);
        press(editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(editor.caret, 9);
        press(editor, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(editor.caret, 14);
        press(editor, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(editor.caret, 9);
        QCOMPARE(editor.anchor, 9);
        // Shift extends the selection a word at a time.
        press(editor, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        press(editor, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(editor.caret, 4);
        QCOMPARE(editor.anchor, 9);
        QVERIFY(InlineTextEditor::claims(QKeyEvent(QEvent::KeyPress, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier)));
    }

    void ctrlBackspaceAndDeleteTakeWords()
    {
        InlineTextEditor editor = editorWith(QStringLiteral("one two three"), 7);
        QCOMPARE(press(editor, Qt::Key_Backspace, Qt::ControlModifier), InlineTextEditor::Result::edited);
        QCOMPARE(editor.text(), QStringLiteral("one  three"));
        QCOMPARE(editor.caret, 4);
        press(editor, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(editor.text(), QStringLiteral("one three"));
        press(editor, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(editor.text(), QStringLiteral("one "));
        // With a selection, only the selection goes.
        editor.anchor = 0;
        editor.caret = 2;
        press(editor, Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(editor.text(), QStringLiteral("e "));
    }

    void ctrlASelectsAllAndClicksSelectWordsAndLines()
    {
        InlineTextEditor editor = editorWith(QStringLiteral("hello big\nworld"), 2);
        press(editor, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(std::min(editor.caret, editor.anchor), 0);
        QCOMPARE(std::max(editor.caret, editor.anchor), 15);
        editor.selectWord(7);
        QCOMPARE(editor.anchor, 6);
        QCOMPARE(editor.caret, 9);
        editor.selectLine(12);
        QCOMPARE(editor.anchor, 10);
        QCOMPARE(editor.caret, 15);
    }

    void preeditPushesTheFollowingTextAlong()
    {
        InlineTextEditor editor = editorWith(QStringLiteral("abcd"), 2);
        QInputMethodEvent composing(QStringLiteral("xy"), {});
        QCOMPARE(editor.inputMethod(composing), InlineTextEditor::Result::moved);
        QCOMPARE(editor.text(), QStringLiteral("abcd"));
        QCOMPARE(editor.displayText(), QStringLiteral("abxycd"));
        QCOMPARE(editor.displayObject().text.text, QStringLiteral("abxycd"));
        QInputMethodEvent committed;
        committed.setCommitString(QStringLiteral("XY"));
        editor.inputMethod(committed);
        QCOMPARE(editor.text(), QStringLiteral("abXYcd"));
        QCOMPARE(editor.displayText(), QStringLiteral("abXYcd"));
    }

    void doubleAndTripleClicksOnTheCanvas()
    {
        EditorSession session;
        EditorCanvas canvas(session);
        session.createDocument({400, 300});
        canvas.resize(800, 600);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        session.actualSize();
        const QUuid id = session.addText({100, 100}, QStringLiteral("alpha beta\ngamma"));
        const auto view = [&](QPointF document) { return session.viewport.viewPoint(document, session.document()->size).toPoint(); };
        session.selectTool(Tool::text);
        const QRectF box = session.document()->bounds(id);
        // A point inside "alpha" on the first line.
        const QPointF inFirstWord(box.left() + 8, 100 - 6);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(inFirstWord));
        QVERIFY(canvas.isEditingText());
        QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(inFirstWord));
        QTest::keyClicks(&canvas, QStringLiteral("Z"));
        QCOMPARE(session.document()->find(id)->text.text, QStringLiteral("Z beta\ngamma"));
        // Double, then a third click: the line.
        QTest::mouseDClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(inFirstWord));
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, view(inFirstWord));
        QTest::keyClicks(&canvas, QStringLiteral("Q"));
        QCOMPARE(session.document()->find(id)->text.text, QStringLiteral("Q\ngamma"));
        // The preedit draws without touching the document.
        QInputMethodEvent composing(QStringLiteral("long preedit"), {});
        QApplication::sendEvent(&canvas, &composing);
        QVERIFY(!canvas.grab().isNull());
        QCOMPARE(session.document()->find(id)->text.text, QStringLiteral("Q\ngamma"));
        canvas.finishTextEditing();
    }
};

QTEST_MAIN(InlineTextEditorTests)
#include "InlineTextEditorTests.moc"
