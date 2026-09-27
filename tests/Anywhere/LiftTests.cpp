#include "Anywhere/Lift.h"
#include "FakeDesktop.h"
#include "LiftFixtures.h"
#include "Live/Browser.h"
#include "Live/LiveSession.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

// Lift into vectors (docs/ANYWHERE.md): a real page in headless Chromium read
// from its DOM, a fake accessibility tree, and the traced fallback.
namespace {
const VectorObject *byLift(const VectorDocument &document, const QString &selector, std::optional<ObjectKind> kind = std::nullopt,
                           const QString &name = QString())
{
    for (const VectorObject &object : document.objects) {
        if (object.liftedFrom == selector && (!kind || object.kind == *kind) && (name.isEmpty() || object.name == name))
            return &object;
    }
    return nullptr;
}

bool near(const QRectF &a, const QRectF &b, double tolerance = 1.5)
{
    return std::abs(a.left() - b.left()) <= tolerance && std::abs(a.top() - b.top()) <= tolerance && std::abs(a.width() - b.width()) <= tolerance
           && std::abs(a.height() - b.height()) <= tolerance;
}

QString describe(const QRectF &rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height());
}

bool runs(LiftJob &job)
{
    QElapsedTimer clock;
    clock.start();
    job.start();
    while (job.isRunning() && clock.elapsed() < 60'000)
        QTest::qWait(20);
    return !job.isRunning();
}
}

class LiftTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    std::unique_ptr<LiveSession> m_live;

    bool openPage()
    {
        if (m_live)
            return true;
        if (Browser::executable().isEmpty())
            return false;
        QFile html(m_directory.filePath(QStringLiteral("card.html")));
        if (!html.open(QIODevice::WriteOnly))
            return false;
        html.write(LiftFixtures::page);
        html.close();
        QImage picture(20, 20, QImage::Format_RGB32);
        picture.fill(QColor(0xab, 0xcd, 0xef));
        picture.save(m_directory.filePath(QStringLiteral("pic.png")));
        m_live = std::make_unique<LiveSession>();
        LiveSession::Target target;
        target.url = QUrl::fromLocalFile(html.fileName());
        target.headless = true;
        target.profile = m_directory.filePath(QStringLiteral("profile"));
        if (!m_live->start(target).isEmpty())
            return false;
        QElapsedTimer clock;
        clock.start();
        while (m_live->state() == LiveSession::State::starting && clock.elapsed() < 60'000)
            QTest::qWait(50);
        return m_live->state() == LiveSession::State::running;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("OMASTRATOR_THEME_DIR", m_directory.filePath(QStringLiteral("no-theme")).toUtf8());
    }

    void cleanupTestCase()
    {
        if (m_live)
            m_live->stop();
    }

    void aCardLiftsFromTheDomInPlace()
    {
        if (!openPage())
            QSKIP("Chromium isn't installed, so pages can't be lifted here.");
        // The card's box in the viewport, as the bar would pass it.
        auto job = LiftJob::web(*m_live, true, QRectF(40, 30, 304, 224), QStringLiteral("div#card"));
        QVERIFY(runs(*job));
        QVERIFY2(job->result(), qPrintable(job->error()));
        const Lift::Result &lifted = *job->result();
        const VectorDocument &art = lifted.art;
        QCOMPARE(lifted.method, QStringLiteral("dom"));
        const VectorObject *card = art.find(lifted.root);
        QVERIFY(card);
        QCOMPARE(card->kind, ObjectKind::group);
        QCOMPARE(card->name, QStringLiteral("div \"Pricing card\""));
        QCOMPARE(card->liftedFrom, QStringLiteral("#card"));

        // The box: a live rectangle with the page's corners, the gradient, and the border inside it.
        const VectorObject *background = byLift(art, QStringLiteral("#card"), ObjectKind::path, QStringLiteral("Background"));
        QVERIFY(background);
        QVERIFY2(near(background->outline().boundingRect(), QRectF(40, 30, 304, 224)), qPrintable(describe(background->outline().boundingRect())));
        QVERIFY(background->liveShape());
        QCOMPARE(background->liveShape()->radii[0], 12.0);
        QCOMPARE(background->liveShape()->radii[2], 4.0);
        QCOMPARE(background->fill.kind, PaintKind::linearGradient);
        QCOMPARE(background->fill.stops.front().color, QColor(Qt::red));
        QCOMPARE(background->fill.stops.back().color, QColor(Qt::blue));
        QVERIFY(background->fill.start.x() < background->fill.end.x());
        const VectorObject *border = byLift(art, QStringLiteral("#card"), ObjectKind::path, QStringLiteral("Border"));
        QVERIFY(border);
        QCOMPARE(border->stroke.width, 2.0);
        QCOMPARE(border->stroke.paint.color, QColor(Qt::green));
        QCOMPARE(border->stroke.alignment, StrokeAlignment::inside);

        // Overflow hidden: a clip group inside the card holds the content, and the spilling box is in it.
        const auto children = art.children(lifted.root);
        const auto clip = std::find_if(children.begin(), children.end(), [&](const QUuid &id) { return art.find(id)->isClipGroup; });
        QVERIFY(clip != children.end());
        const VectorObject *spill = byLift(art, QStringLiteral("#spill"));
        QVERIFY(spill);
        QVERIFY(art.isAncestor(*clip, spill->id));
        QVERIFY(near(spill->outline().boundingRect(), QRectF(292, 182, 100, 100)));

        // Text: real font, size and spacing, the bold word a run of its own colour, on the page's baseline.
        const VectorObject *title = byLift(art, QStringLiteral("#title"), ObjectKind::text);
        QVERIFY(title);
        QCOMPARE(title->text.text, QStringLiteral("Plain bold text"));
        QCOMPARE(title->text.family, QStringLiteral("DejaVu Sans"));
        QCOMPARE(title->text.size, 20.0);
        QCOMPARE(title->text.tracking, 50.0);
        QCOMPARE(title->fill.color, QColor(Qt::white));
        QCOMPARE(title->text.runs.size(), size_t(1));
        QCOMPARE(title->text.text.mid(title->text.runs[0].start, title->text.runs[0].length).trimmed(), QStringLiteral("bold"));
        QCOMPARE(title->text.runs[0].format.fill, std::optional<QColor>(QColor(Qt::yellow)));
        QVERIFY(title->text.runs[0].format.isBold());
        QVERIFY(std::abs(title->transform.dx() - 62) < 1.5);
        QVERIFY2(title->transform.dy() > 66 && title->transform.dy() < 80, qPrintable(QString::number(title->transform.dy())));

        // The inline SVG, imported as vectors in its box; the image, placed and cropped to its box.
        const VectorObject *icon = byLift(art, QStringLiteral("#icon"));
        QVERIFY(icon);
        QVERIFY2(near(art.bounds(icon->id), QRectF(62, 102, 40, 40)), qPrintable(describe(art.bounds(icon->id))));
        const VectorObject *square = nullptr;
        for (const QUuid &id : art.descendants(icon->id))
            square = art.find(id)->kind == ObjectKind::path ? art.find(id) : square;
        QVERIFY(square);
        QCOMPARE(square->fill.color, QColor(0x12, 0x34, 0x56));
        const VectorObject *picture = byLift(art, QStringLiteral("#pic"), ObjectKind::image);
        QVERIFY(picture);
        QVERIFY2(near(art.bounds(picture->id), QRectF(122, 102, 40, 40)), qPrintable(describe(art.bounds(picture->id))));
        QCOMPARE(QColor(picture->image.pixel(picture->image.width() / 2, picture->image.height() / 2)), QColor(0xab, 0xcd, 0xef));
        // Paint order: the background under the content.
        QVERIFY(art.indexOf(background->id) < art.indexOf(title->id));
    }

    void aTransformIsKeptAndTheViewportLiftsWhole()
    {
        if (!openPage())
            QSKIP("Chromium isn't installed, so pages can't be lifted here.");
        auto job = LiftJob::web(*m_live, false, QRectF(), QStringLiteral("card.html"));
        QVERIFY(runs(*job));
        QVERIFY2(job->result(), qPrintable(job->error()));
        const VectorDocument &art = job->result()->art;
        // The page's own background under everything.
        const VectorObject *paper = nullptr;
        for (const VectorObject &object : art.objects)
            paper = object.name == QLatin1String("Page background") ? &object : paper;
        QVERIFY(paper);
        QCOMPARE(paper->fill.color, QColor(0xf0, 0xf0, 0xf0));
        // Rotated a quarter turn about its centre: 50 wide and 100 tall around 450, 75.
        const VectorObject *turned = byLift(art, QStringLiteral("#turned"));
        QVERIFY(turned);
        QCOMPARE(turned->kind, ObjectKind::group);
        QVERIFY2(near(art.bounds(turned->id), QRectF(425, 25, 50, 100)), qPrintable(describe(art.bounds(turned->id))));
        QVERIFY(byLift(art, QStringLiteral("#card")));
    }

    void aLiftCanBeCancelled()
    {
        if (!openPage())
            QSKIP("Chromium isn't installed, so pages can't be lifted here.");
        auto job = LiftJob::web(*m_live, false, QRectF(), QStringLiteral("card.html"));
        job->start();
        job->cancel();
        QVERIFY(!job->isRunning());
        QVERIFY(job->wasCancelled());
        QVERIFY(!job->result());
        QTest::qWait(300);
        QVERIFY(!job->result());
    }

    void anAppLiftsFromItsAccessibilityTree()
    {
        FakeDesktop desktop;
        Hyprland::Window &window = desktop.addWindow(QStringLiteral("zenity"), QRect(100, 50, 400, 300), 77);
        desktop.trees.insert(77, QJsonDocument::fromJson(LiftFixtures::tree).object());
        auto job = LiftJob::screen(desktop, window, QRect(), QStringLiteral("zenity"));
        QVERIFY(runs(*job));
        QVERIFY2(job->result(), qPrintable(job->error()));
        const Lift::Result &lifted = *job->result();
        const VectorDocument &art = lifted.art;
        QCOMPARE(lifted.method, QStringLiteral("accessibility"));
        QCOMPARE(art.find(lifted.root)->name, QStringLiteral("zenity"));
        // The window's colour as a rectangle, in window coordinates.
        const VectorObject *paper = byLift(art, QStringLiteral("/frame[0]"), ObjectKind::path);
        QVERIFY(paper);
        QCOMPARE(paper->fill.color, desktop.screenColor);
        QVERIFY(near(paper->outline().boundingRect(), QRectF(0, 0, 400, 300)));
        // Each widget a group named by role and name, its text a text object with the tree's font.
        const VectorObject *button = byLift(art, QStringLiteral("/frame[0]/push button[1]"), ObjectKind::group);
        QVERIFY(button);
        QCOMPARE(button->name, QStringLiteral("push button “OK”"));
        const VectorObject *question = byLift(art, QStringLiteral("/frame[0]/label[0]"), ObjectKind::text);
        QVERIFY(question);
        QCOMPARE(question->text.text, QStringLiteral("Delete the file?"));
        QCOMPARE(question->text.family, QStringLiteral("DejaVu Sans"));
        QVERIFY(std::abs(question->text.size - 11 * 96.0 / 72) < 0.01);
        QCOMPARE(question->fill.color, QColor(0x20, 0x20, 0x20));
        QCOMPARE(question->transform.dx(), 20.0);
        const VectorObject *ok = byLift(art, QStringLiteral("/frame[0]/push button[1]/label[0]"), ObjectKind::text);
        QVERIFY(ok);
        QVERIFY(art.isAncestor(button->id, ok->id));
        // Only a region of the window: the button alone.
        auto part = LiftJob::screen(desktop, window, QRect(290, 240, 100, 50), QStringLiteral("OK"));
        QVERIFY(runs(*part));
        QVERIFY(part->result());
        QVERIFY(!byLift(part->result()->art, QStringLiteral("/frame[0]/label[0]"), ObjectKind::text));
        QVERIFY(byLift(part->result()->art, QStringLiteral("/frame[0]/push button[1]/label[0]"), ObjectKind::text));
    }

    void withNoTreeTheScreenIsTraced()
    {
        FakeDesktop desktop;
        Hyprland::Window &window = desktop.addWindow(QStringLiteral("foot"), QRect(100, 50, 400, 300), 78);
        auto job = LiftJob::screen(desktop, window, QRect(), QStringLiteral("foot"));
        QVERIFY(runs(*job));
        QVERIFY2(job->result(), qPrintable(job->error()));
        const Lift::Result &lifted = *job->result();
        QCOMPARE(lifted.method, QStringLiteral("trace"));
        QCOMPARE(lifted.art.find(lifted.root)->liftedFrom, QStringLiteral("trace"));
        QVERIFY(lifted.objects > 0);
        QVERIFY(lifted.notes.join(' ').contains(QLatin1String("traced")));
        // Traced paths sit over the window, in its own coordinates.
        QVERIFY2(near(lifted.art.bounds(lifted.root), QRectF(0, 0, 400, 300), 2), qPrintable(describe(lifted.art.bounds(lifted.root))));
        QCOMPARE(lifted.art.find(lifted.art.children(lifted.root).front())->fill.color, desktop.screenColor);
        // Nothing on screen to capture: a plain failure.
        desktop.grabFails = true;
        auto failing = LiftJob::screen(desktop, window, QRect(), QStringLiteral("foot"));
        QVERIFY(runs(*failing));
        QVERIFY(!failing->result());
        QVERIFY(!failing->error().isEmpty());
    }

    void theTreeHelperIsValidPython()
    {
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty())
            QSKIP("python3 isn't installed");
        QProcess check;
        check.start(python, {QStringLiteral("-c"), QStringLiteral("import sys; compile(sys.stdin.read(), 'tree', 'exec')")});
        QVERIFY(check.waitForStarted());
        check.write(Lift::accessibleTreeScript().toUtf8());
        check.closeWriteChannel();
        QVERIFY(check.waitForFinished(10000));
        QVERIFY2(check.exitCode() == 0, check.readAllStandardError().constData());
    }
};

QTEST_MAIN(LiftTests)
#include "LiftTests.moc"
