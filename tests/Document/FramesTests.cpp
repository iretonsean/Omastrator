#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include "Rendering/VectorRenderer.h"
#include <QJsonDocument>
#include <QTest>

// Frames (Figma's): a container with a box of its own that paints, clips and nests.
namespace {
QColor pixel(const VectorDocument &document, QPointF at)
{
    return VectorRenderer::render(document, 1, false).pixelColor(at.toPoint());
}

bool close(const QColor &a, const QColor &b, int tolerance = 12)
{
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance && std::abs(a.blue() - b.blue()) <= tolerance;
}

struct Board {
    EditorSession session;
    QUuid frame;
    QUuid child;

    // A red 100 pt frame at (20, 20) holding a blue square that runs 40 pt past its corner.
    Board()
    {
        session.createDocument({200, 200});
        child = session.addPath(Shapes::rectangle({80, 80, 80, 80}), QStringLiteral("Square"));
        session.setFillsOfSelection({Paint::solid(Qt::blue)}, QStringLiteral("Fill"));
        session.setStrokesOfSelection({}, QStringLiteral("Stroke"));
        frame = session.addFrame({20, 20, 100, 100});
        session.setFillsOfSelection({Paint::solid(Qt::red)}, QStringLiteral("Fill"));
        edit([&](VectorDocument &document) { document.move(child, frame, 0); });
    }
    template <typename Change> void edit(Change change)
    {
        VectorDocument document = *session.document();
        change(document);
        session.loadDocument(document);
    }
    const VectorDocument &document() const { return *session.document(); }
    const VectorObject &read(const QUuid &id) const { return *document().find(id); }
};
}

class FramesTests : public QObject {
    Q_OBJECT

private slots:
    void aFramePaintsUnderItsChildrenAndClipsThem()
    {
        Board board;
        QCOMPARE(board.read(board.child).parentID, board.frame);
        QVERIFY(close(pixel(board.document(), {40, 40}), Qt::red));
        QVERIFY(close(pixel(board.document(), {100, 100}), Qt::blue));
        // Past the box the square is clipped away.
        QVERIFY(close(pixel(board.document(), {140, 140}), Qt::white));
        QCOMPARE(board.document().bounds(board.frame), QRectF(20, 20, 100, 100));
        board.session.select({board.frame});
        QVERIFY(board.session.selectedFramesClip());
        board.session.setClipsContent(false);
        QVERIFY(!board.read(board.frame).clipsContent);
        QVERIFY(close(pixel(board.document(), {140, 140}), Qt::blue));
        QCOMPARE(board.document().bounds(board.frame), QRectF(20, 20, 140, 140));
    }

    void aFramesStrokesDrawOverItsChildren()
    {
        Board board;
        board.session.select({board.frame});
        StrokeStyle edge;
        edge.paint = Paint::solid(Qt::green);
        edge.width = 8;
        board.session.setStrokesOfSelection({edge}, QStringLiteral("Stroke"));
        // Where the square meets the frame's right edge, the stroke is on top.
        QVERIFY(close(pixel(board.document(), {120, 100}), Qt::green));
    }

    void aFrameSurvivesTheFileAndMovesWithItsChildren()
    {
        Board board;
        board.session.select({board.frame});
        board.session.setClipsContent(false);
        const VectorDocument read = DocumentCodec::decode(DocumentCodec::encode(board.document()));
        const VectorObject &frame = *read.find(board.frame);
        QCOMPARE(frame.kind, ObjectKind::frame);
        QCOMPARE(frame.shape->rect, QRectF(20, 20, 100, 100));
        QVERIFY(!frame.clipsContent);
        QCOMPARE(frame.fill, Paint::solid(Qt::red));
        QCOMPARE(read.find(board.child)->parentID, board.frame);
        // A frame without its box is refused rather than read wrong.
        QJsonObject broken = DocumentCodec::encode(board.read(board.frame));
        broken.remove("shape");
        QVERIFY_THROWS_EXCEPTION(CodecError, DocumentCodec::decodeObject(broken));
        board.session.moveSelection({10, 5});
        QCOMPARE(board.read(board.frame).shape->rect, QRectF(30, 25, 100, 100));
        QCOMPARE(board.read(board.frame).path.bounds(), QRectF(30, 25, 100, 100));
        QCOMPARE(board.document().bounds(board.child), QRectF(90, 85, 80, 80));
    }

