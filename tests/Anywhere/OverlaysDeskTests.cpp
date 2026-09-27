#include "Anywhere/Desk.h"
#include "Anywhere/Overlays.h"
#include "IO/ProjectStore.h"
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

// Drawing on top of any surface, and the Desk (docs/ANYWHERE.md): the art is
// a real document layer anchored to its surface, every drawing one named undo
// step; the Desk takes frames labelled with their source.
namespace {
Surface window(const QString &app, const QRect &rect)
{
    Surface surface;
    surface.kind = Surface::Kind::window;
    surface.app = app;
    surface.title = app + QStringLiteral(" window");
    surface.rect = rect;
    surface.monitor = QStringLiteral("DP-1");
    surface.key = Surface::keyFor(surface.kind, surface.app, {});
    return surface;
}

OverlayStore::Stroke stroke(const QString &tool, std::vector<QPointF> points, const QString &text = QString())
{
    return OverlayStore::Stroke{tool, std::move(points), text};
}
}

class OverlaysDeskTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
    }

    void drawingIsAnchoredToItsSurfaceAsOneUndoStep()
    {
        OverlayStore overlays;
        QCOMPARE(overlays.load(m_directory.filePath(QStringLiteral("one.omai"))), QString());
        const Surface foot = window(QStringLiteral("foot"), QRect(100, 50, 800, 600));
        QString error;
        const QUuid box = overlays.draw(foot, stroke(QStringLiteral("rectangle"), {{150, 90}, {250, 140}}), &error);
        QVERIFY2(!box.isNull(), qPrintable(error));
        EditorSession &session = overlays.session();
        // One step, named for what and where.
        QCOMPARE(session.undoName(), QStringLiteral("Draw Rectangle on foot"));
        QCOMPARE(overlays.surfaces(), QStringList{QStringLiteral("window:foot")});
        QCOMPARE(overlays.art(QStringLiteral("window:foot")).size(), size_t(1));
        // Stored relative to the window's corner, so it moves with the window.
        QCOMPARE(session.document()->bounds(box), QRectF(50, 40, 100, 50));
        const auto layer = overlays.layer(QStringLiteral("window:foot"));
        QVERIFY(layer);
        QCOMPARE(session.document()->layerOf(box), layer);
        QCOMPARE(session.document()->find(box)->stroke.paint, Paint::solid(OverlayStore::ink()));
        QCOMPARE(session.document()->find(box)->fill, Paint::none());
        session.undo();
        QVERIFY(!overlays.layer(QStringLiteral("window:foot")));
        QVERIFY(overlays.surfaces().isEmpty());
        session.redo();
        QCOMPARE(overlays.art(QStringLiteral("window:foot")).size(), size_t(1));

        // Every tool: an arrow has its head, text and notes their words; a second surface gets its own layer.
        const QUuid arrow = overlays.draw(foot, stroke(QStringLiteral("arrow"), {{120, 60}, {300, 60}}), &error);
        QCOMPARE(session.document()->find(arrow)->stroke.endArrow, Arrowhead::arrow);
        const QUuid pen = overlays.draw(foot, stroke(QStringLiteral("pen"), {{110, 300}, {130, 320}, {160, 310}, {200, 340}}), &error);
        QCOMPARE(session.document()->find(pen)->kind, ObjectKind::path);
        const QUuid text = overlays.draw(foot, stroke(QStringLiteral("text"), {{400, 400}}, QStringLiteral("Too tight")), &error);
        QCOMPARE(session.document()->find(text)->text.text, QStringLiteral("Too tight"));
        QVERIFY(overlays.draw(foot, stroke(QStringLiteral("text"), {{400, 400}}), &error).isNull());
        QCOMPARE(error, QStringLiteral("Type something first."));
        const Surface thunar = window(QStringLiteral("thunar"), QRect(1000, 0, 500, 500));
        const QUuid note = overlays.draw(thunar, stroke(QStringLiteral("note"), {{1010, 10}}, QStringLiteral("Icons are blurry")), &error);
        QCOMPARE(session.undoName(), QStringLiteral("Draw Note on thunar"));
        QCOMPARE(session.document()->find(note)->kind, ObjectKind::group);
        QCOMPARE(session.document()->find(note)->name, QStringLiteral("Note"));
        QCOMPARE(overlays.surfaces().size(), 2);
        QVERIFY(overlays.draw(thunar, stroke(QStringLiteral("shapeBuilder"), {{1, 1}}), &error).isNull());

        // Selecting a surface's art is what the bar and Ask act on.
        QVERIFY(overlays.selectArt(QStringLiteral("window:foot")));
        QCOMPARE(overlays.selectedSurface(), QStringLiteral("window:foot"));
        QCOMPARE(session.selection().size(), size_t(4));
        QVERIFY(!overlays.selectArt(QStringLiteral("window:gimp")));

        // A proposal on show pauses drawing, so it can't be kept by accident.
        session.beginInteraction(QStringLiteral("AI: Mock-up"));
        QVERIFY(overlays.draw(foot, stroke(QStringLiteral("line"), {{0, 0}, {10, 10}}), &error).isNull());
        QVERIFY(error.contains(QLatin1String("proposal")));
        session.cancelInteraction();

        overlays.clear(QStringLiteral("window:thunar"));
        QCOMPARE(session.undoName(), QStringLiteral("Clear Overlay on thunar"));
        QVERIFY(overlays.art(QStringLiteral("window:thunar")).empty());
    }

    void theArtIsKeptAndRenderedForTheShell()
    {
        const QString path = m_directory.filePath(QStringLiteral("kept.omai"));
        {
            OverlayStore overlays;
            overlays.load(path);
            QString error;
            overlays.draw(window(QStringLiteral("foot"), QRect(0, 0, 400, 300)), stroke(QStringLiteral("ellipse"), {{20, 20}, {120, 80}}), &error);
            QCOMPARE(overlays.save(), QString());
        }
        OverlayStore overlays;
        QCOMPARE(overlays.load(path), QString());
        QCOMPARE(overlays.surfaces(), QStringList{QStringLiteral("window:foot")});
        const auto picture = overlays.picture(QStringLiteral("window:foot"), 2);
        QVERIFY(picture);
        QVERIFY(QFileInfo::exists(picture->path));
        QVERIFY(picture->path.startsWith(m_directory.filePath(QStringLiteral("runtime/overlays"))));
        // The picture covers the art with room for its stroke, from the surface's corner.
        QVERIFY(picture->bounds.contains(QRectF(20, 20, 100, 60)));
        const QImage image(picture->path);
        QCOMPARE(image.size(), (picture->bounds.size() * 2).toSize());
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        // Unchanged art isn't drawn again; changed art gets a new file, so the shell's cache can't show the old one.
        QCOMPARE(overlays.picture(QStringLiteral("window:foot"), 2)->path, picture->path);
        QString error;
        overlays.draw(window(QStringLiteral("foot"), QRect(0, 0, 400, 300)), stroke(QStringLiteral("line"), {{0, 0}, {50, 50}}), &error);
        const auto again = overlays.picture(QStringLiteral("window:foot"), 2);
        QVERIFY(again->path != picture->path);
        QVERIFY(!QFileInfo::exists(picture->path));
        QVERIFY(!overlays.picture(QStringLiteral("window:gimp")));

        // The art alone, as the Desk and a new document take it.
        const VectorDocument extracted = overlays.extract(QStringLiteral("window:foot"));
        QCOMPARE(extracted.layers().size(), size_t(1));
        QCOMPARE(extracted.children(extracted.layers().front()).size(), size_t(2));
        QCOMPARE(extracted.find(extracted.layers().front())->name, QStringLiteral("foot"));
    }

    void framesOnTheDeskAreLabelledWithTheirSource()
    {
        EditorSession desk;
        desk.loadDocument(Desk::blank());
        OverlayStore overlays;
        overlays.load(m_directory.filePath(QStringLiteral("desk-art.omai")));
        QString error;
        overlays.draw(window(QStringLiteral("foot"), QRect(0, 0, 400, 300)), stroke(QStringLiteral("rectangle"), {{10, 10}, {60, 40}}), &error);

        Desk::Frame first;
        first.source = QStringLiteral("foot");
        first.time = QDateTime(QDate(2026, 9, 27), QTime(14, 5));
        first.screenshot = QImage(400, 300, QImage::Format_ARGB32);
        first.screenshot.fill(Qt::darkBlue);
        first.art = overlays.extract(QStringLiteral("window:foot"));
        const QUuid one = Desk::addFrame(desk, first, &error);
        QVERIFY2(!one.isNull(), qPrintable(error));
        QCOMPARE(desk.undoName(), QStringLiteral("Send to Desk"));
        auto frames = Desk::frames(*desk.document());
        QCOMPARE(frames.size(), size_t(1));
        QCOMPARE(frames[0].second, QStringLiteral("foot · 14:05"));
        const VectorDocument &document = *desk.document();
        // The screenshot, then the art over it at the same place, then the label above.
        const std::vector<QUuid> children = document.children(one);
        QCOMPARE(children.size(), size_t(4));
        QCOMPARE(document.find(children[1])->kind, ObjectKind::image);
        const QRectF frameBox = document.bounds(children[0]);
        QCOMPARE(frameBox.size(), QSizeF(400, 300));
        QCOMPARE(document.bounds(children[2]).topLeft(), frameBox.topLeft() + QPointF(10, 10));
        QCOMPARE(document.find(children[3])->text.text, QStringLiteral("foot · 14:05"));

        // The next one goes after it; the artboard grows to keep them.
        Desk::Frame second;
        second.source = QStringLiteral("example.com/pricing");
        second.time = QDateTime(QDate(2026, 9, 27), QTime(9, 30));
        second.size = QSizeF(5800, 400);
        const QUuid two = Desk::addFrame(desk, second);
        frames = Desk::frames(*desk.document());
        QCOMPARE(frames.size(), size_t(2));
        QCOMPARE(frames[1].second, QStringLiteral("example.com/pricing · 09:30"));
        // Too wide for the row: a new row under the first.
        QVERIFY(desk.document()->bounds(two).top() > desk.document()->bounds(one).bottom());
        QVERIFY(desk.document()->size.width() >= desk.document()->bounds(two).right());
        desk.undo();
        QCOMPARE(Desk::frames(*desk.document()).size(), size_t(1));

        // Not while something else is being edited on the Desk.
        desk.beginInteraction(QStringLiteral("Move"));
        QVERIFY(Desk::addFrame(desk, second, &error).isNull());
        QVERIFY(error.contains(QLatin1String("Finish")));
        desk.cancelInteraction();
        Desk::Frame empty;
        QVERIFY(!Desk::addFrame(desk, empty, &error).isNull());

        // It saves and opens like any document.
        const QString path = m_directory.filePath(QStringLiteral("desk.omai"));
        ProjectStore::write(*desk.document(), path);
        QCOMPARE(Desk::frames(ProjectStore::read(path)).size(), size_t(2));
        QCOMPARE(Desk::defaultPath(), m_directory.filePath(QStringLiteral("data/omastrator/desk.omai")));
    }
};

QTEST_MAIN(OverlaysDeskTests)
#include "OverlaysDeskTests.moc"
