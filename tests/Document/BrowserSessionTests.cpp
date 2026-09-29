#include "Document/EditorSession.h"
#include <QTest>

// A Browser View's address and picture change without being edits (docs/BROWSER-VIEW.md).
namespace {
struct Board {
    EditorSession session;
    QUuid frame;

    Board()
    {
        VectorDocument document = VectorDocument::blank({1000, 800});
        VectorObject view = VectorObject::frame({0, 0, 400, 300}, QStringLiteral("Site"));
        view.browser = BrowserView{QUrl(QStringLiteral("http://localhost/a")), {}, {}};
        frame = view.id;
        document.insert(view, document.layers().front());
        session.loadDocument(document);
    }
    const BrowserView &view() const { return *session.document()->find(frame)->browser; }
};
}

class BrowserSessionTests : public QObject {
    Q_OBJECT

private slots:
    void anAddressChangeIsNotAnUndoStepButMarksTheFileUnsaved()
    {
        Board board;
        QVERIFY(!board.session.isModified());
        const int steps = board.session.undoNames().size();
        board.session.setBrowserLocation(board.frame, QUrl(QStringLiteral("http://localhost/b")), {0, 40});
        QCOMPARE(board.view().url, QUrl(QStringLiteral("http://localhost/b")));
        QCOMPARE(board.view().scroll, QPointF(0, 40));
        QCOMPARE(board.session.undoNames().size(), steps);
        QVERIFY(board.session.isModified());
    }

    void scrollingAloneAsksNothing()
    {
        Board board;
        board.session.setBrowserLocation(board.frame, board.view().url, {0, 300});
        QCOMPARE(board.view().scroll, QPointF(0, 300));
        QVERIFY(!board.session.isModified());
    }

    void thePictureIsSilent()
    {
        Board board;
        int changes = 0;
        connect(&board.session, &EditorSession::changed, this, [&] { ++changes; });
        QImage picture(400, 300, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::green);
        board.session.setBrowserPicture(board.frame, picture);
        QCOMPARE(board.view().picture.size(), QSize(400, 300));
        QCOMPARE(changes, 0);
        QVERIFY(!board.session.isModified());
    }

    void anUndoAfterANavigationKeepsTheNewAddress()
    {
        Board board;
        board.session.addFrame({700, 0, 50, 50}, QStringLiteral("Later"));
        board.session.setBrowserLocation(board.frame, QUrl(QStringLiteral("http://localhost/b")), {});
        board.session.undo();
        QCOMPARE(board.view().url, QUrl(QStringLiteral("http://localhost/b")));
        board.session.redo();
        QCOMPARE(board.view().url, QUrl(QStringLiteral("http://localhost/b")));
    }

    void framesThatAreNotBrowserViewsAreLeftAlone()
    {
        Board board;
        const QUuid other = board.session.addFrame({800, 0, 10, 10}, QStringLiteral("Plain"));
        board.session.setBrowserLocation(other, QUrl(QStringLiteral("http://localhost/x")), {});
        QVERIFY(!board.session.document()->find(other)->browser);
    }
};

QTEST_MAIN(BrowserSessionTests)
#include "BrowserSessionTests.moc"
