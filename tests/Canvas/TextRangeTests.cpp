#include "Canvas/EditorCanvas.h"
#include "Canvas/InlineTextEditor.h"
#include <QTest>

// Characters selected in type edited in place: the session styles just them,
// typing carries runs along, and the range goes when editing ends.
class TextRangeTests : public QObject {
    Q_OBJECT

private:
    struct Editing {
        EditorSession session;
        EditorCanvas canvas{session};
        QUuid id;
        explicit Editing(const QString &words)
        {
            session.createDocument({400, 300});
            canvas.resize(800, 600);
            canvas.show();
            if (!QTest::qWaitForWindowExposed(&canvas))
                qWarning("the canvas never showed");
            session.actualSize();
            id = session.addText({100, 100}, words);
            session.selectTool(Tool::text);
            const QRectF box = session.document()->bounds(id);
            QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier,
                              session.viewport.viewPoint(QPointF(box.left() + 1, box.center().y()), session.document()->size).toPoint());
        }
        const TextContent &text() const { return session.document()->find(id)->text; }
    };

private slots:
    void selectedCharactersAreWhatTypeEditsStyle()
    {
        Editing w(QStringLiteral("Hello world"));
        QVERIFY(w.canvas.isEditingText());
        QTest::keyClick(&w.canvas, Qt::Key_End);
        QTest::keyClick(&w.canvas, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(w.session.textRange().has_value());
        QCOMPARE(w.session.textRange()->id, w.id);
        w.session.updateText([](TextContent &text) { text.size = 48; }, QStringLiteral("Font Size"));
        QCOMPARE(w.text().size, 24.0);
        QCOMPARE(w.text().formatAt(8).size, 48.0);
        QCOMPARE(w.text().formatAt(2).size, 24.0);
        QVERIFY(w.canvas.isEditingText());
        // Typed at the run's end, text is as large as the run.
        QTest::keyClick(&w.canvas, Qt::Key_End);
        QTest::keyClicks(&w.canvas, QStringLiteral("s"));
        QCOMPARE(w.text().text, QStringLiteral("Hello worlds"));
        QCOMPARE(w.text().formatAt(11).size, 48.0);
        // And at the start, as the text is.
        QTest::keyClick(&w.canvas, Qt::Key_Home);
        QTest::keyClicks(&w.canvas, QStringLiteral(">"));
        QCOMPARE(w.text().formatAt(0).size, 24.0);
        QCOMPARE(w.text().runs.front().start, 7);
        QTest::keyClick(&w.canvas, Qt::Key_Escape);
        QVERIFY(!w.canvas.isEditingText());
        QVERIFY(!w.session.textRange().has_value());
        QCOMPARE(w.text().runs.front().length, 6);
    }

    void aCaretAloneEditsTheWholeText()
    {
        Editing w(QStringLiteral("Hello"));
        QTest::keyClick(&w.canvas, Qt::Key_Right);
        w.session.updateText([](TextContent &text) { text.tracking = 50; }, QStringLiteral("Tracking"));
        QVERIFY(w.text().runs.empty());
        QCOMPARE(w.text().tracking, 50.0);
    }

    void aColourPickedWhileTypingColoursTheSelection()
    {
        Editing w(QStringLiteral("Hello world"));
        QTest::keyClick(&w.canvas, Qt::Key_Home);
        QTest::keyClick(&w.canvas, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
        w.session.setFillOfSelection(Paint::solid(Qt::red));
        QCOMPARE(w.text().formatAt(0).fill, std::optional<QColor>(QColor(Qt::red)));
        QVERIFY(!w.text().formatAt(8).fill.has_value());
        // The canvas draws the red run red, once the selection's tint is gone.
        QTest::keyClick(&w.canvas, Qt::Key_End);
        const QImage shot = w.canvas.grab().toImage();
        bool red = false;
        for (int y = 0; y < shot.height() && !red; y += 2) {
            for (int x = 0; x < shot.width() && !red; x += 2) {
                const QColor pixel = shot.pixelColor(x, y);
                red = pixel.red() > 200 && pixel.green() < 60 && pixel.blue() < 60;
            }
        }
        QVERIFY(red);
    }
};

QTEST_MAIN(TextRangeTests)
#include "TextRangeTests.moc"
