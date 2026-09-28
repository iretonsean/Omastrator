#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QTest>

// Auto layout (docs/AUTO-LAYOUT.md): frames that place their children and size themselves to them.
namespace {
struct Layout {
    EditorSession session;
    VectorDocument document;
    QUuid frame;

    // A frame at (100, 100) with the given sizes as rectangles, bottom to top, laid out as `layout` says.
    Layout(const AutoLayout &layout, const std::vector<QSizeF> &sizes, QSizeF box = {50, 50}, LayoutItem sizing = {LayoutSizing::hug, LayoutSizing::hug})
    {
        document = VectorDocument::blank({600, 600});
        VectorObject made = VectorObject::frame({QPointF(100, 100), box});
        made.autoLayout = layout;
        made.layout = sizing;
        frame = made.id;
        document.insert(made, document.layers().front());
        double x = 300;
        for (const QSizeF &size : sizes) {
            // Live rectangles, as the Rectangle tool draws them.
            VectorObject rect;
            EditorSession::reshape(rect, LiveRectangle{.rect = QRectF(QPointF(x, 400), size), .placement = {}});
            rect.name = QStringLiteral("Item");
            document.insert(rect, frame);
            x += 5;
        }
        session.loadDocument(document);
    }
    const VectorDocument &now() const { return *session.document(); }
    QRectF box() const { return now().find(frame)->shape->rect; }
    QRectF item(size_t index) const { return now().bounds(now().children(frame)[index]); }
};

AutoLayout layout(LayoutDirection direction, double gap = 10, double padding = 10)
{
    AutoLayout result;
    result.direction = direction;
    result.gap = gap;
    result.paddingLeft = result.paddingTop = result.paddingRight = result.paddingBottom = padding;
    return result;
}
}

class AutoLayoutTests : public QObject {
    Q_OBJECT

private slots:
    void aHuggingRowPlacesItsChildrenAndTakesTheirSize()
    {
        Layout row(layout(LayoutDirection::horizontal), {{20, 20}, {30, 10}, {10, 40}});
        QCOMPARE(row.item(0), QRectF(110, 110, 20, 20));
        QCOMPARE(row.item(1), QRectF(140, 110, 30, 10));
        QCOMPARE(row.item(2), QRectF(180, 110, 10, 40));
        // 20 + 30 + 10, two gaps and the padding; the tallest and the padding.
        QCOMPARE(row.box(), QRectF(100, 100, 100, 60));
    }

    void aColumnCentresAcross()
    {
        AutoLayout column = layout(LayoutDirection::vertical, 5, 0);
        column.counter = LayoutAlign::center;
        Layout stack(column, {{40, 10}, {20, 10}});
        QCOMPARE(stack.box(), QRectF(100, 100, 40, 25));
        QCOMPARE(stack.item(0), QRectF(100, 100, 40, 10));
        QCOMPARE(stack.item(1), QRectF(110, 115, 20, 10));
    }

    void aFixedFrameGivesItsRoomToFillChildren()
    {
        Layout row(layout(LayoutDirection::horizontal), {{20, 20}, {30, 10}}, {200, 60}, {LayoutSizing::fixed, LayoutSizing::fixed});
        VectorDocument document = row.now();
        VectorObject *second = document.find(document.children(row.frame)[1]);
        second->layout.width = LayoutSizing::fill;
        second->layout.height = LayoutSizing::fill;
        row.session.loadDocument(document);
        QCOMPARE(row.box(), QRectF(100, 100, 200, 60));
        // 200 less the padding, the first item and the gap.
        QCOMPARE(row.item(1), QRectF(140, 110, 150, 40));
        // A rectangle keeps its live shape at its new size.
        QVERIFY(row.now().find(row.now().children(row.frame)[1])->liveShape());
    }

    void autoSpacingSpreadsTheItems()
    {
        AutoLayout spread = layout(LayoutDirection::horizontal, 0, 0);
        spread.spaceBetween = true;
        Layout row(spread, {{10, 10}, {10, 10}, {10, 10}}, {110, 10}, {LayoutSizing::fixed, LayoutSizing::hug});
        QCOMPARE(row.item(0).left(), 100.0);
        QCOMPARE(row.item(1).left(), 150.0);
        QCOMPARE(row.item(2).left(), 200.0);
    }