    void clicksFollowFigmasRules()
    {
        Board board;
        const VectorDocument &document = board.document();
        // Inside the square: the square, and a top-level frame lets the click through to it.
        QCOMPARE(document.hitTest({100, 100}, 1), board.child);
        QCOMPARE(document.selectableObject(board.child), board.child);
        // The frame's empty part: the frame.
        QCOMPARE(document.hitTest({40, 40}, 1), board.frame);
        QCOMPARE(document.selectableObject(board.frame), board.frame);
        // What's clipped away can't be clicked.
        QCOMPARE(document.hitTest({140, 140}, 1), std::nullopt);
        // A frame nested in the top-level one is picked whole, as a group is.
        const QUuid inner = board.session.addFrame({60, 60, 50, 50});
        QCOMPARE(board.read(inner).parentID, board.frame);
        board.edit([&](VectorDocument &edited) { edited.move(board.child, inner, 0); });
        QCOMPARE(board.document().selectableObject(board.child), inner);
    }

    void frameSelectionWrapsAndRemoveFrameReleases()
    {
        EditorSession session;
        session.createDocument({200, 200});
        const QUuid a = session.addPath(Shapes::rectangle({10, 10, 20, 20}), QStringLiteral("A"));
        const QUuid b = session.addPath(Shapes::rectangle({50, 40, 30, 30}), QStringLiteral("B"));
        session.select({a, b});
        session.frameSelection();
        QCOMPARE(session.undoName(), QStringLiteral("Frame Selection"));
        QCOMPARE(session.selection().size(), size_t(1));
        const QUuid frame = session.selection().front();
        const VectorDocument &document = *session.document();
        QCOMPARE(document.find(frame)->kind, ObjectKind::frame);
        QCOMPARE(document.find(frame)->shape->rect, QRectF(10, 10, 70, 60));
        QCOMPARE(document.children(frame), (std::vector<QUuid>{a, b}));
        // A selected frame answers for its own paint, not its children's.
        QCOMPARE(session.selectedLeaves(), std::vector<QUuid>{frame});
        session.setFillsOfSelection({Paint::solid(Qt::yellow)}, QStringLiteral("Fill"));
        QCOMPARE(session.document()->find(frame)->fill, Paint::solid(Qt::yellow));
        QVERIFY(session.document()->find(a)->fill != Paint::solid(Qt::yellow));
        QVERIFY(session.canUngroup());
        session.ungroupSelection();
        QVERIFY(!session.document()->find(frame));
        QCOMPARE(session.document()->find(a)->parentID, session.document()->layers().front());
    }

    void cornersOnAFrameStayLive()
    {
        Board board;
        board.session.select({board.frame});
        board.session.setCornerRadius(12);
        QCOMPARE(board.read(board.frame).shape->radii[0], 12.0);
        QVERIFY(board.read(board.frame).liveShape());
        // The rounded corner clips too: its very corner shows the paper.
        QVERIFY(close(pixel(board.document(), {21, 21}), Qt::white));
    }

    void aPresetNameWithPercentPlaceholdersNumbersIntact()
    {
        EditorSession session;
        session.createDocument({200, 200});
        for (const QString &name : {QStringLiteral("50%1 off"), QStringLiteral("A %2 B")}) {
            session.addFrame({0, 0, 10, 10}, name);
            const QUuid second = session.addFrame({0, 0, 10, 10}, name);
            QCOMPARE(session.document()->find(second)->name, name + QStringLiteral(" 2"));
        }
    }
};

QTEST_MAIN(FramesTests)
#include "FramesTests.moc"
