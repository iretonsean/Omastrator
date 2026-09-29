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
    QRectF box(const QUuid &id) const { return session.document()->bounds(id); }

    // A badge in the frame's top right corner: it follows the right edge (constraint end) unless it is fixed.
    QUuid addBadge(PreviewRule rule = PreviewRule::constraints)
    {
        VectorDocument document = *session.document();
        VectorObject badge = VectorObject::frame({320, 10, 60, 30}, QStringLiteral("Badge"));
        badge.layout.horizontal = LayoutConstraint::end;
        badge.layout.previewRule = rule;
        const QUuid id = badge.id;
        document.insert(badge, frame);
        session.loadDocument(document);
        return id;
    }
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

    void aPreviewIsNeverAHistoryStep()
    {
        Board board;
        const QUuid badge = board.addBadge();
        const auto steps = board.session.undoNames();
        board.session.beginPreview(QStringLiteral("Preview Width"));
        QVERIFY(board.session.isPreviewOnly());
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 200, 300));
        // The badge follows the right edge, so it moves 200 left.
        QCOMPARE(board.box(badge).x(), 120.0);
        board.session.previewFrameBox(board.frame, {0, 0, 390, 300});
        QCOMPARE(board.box(badge).x(), 310.0);
        // What the design says stays what it says.
        QCOMPARE(board.session.designBox(board.frame), QRectF(0, 0, 400, 300));
        board.session.cancelInteraction();
        QVERIFY(!board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));
        QCOMPARE(board.box(badge).x(), 320.0);
        QVERIFY(board.session.undoNames() == steps);
        QVERIFY(!board.session.isModified());
    }

    void committingAPreviewRecordsNothing()
    {
        Board board;
        const auto steps = board.session.undoNames();
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.commitInteraction();
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));
        QVERIFY(board.session.undoNames() == steps);
    }

    void anEditUndoOrToolChangeEndsAPreview()
    {
        Board board;
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.addFrame({700, 0, 50, 50}, QStringLiteral("Later"));
        QVERIFY(!board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));

        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.undo();
        QVERIFY(!board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));

        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.selectTool(Tool::rectangle);
        QVERIFY(!board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));
    }

    void theBrowseToolKeepsAHeldPreview()
    {
        Board board;
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.selectTool(Tool::browse);
        QVERIFY(board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame).width(), 200.0);
    }

    void aFixedChildStaysWhereTheDesignPutItInThePreview()
    {
        Board board;
        const QUuid badge = board.addBadge(PreviewRule::fixed);
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        QCOMPARE(board.box(badge).x(), 320.0);
        board.session.cancelInteraction();
        // The out-of-the-flow flag was on the preview's copy only.
        QVERIFY(!board.session.document()->find(badge)->layout.absolute);
    }

    void theDesignWidthIsOneNamedStepAndKeepsTheChildrenOnTheirConstraints()
    {
        Board board;
        const QUuid badge = board.addBadge();
        board.session.select({board.frame});
        const auto before = board.session.undoNames();
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.setPreviewAsDesignWidth();
        QVERIFY(!board.session.isPreviewOnly());
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 200, 300));
        QCOMPARE(board.box(badge).x(), 120.0);
        QCOMPARE(board.session.undoNames().size(), before.size() + 1);
        QCOMPARE(board.session.undoNames().back(), QStringLiteral("Design Width"));
        board.session.undo();
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));
        QCOMPARE(board.box(badge).x(), 320.0);
    }

    void settingTheDesignWidthToItsOwnWidthMakesNoStep()
    {
        Board board;
        const auto before = board.session.undoNames();
        board.session.setDesignBox(board.frame, {0, 0, 400, 300});
        board.session.setPreviewAsDesignWidth();
        QVERIFY(board.session.undoNames() == before);
    }

    void onlyABrowserViewTakesADesignWidth()
    {
        Board board;
        const QUuid plain = board.session.addFrame({800, 0, 100, 100}, QStringLiteral("Plain"));
        const auto before = board.session.undoNames();
        board.session.setDesignBox(plain, {800, 0, 50, 100});
        QCOMPARE(board.box(plain).width(), 100.0);
        QVERIFY(board.session.undoNames() == before);
    }

    void fixedWhilePreviewingIsOneStepOnTheSelectedChild()
    {
        Board board;
        const QUuid badge = board.addBadge();
        board.session.select({badge});
        const auto before = board.session.undoNames();
        board.session.setFixedWhilePreviewing(true);
        QCOMPARE(board.session.document()->find(badge)->layout.previewRule, PreviewRule::fixed);
        QCOMPARE(board.session.undoNames().size(), before.size() + 1);
        QCOMPARE(board.session.undoNames().back(), QStringLiteral("Fixed While Previewing"));
        board.session.undo();
        QCOMPARE(board.session.document()->find(badge)->layout.previewRule, PreviewRule::constraints);
    }

    void anAddressChangeDuringAPreviewSurvivesItsEnd()
    {
        Board board;
        board.session.beginPreview(QStringLiteral("Preview Width"));
        board.session.previewFrameBox(board.frame, {0, 0, 200, 300});
        board.session.setBrowserLocation(board.frame, QUrl(QStringLiteral("http://localhost/b")), {0, 90});
        board.session.cancelInteraction();
        QCOMPARE(board.view().url, QUrl(QStringLiteral("http://localhost/b")));
        QCOMPARE(board.view().scroll, QPointF(0, 90));
        QCOMPARE(board.box(board.frame), QRectF(0, 0, 400, 300));
    }
};

QTEST_MAIN(BrowserSessionTests)
#include "BrowserSessionTests.moc"