    void wrappingRunsOnIntoRows()
    {
        AutoLayout wrapping = layout(LayoutDirection::horizontal, 10, 0);
        wrapping.wrap = true;
        wrapping.counterGap = 6;
        Layout grid(wrapping, {{40, 20}, {40, 30}, {40, 20}}, {100, 10}, {LayoutSizing::fixed, LayoutSizing::hug});
        QCOMPARE(grid.item(0).topLeft(), QPointF(100, 100));
        QCOMPARE(grid.item(1).topLeft(), QPointF(150, 100));
        // 40 + 10 + 40 fills the row; the third starts the next, under the taller one.
        QCOMPARE(grid.item(2).topLeft(), QPointF(100, 136));
        QCOMPARE(grid.box(), QRectF(100, 100, 100, 56));
    }

    void anAbsoluteChildStaysOutOfTheFlow()
    {
        Layout row(layout(LayoutDirection::horizontal), {{20, 20}, {30, 30}, {20, 20}});
        VectorDocument document = row.now();
        const QUuid loose = document.children(row.frame)[1];
        document.find(loose)->layout.absolute = true;
        const QRectF before = document.bounds(loose);
        row.session.loadDocument(document);
        QCOMPARE(row.now().bounds(loose), before);
        QCOMPARE(row.item(2), QRectF(140, 110, 20, 20));
        QCOMPARE(row.box(), QRectF(100, 100, 70, 40));
    }

    void nestedFramesHugFromTheInsideOut()
    {
        VectorDocument document = VectorDocument::blank({600, 600});
        VectorObject outer = VectorObject::frame({0, 0, 10, 10});
        outer.autoLayout = layout(LayoutDirection::vertical, 10, 10);
        outer.layout = {LayoutSizing::hug, LayoutSizing::hug};
        VectorObject inner = VectorObject::frame({300, 300, 10, 10});
        inner.autoLayout = layout(LayoutDirection::horizontal, 4, 2);
        inner.layout = {LayoutSizing::hug, LayoutSizing::hug};
        document.insert(outer, document.layers().front());
        document.insert(inner, outer.id);
        for (int index = 0; index < 2; ++index) {
            VectorObject rect;
            rect.path = Shapes::rectangle({500, 500, 30, 20});
            document.insert(rect, inner.id);
        }
        EditorSession session;
        session.loadDocument(document);
        const VectorDocument &laid = *session.document();
        // The inner row: 30 + 4 + 30 and 2 pt padding; the outer column hugs that with 10 more.
        QCOMPARE(laid.find(inner.id)->shape->rect, QRectF(10, 10, 68, 24));
        QCOMPARE(laid.find(outer.id)->shape->rect, QRectF(0, 0, 88, 44));
    }

    void shiftAWrapsTheSelectionInAHuggingFrame()
    {
        EditorSession session;
        session.createDocument({400, 400});
        const QUuid right = session.addPath(Shapes::rectangle({100, 20, 30, 30}), QStringLiteral("Right"));
        const QUuid left = session.addPath(Shapes::rectangle({20, 24, 40, 20}), QStringLiteral("Left"));
        session.select({right, left});
        session.addAutoLayout();
        QCOMPARE(session.undoName(), QStringLiteral("Add Auto Layout"));
        const QUuid frame = session.selection().front();
        const VectorObject &made = *session.document()->find(frame);
        QVERIFY(made.autoLayout);
        QCOMPARE(made.autoLayout->direction, LayoutDirection::horizontal);
        // The gap they had between them; no padding, since the frame was their bounds.
        QCOMPARE(made.autoLayout->gap, 40.0);
        QCOMPARE(made.autoLayout->paddingLeft, 0.0);
        QCOMPARE(made.fill, Paint::none());
        // In the order they sat, left first.
        QCOMPARE(session.document()->children(frame), (std::vector<QUuid>{left, right}));
        QCOMPARE(session.document()->bounds(left).left(), 20.0);
        QCOMPARE(session.document()->bounds(right).left(), 100.0);
        // An edit re-lays it out: the left one grows, the right one moves along.
        session.select({left});
        session.scaleSelection(1.5, 1);
        QCOMPARE(session.document()->bounds(right).left(), 120.0);
        session.select({frame});
        session.removeAutoLayout();
        QVERIFY(!session.document()->find(frame)->autoLayout);
        QCOMPARE(session.document()->bounds(right).left(), 120.0);
    }

