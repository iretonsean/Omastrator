#include "Document/DocumentCodec.h"
#include "Document/EditorSession.h"
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

// Ruler guides, isolation, the key object and distribute spacing, and the History panel's steps.
namespace {
struct Fixture {
    EditorSession session;
    Fixture() { session.createDocument({400, 300}); }
    QUuid box(const QRectF &rect) { return session.addPath(Shapes::rectangle(rect), QStringLiteral("Box")); }
    QRectF bounds(const QUuid &id) const { return session.document()->bounds(id); }
};
}

class GuidesAlignHistoryTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void cleanup() { QSettings().clear(); }

    void guidesAreUndoableAndSaved()
    {
        Fixture f;
        f.session.addGuide({Qt::Horizontal, 120});
        QCOMPARE(f.session.undoName(), QString("Add Guide"));
        f.session.addGuide({Qt::Vertical, 40.5});
        f.session.moveGuide(0, 130);
        QCOMPARE(f.session.undoName(), QString("Move Guide"));
        QCOMPARE(f.session.document()->guides.front().position, 130.0);
        // Through text, as a .omai holds it.
        const QByteArray saved = QJsonDocument(DocumentCodec::encode(*f.session.document())).toJson();
        const VectorDocument loaded = DocumentCodec::decode(QJsonDocument::fromJson(saved).object());
        QCOMPARE(loaded.guides, f.session.document()->guides);
        QCOMPARE(loaded.guides.back().orientation, Qt::Vertical);
        f.session.removeGuide(1);
        QCOMPARE(f.session.document()->guides.size(), size_t(1));
        f.session.clearGuides();
        QVERIFY(f.session.document()->guides.empty());
        QCOMPARE(f.session.undoName(), QString("Clear Guides"));
        f.session.undo();
        QCOMPARE(f.session.document()->guides.size(), size_t(1));
    }

    void pathsBecomeGuidesAndBack()
    {
        Fixture f;
        const QUuid box = f.box({10, 20, 100, 50});
        const QUuid line = f.session.addPath(Shapes::line({0, 200}, {300, 200}), QStringLiteral("Line"));
        f.session.select({box, line});
        QVERIFY(f.session.canMakeGuides());
        f.session.makeGuides();
        QCOMPARE(f.session.undoName(), QString("Make Guides"));
        QVERIFY(!f.session.document()->find(box) && !f.session.document()->find(line));
        // The box's four edges, and the straight line's one.
        const std::vector<Guide> expected{{Qt::Vertical, 10}, {Qt::Vertical, 110}, {Qt::Horizontal, 20}, {Qt::Horizontal, 70}, {Qt::Horizontal, 200}};
        QCOMPARE(f.session.document()->guides, expected);
        f.session.releaseGuides();
        QVERIFY(f.session.document()->guides.empty());
        QCOMPARE(f.session.selection().size(), size_t(5));
        QCOMPARE(f.bounds(f.session.selection().back()), QRectF(0, 200, 400, 0));
    }

    void isolationKeepsNewObjectsInside()
    {
        Fixture f;
        const QUuid a = f.box({10, 10, 40, 40});
        const QUuid b = f.box({100, 10, 40, 40});
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid group = f.session.selection().front();
        const QUuid outside = f.box({200, 10, 40, 40});
        f.session.isolate(group);
        QCOMPARE(f.session.isolatedGroup(), std::optional(group));
        QVERIFY(f.session.selection().empty());
        // New objects go in the group, even with nothing selected.
        const QUuid added = f.box({10, 100, 40, 40});
        QCOMPARE(f.session.document()->find(added)->parentID, std::optional(group));
        // Select All and marquees stay inside.
        f.session.selectAll();
        QCOMPARE(f.session.selection(), (std::vector<QUuid>{a, b, added}));
        const std::vector<QUuid> marquee = f.session.objectsIn({0, 0, 400, 300}, false);
        QCOMPARE(marquee, (std::vector<QUuid>{a, b, added}));
        QVERIFY(std::find(marquee.begin(), marquee.end(), outside) == marquee.end());
        f.session.exitIsolation();
        QVERIFY(f.session.isolation().empty());
        QCOMPARE(f.session.selection(), std::vector<QUuid>{group});
        // Ungrouping it away ends the isolation too.
        f.session.isolate(group);
        f.session.select({group});
        f.session.ungroupSelection();
        QVERIFY(f.session.isolation().empty());
    }

    void nestedIsolationStepsOut()
    {
        Fixture f;
        const QUuid a = f.box({10, 10, 40, 40});
        const QUuid b = f.box({100, 10, 40, 40});
        f.session.select({a, b});
        f.session.groupSelection();
        const QUuid inner = f.session.selection().front();
        const QUuid c = f.box({200, 10, 40, 40});
        f.session.select({inner, c});
        f.session.groupSelection();
        const QUuid outer = f.session.selection().front();
        f.session.isolate(inner);
        QCOMPARE(f.session.isolation(), (std::vector<QUuid>{outer, inner}));
        f.session.exitIsolation(1);
        QCOMPARE(f.session.isolation(), std::vector<QUuid>{outer});
        QCOMPARE(f.session.selection(), std::vector<QUuid>{inner});
    }

    void theKeyObjectHoldsStill()
    {
        Fixture f;
        const QUuid a = f.box({10, 10, 40, 40});
        const QUuid key = f.box({100, 60, 40, 40});
        const QUuid c = f.box({200, 30, 60, 20});
        f.session.select({a, key, c});
        f.session.setKeyObject(key);
        QCOMPARE(f.session.keyObject(), std::optional(key));
        f.session.align(AlignEdge::left);
        QCOMPARE(f.bounds(key).left(), 100.0);
        QCOMPARE(f.bounds(a).left(), 100.0);
        QCOMPARE(f.bounds(c).left(), 100.0);
        f.session.undo();
        // To the artboard ignores it.
        f.session.align(AlignEdge::top, AlignTarget::artboard);
        QCOMPARE(f.bounds(key).top(), 0.0);
        // A new selection drops it.
        f.session.select({a, c});
        QVERIFY(!f.session.keyObject());
        // Only a selected object, of two or more, can be key.
        f.session.setKeyObject(key);
        QVERIFY(!f.session.keyObject());
    }

    void distributeSpacingLeavesTheGap()
    {
        Fixture f;
        const QUuid a = f.box({10, 10, 40, 40});
        const QUuid b = f.box({70, 10, 20, 40});
        const QUuid c = f.box({200, 10, 30, 40});
        f.session.select({c, a, b});
        f.session.distributeSpacing(DistributeAxis::horizontal, 12);
        QCOMPARE(f.session.undoName(), QString("Distribute Spacing"));
        QCOMPARE(f.bounds(a).left(), 10.0);
        QCOMPARE(f.bounds(b).left() - f.bounds(a).right(), 12.0);
        QCOMPARE(f.bounds(c).left() - f.bounds(b).right(), 12.0);
        f.session.undo();
        // Measured from the key object, which stays put.
        f.session.setKeyObject(c);
        f.session.distributeSpacing(DistributeAxis::horizontal, 12);
        QCOMPARE(f.bounds(c).left(), 200.0);
        QCOMPARE(f.bounds(c).left() - f.bounds(b).right(), 12.0);
        QCOMPARE(f.bounds(b).left() - f.bounds(a).right(), 12.0);
        f.session.undo();
        // Auto: even gaps between the outermost two.
        f.session.distributeSpacing(DistributeAxis::horizontal, std::nullopt);
        QCOMPARE(f.bounds(a).left(), 10.0);
        QCOMPARE(f.bounds(c).right(), 230.0);
        QCOMPARE(f.bounds(b).left() - f.bounds(a).right(), f.bounds(c).left() - f.bounds(b).right());
    }

    void distributeByEdges()
    {
        Fixture f;
        const QUuid a = f.box({0, 10, 40, 40});
        const QUuid b = f.box({50, 10, 20, 40});
        const QUuid c = f.box({200, 10, 60, 40});
        f.session.select({a, b, c});
        f.session.distribute(AlignEdge::right);
        QCOMPARE(f.bounds(a).right(), 40.0);
        QCOMPARE(f.bounds(c).right(), 260.0);
        QCOMPARE(f.bounds(b).right(), 150.0);
        f.session.distribute(AlignEdge::left);
        QCOMPARE(f.bounds(b).left(), 100.0);
    }

    void historyStepsLikeUndo()
    {
        Fixture f;
        for (int index = 0; index < 5; ++index)
            f.box({10.0 * index, 10, 5, 5});
        QCOMPARE(f.session.undoNames().size(), size_t(5));
        QVERIFY(f.session.redoNames().empty());
        // Three rows up is three Ctrl+Z.
        f.session.stepHistory(-3);
        QCOMPARE(f.session.undoNames().size(), size_t(2));
        QCOMPARE(f.session.redoNames(), (std::vector<QString>{"Draw Box", "Draw Box", "Draw Box"}));
        Fixture g;
        for (int index = 0; index < 5; ++index)
            g.box({10.0 * index, 10, 5, 5});
        g.session.undo();
        g.session.undo();
        g.session.undo();
        QCOMPARE(f.session.document()->children(*f.session.activeLayer()).size(), g.session.document()->children(*g.session.activeLayer()).size());
        f.session.stepHistory(2);
        QCOMPARE(f.session.redoNames().size(), size_t(1));
        // A new edit drops what's left to redo.
        f.session.addGuide({Qt::Vertical, 5});
        QVERIFY(f.session.redoNames().empty());
        QCOMPARE(f.session.undoNames().back(), QString("Add Guide"));
    }

    void theHistoryLimitIsAPreference()
    {
        EditorSession::setHistoryLimit(3);
        QCOMPARE(EditorSession::historyLimit(), 3);
        Fixture f;
        for (int index = 0; index < 6; ++index)
            f.box({10.0 * index, 10, 5, 5});
        QCOMPARE(f.session.undoNames().size(), size_t(3));
        EditorSession::setHistoryLimit(100);
        QCOMPARE(QSettings().value("historyLimit").toInt(), 100);
    }
};

QTEST_MAIN(GuidesAlignHistoryTests)
#include "GuidesAlignHistoryTests.moc"