    void autoLayoutSurvivesTheFile()
    {
        AutoLayout saved = layout(LayoutDirection::vertical, 12, 3);
        saved.paddingRight = 7;
        saved.primary = LayoutAlign::end;
        saved.counter = LayoutAlign::center;
        saved.wrap = true;
        saved.counterGap = 4;
        saved.spaceBetween = true;
        Layout column(saved, {{10, 10}});
        VectorDocument document = column.now();
        document.find(document.children(column.frame)[0])->layout = {LayoutSizing::fill, LayoutSizing::fixed, true};
        const VectorDocument read = DocumentCodec::decode(DocumentCodec::encode(document));
        QCOMPARE(*read.find(column.frame)->autoLayout, saved);
        QCOMPARE(read.find(column.frame)->layout, (LayoutItem{LayoutSizing::hug, LayoutSizing::hug, false}));
        QCOMPARE(read.find(read.children(column.frame)[0])->layout, (LayoutItem{LayoutSizing::fill, LayoutSizing::fixed, true}));
    }

    void resizingAFrameMovesItsChildrenByTheirConstraints()
    {
        EditorSession session;
        session.createDocument({600, 600});
        const QUuid pinned = session.addPath(Shapes::rectangle({110, 110, 20, 20}), QStringLiteral("Pinned"));
        const QUuid right = session.addPath(Shapes::rectangle({160, 110, 20, 20}), QStringLiteral("Right"));
        const QUuid stretch = session.addPath(Shapes::rectangle({110, 150, 70, 10}), QStringLiteral("Stretch"));
        const QUuid centred = session.addPath(Shapes::rectangle({135, 170, 20, 20}), QStringLiteral("Centred"));
        const QUuid scaled = session.addPath(Shapes::rectangle({100, 100, 45, 45}), QStringLiteral("Scaled"));
        session.select({pinned, right, stretch, centred, scaled});
        session.frameSelection();
        const QUuid frame = session.selection().front();
        QCOMPARE(session.document()->find(frame)->shape->rect, QRectF(100, 100, 80, 90));
        const auto constrain = [&](const QUuid &id, LayoutConstraint x, LayoutConstraint y) {
            session.select({id});
            session.setConstraint(Qt::Horizontal, x);
            session.setConstraint(Qt::Vertical, y);
        };
        constrain(right, LayoutConstraint::end, LayoutConstraint::start);
        constrain(stretch, LayoutConstraint::both, LayoutConstraint::start);
        constrain(centred, LayoutConstraint::center, LayoutConstraint::end);
        constrain(scaled, LayoutConstraint::scale, LayoutConstraint::scale);
        // The handles: twice as wide, 10 taller, from the top left.
        session.select({frame});
        session.transformSelection(QTransform::fromTranslate(-100, -100) * QTransform::fromScale(2, 100.0 / 90) * QTransform::fromTranslate(100, 100),
                                   QStringLiteral("Scale"), true);
        const VectorDocument &document = *session.document();
        QCOMPARE(document.find(frame)->shape->rect, QRectF(100, 100, 160, 100));
        QCOMPARE(document.bounds(pinned), QRectF(110, 110, 20, 20));
        QCOMPARE(document.bounds(right), QRectF(240, 110, 20, 20));
        QCOMPARE(document.bounds(stretch), QRectF(110, 150, 150, 10));
        QCOMPARE(document.bounds(centred).topLeft(), QPointF(175, 180));
        QCOMPARE(document.bounds(scaled).width(), 90.0);
        // The Scale tool's kind of scaling still scales everything.
        session.transformSelection(QTransform::fromScale(0.5, 0.5), QStringLiteral("Scale"), false);
        QCOMPARE(session.document()->bounds(pinned).size(), QSizeF(10, 10));
    }
};

QTEST_MAIN(AutoLayoutTests)
#include "AutoLayoutTests.moc"
