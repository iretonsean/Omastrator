#include "Canvas/BrowserViewHost.h"
#include "Canvas/EditorCanvas.h"
#include "Document/BrowserAddress.h"
#include "Document/EditorSession.h"
#include "UI/BrowserViews.h"
#include <QLineEdit>
#include <QSignalSpy>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

// The Browser View tool, its address bar and the sign-in strip (docs/BROWSER-VIEW.md, section 4). None of it needs
// Chromium: the canvas talks to a fake host, and signing in runs a fake browser script.
namespace {
class FakeHost : public BrowserViewHost {
public:
    QImage picture(const QUuid &) const override { return {}; }
    QString message(const QUuid &) const override { return {}; }
    Bar bar(const QUuid &) const override { return state; }
    void act(const QUuid &frame, Action action) override { acts.emplace_back(frame, action); }
    QList<int> breakpoints(const QUuid &) const override { return widths; }
    bool signInOffered() const override { return offered; }
    void signIn() override { ++signIns; offered = false; }
    void dismissSignIn() override { ++dismissals; offered = false; }

    Bar state;
    QList<int> widths{390, 768, 1280, 1440};
    std::vector<std::pair<QUuid, Action>> acts;
    bool offered = false;
    int signIns = 0;
    int dismissals = 0;
};

struct Rig {
    EditorSession session;
    EditorCanvas canvas{session};
    FakeHost host;
    QUuid frame;

    Rig()
    {
        session.loadDocument(VectorDocument::blank({4000, 3000}));
        canvas.resize(1000, 800);
        canvas.show();
        // Zoomed in enough that a 600-wide frame's bar isn't collapsed.
        session.zoomToRect(QRectF(50, 100, 900, 700));
        canvas.setBrowserViewHost(&host);
    }
    QPointF view(QPointF document) const { return canvas.documentToView().map(document); }
    const VectorObject *object(const QUuid &id) const { return session.document()->find(id); }
    QLineEdit *editor()
    {
        auto *edit = canvas.findChild<QLineEdit *>(QStringLiteral("browserAddressEdit"));
        return edit && edit->isVisible() ? edit : nullptr;
    }
    // A Browser View whose bar is wide enough to hold its buttons at the current zoom.
    void add(QRectF rect = {100, 200, 600, 400})
    {
        frame = session.addBrowserView(rect, QUrl(QStringLiteral("https://example.com/a")));
        session.deselectAll();
    }
    // The middle of the breakpoint button for `width`, laid out from the bar's right end as the canvas does.
    QPoint button(int width, QList<int> all)
    {
        const VectorObject *object = this->object(frame);
        const QRectF box = canvas.documentToView().mapRect(object->path.painterPath().boundingRect());
        const int design = int(std::lround(session.designBox(frame).width()));
        if (!all.contains(design))
            all.append(design);
        std::sort(all.begin(), all.end());
        QFont font = canvas.font();
        font.setPixelSize(11);
        const QFontMetricsF metrics(font);
        const auto size = [&](int each) { return metrics.horizontalAdvance(QString::number(each)) + 14; };
        double total = -2;
        for (int each : all)
            total += size(each) + 2;
        double x = box.right() - 2 - total;
        for (int each : all) {
            if (each == width)
                return QPoint(int(x + size(each) / 2), int(box.top() - 4 - 14));
            x += size(each) + 2;
        }
        return {};
    }
    void click(QPoint at) { QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, at); }
    void draw(QPointF from, QPointF to)
    {
        session.selectTool(Tool::browserView);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, view(from).toPoint());
        QTest::mouseMove(&canvas, view((from + to) / 2).toPoint());
        QTest::mouseMove(&canvas, view(to).toPoint());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, view(to).toPoint());
    }
};
}

class BrowserBarTests : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_directory;

private slots:
    // Before any EditorSession: QSettings caches its paths on first use, so a later change would reach the real config.
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
        qputenv("OMASTRATOR_RUNTIME_DIR", m_directory.filePath(QStringLiteral("runtime")).toUtf8());
        qputenv("XDG_DATA_HOME", m_directory.filePath(QStringLiteral("data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", m_directory.filePath(QStringLiteral("config")).toUtf8());
    }

    void settingsLiveInTheTemporaryFolder()
    {
        QVERIFY2(QSettings().fileName().startsWith(m_directory.path()), qPrintable(QSettings().fileName()));
    }

    void anAddressIsWhatPeopleType_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("expected");
        QTest::newRow("full") << "https://example.com/a?b=1" << "https://example.com/a?b=1";
        QTest::newRow("bare host") << "example.com" << "https://example.com";
        QTest::newRow("padding") << "  example.com/x  " << "https://example.com/x";
        QTest::newRow("localhost") << "localhost:5173" << "http://localhost:5173";
        QTest::newRow("loopback") << "127.0.0.1:8080/app" << "http://127.0.0.1:8080/app";
        QTest::newRow("http kept") << "http://example.com" << "http://example.com";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("spaces inside") << "not a url" << "";
        QTest::newRow("file") << "file:///etc/passwd" << "";
        QTest::newRow("javascript") << "javascript:alert(1)" << "";
        QTest::newRow("data") << "data:text/html,hi" << "";
        QTest::newRow("chrome") << "chrome://settings" << "";
        QTest::newRow("no host") << "https://" << "";
    }

    void anAddressIsWhatPeopleType()
    {
        QFETCH(QString, typed);
        QFETCH(QString, expected);
        const std::optional<QUrl> url = BrowserAddress::parse(typed);
        QCOMPARE(url ? url->toString() : QString(), expected);
    }

    void theBarShowsTheHostAndPathNotTheScheme()
    {
        QCOMPARE(BrowserAddress::shown(QUrl(QStringLiteral("https://www.example.com/"))), QStringLiteral("example.com"));
        QCOMPARE(BrowserAddress::shown(QUrl(QStringLiteral("http://localhost:5173/docs?x=1"))), QStringLiteral("localhost:5173/docs?x=1"));
    }

    void drawingAViewIsOneUndoStepAndNestsInTheFrameItLandsIn()
    {
        Rig rig;
        const QUuid outer = rig.session.addFrame({0, 0, 2000, 2000});
        const size_t steps = rig.session.undoNames().size();
        const QUuid inner = rig.session.addBrowserView({100, 100, 800, 600});
        QCOMPARE(rig.session.undoNames().size(), steps + 1);
        QCOMPARE(rig.session.undoNames().back(), QStringLiteral("Draw Browser View"));
        QVERIFY(rig.object(inner)->browser.has_value());
        QVERIFY(rig.object(inner)->name.startsWith(QStringLiteral("Browser View")));
        QCOMPARE(rig.object(inner)->parentID, std::optional<QUuid>(outer));
        QCOMPARE(rig.session.selection(), std::vector<QUuid>{inner});
        rig.session.undo();
        QVERIFY(!rig.object(inner));
    }

    void changingTheUrlIsOneStepAndUndoGoesBack()
    {
        Rig rig;
        rig.add();
        rig.session.setBrowserLocation(rig.frame, QUrl(QStringLiteral("https://example.com/a")), {0, 300});
        const size_t steps = rig.session.undoNames().size();
        rig.session.setBrowserUrl(rig.frame, QUrl(QStringLiteral("https://example.org/")));
        QCOMPARE(rig.session.undoNames().size(), steps + 1);
        QCOMPARE(rig.session.undoNames().back(), QStringLiteral("Change URL"));
        QCOMPARE(rig.object(rig.frame)->browser->url, QUrl(QStringLiteral("https://example.org/")));
        QCOMPARE(rig.object(rig.frame)->browser->scroll, QPointF());
        rig.session.undo();
        QCOMPARE(rig.object(rig.frame)->browser->url, QUrl(QStringLiteral("https://example.com/a")));
        // The same address again is no step.
        const size_t before = rig.session.undoNames().size();
        rig.session.setBrowserUrl(rig.frame, QUrl(QStringLiteral("https://example.com/a")));
        QCOMPARE(rig.session.undoNames().size(), before);
    }

    void draggingWithTheToolDrawsAViewAndAsksForItsAddress()
    {
        Rig rig;
        rig.draw({200, 300}, {900, 800});
        QVERIFY(rig.session.selectedBrowserView().has_value());
        const QUuid frame = *rig.session.selectedBrowserView();
        const QRectF bounds = rig.object(frame)->path.bounds();
        QVERIFY(qAbs(bounds.width() - 700) < 3 && qAbs(bounds.height() - 500) < 3);
        QVERIFY(qAbs(bounds.left() - 200) < 3 && qAbs(bounds.top() - 300) < 3);
        QCOMPARE(rig.session.undoNames().back(), QStringLiteral("Draw Browser View"));
        QCOMPARE(rig.session.tool(), Tool::select);
        QVERIFY(rig.editor());
        QCOMPARE(rig.editor()->placeholderText(), QStringLiteral("Type a URL"));
        QVERIFY(rig.canvas.isEditingAddress());
    }

    void aClickWithTheToolDrops1280By800()
    {
        Rig rig;
        rig.session.selectTool(Tool::browserView);
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, rig.view({300, 400}).toPoint());
        QVERIFY(rig.session.selectedBrowserView().has_value());
        const QRectF bounds = rig.object(*rig.session.selectedBrowserView())->path.bounds();
        QCOMPARE(bounds.size(), QSizeF(1280, 800));
        QCOMPARE(rig.session.tool(), Tool::select);
        QVERIFY(rig.editor());
    }

    void enterInTheAddressFieldChangesTheUrl()
    {
        Rig rig;
        rig.draw({200, 300}, {900, 800});
        const QUuid frame = *rig.session.selectedBrowserView();
        QLineEdit *edit = rig.editor();
        QVERIFY(edit);
        QTest::keyClicks(edit, QStringLiteral("localhost:5173/x"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(rig.object(frame)->browser->url, QUrl(QStringLiteral("http://localhost:5173/x")));
        QCOMPARE(rig.session.undoNames().back(), QStringLiteral("Change URL"));
        QTRY_VERIFY(!rig.editor());
    }

    void aRefusedAddressSaysSoAndStaysForCorrecting()
    {
        Rig rig;
        rig.draw({200, 300}, {900, 800});
        const QUuid frame = *rig.session.selectedBrowserView();
        QSignalSpy notices(&rig.canvas, &EditorCanvas::notice);
        QLineEdit *edit = rig.editor();
        QTest::keyClicks(edit, QStringLiteral("file:///etc/passwd"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.front().front().toString(), QStringLiteral("Browser View opens http and https pages only."));
        QVERIFY(rig.object(frame)->browser->url.isEmpty());
        QVERIFY(rig.editor());
    }

    void escapeLeavesTheAddressAsItWas()
    {
        Rig rig;
        rig.add();
        rig.canvas.openAddressEditor(rig.frame);
        QLineEdit *edit = rig.editor();
        QVERIFY(edit);
        QCOMPARE(edit->text(), QStringLiteral("https://example.com/a"));
        QTest::keyClicks(edit, QStringLiteral("x"));
        QTest::keyClick(edit, Qt::Key_Escape);
        QTRY_VERIFY(!rig.editor());
        QCOMPARE(rig.object(rig.frame)->browser->url, QUrl(QStringLiteral("https://example.com/a")));
    }

    void theBarsButtonsAskTheHost()
    {
        Rig rig;
        rig.add();
        rig.host.state.canGoBack = true;
        // The bar sits above the frame: 28 px tall, its buttons 24 px from the left.
        const QPointF corner = rig.view({100, 200});
        const auto press = [&](double x) { QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(int(corner.x() + x), int(corner.y() - 4 - 14))); };
        press(14);
        press(38);
        press(62);
        rig.host.state.loading = true;
        press(62);
        QCOMPARE(rig.host.acts.size(), size_t(4));
        using Action = BrowserViewHost::Action;
        QCOMPARE(rig.host.acts[0].second, Action::back);
        QCOMPARE(rig.host.acts[1].second, Action::forward);
        QCOMPARE(rig.host.acts[2].second, Action::reload);
        QCOMPARE(rig.host.acts[3].second, Action::stop);
        // The bar is not the frame: pressing it doesn't drag the frame.
        QCOMPARE(rig.object(rig.frame)->path.bounds().topLeft(), QPointF(100, 200));
    }

    void aLockedViewsBarStillWorksExceptItsAddress()
    {
        Rig rig;
        rig.add();
        rig.session.setLocked(rig.frame, true);
        rig.host.state.canGoBack = true;
        const QPointF corner = rig.view({100, 200});
        rig.click(QPoint(int(corner.x() + 14), int(corner.y() - 18)));
        QCOMPARE(rig.host.acts.size(), size_t(1));
        rig.click(rig.button(390, rig.host.widths));
        QVERIFY(rig.session.isPreviewOnly());
        rig.click(rig.button(1280, rig.host.widths));
        rig.click(rig.button(1280, rig.host.widths));
        QVERIFY(!rig.session.isPreviewOnly());
        rig.click(QPoint(int(corner.x() + 250), int(corner.y() - 18)));
        QVERIFY(!rig.editor());
    }

    void clickingTheAddressOpensTheEditor()
    {
        Rig rig;
        rig.add();
        const QPointF corner = rig.view({100, 200});
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(int(corner.x() + 250), int(corner.y() - 18)));
        QVERIFY(rig.editor());
        QVERIFY(rig.session.isSelected(rig.frame));
    }

    void aSmallViewShowsTheOrdinaryLabelAndNoButtons()
    {
        Rig rig;
        rig.add({100, 200, 150, 100});
        const QPointF corner = rig.view({100, 200});
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, QPoint(int(corner.x() + 14), int(corner.y() - 18)));
        QVERIFY(rig.host.acts.empty());
    }

    void aSelectedViewOffersItsBreakpointsAndAHoveredOneToo()
    {
        Rig rig;
        rig.add();
        QVERIFY(rig.button(768, rig.host.widths) != QPoint());
        // Nothing selected and the pointer far away: the bar keeps to the address.
        const QImage quiet = rig.canvas.grab().toImage();
        rig.session.select({rig.frame});
        QVERIFY(rig.canvas.grab().toImage() != quiet);
    }

    void aButtonHoldsTheFrameAtItsWidthWithoutAStep()
    {
        Rig rig;
        rig.add();
        rig.session.select({rig.frame});
        const auto steps = rig.session.undoNames();
        const bool modified = rig.session.isModified();
        rig.click(rig.button(390, rig.host.widths));
        QVERIFY(rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 390.0);
        QCOMPARE(rig.object(rig.frame)->path.bounds().topLeft(), QPointF(100, 200));
        rig.click(rig.button(1280, rig.host.widths));
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 1280.0);
        // The same button again ends it, and so does the dotted design width.
        rig.click(rig.button(1280, rig.host.widths));
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);
        rig.click(rig.button(768, rig.host.widths));
        rig.click(rig.button(600, rig.host.widths));
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);
        QVERIFY(rig.session.undoNames() == steps);
        QCOMPARE(rig.session.isModified(), modified);
    }

    void escapeAPressElsewhereAndAnEditEndAHeldPreview()
    {
        Rig rig;
        rig.add();
        rig.session.select({rig.frame});
        rig.click(rig.button(390, rig.host.widths));
        QVERIFY(rig.session.isPreviewOnly());
        QTest::keyClick(&rig.canvas, Qt::Key_Escape);
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);

        rig.click(rig.button(390, rig.host.widths));
        rig.click(rig.view({3000, 2500}).toPoint());
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);

        rig.session.select({rig.frame});
        rig.click(rig.button(390, rig.host.widths));
        rig.session.moveSelection({10, 0});
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);
    }

    void aHeldPreviewSurvivesTheBrowseTool()
    {
        Rig rig;
        rig.add();
        rig.session.select({rig.frame});
        rig.click(rig.button(390, rig.host.widths));
        rig.session.selectTool(Tool::browse);
        QVERIFY(rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 390.0);
        rig.session.selectTool(Tool::select);
        QVERIFY(!rig.session.isPreviewOnly());
    }

    void setAsDesignWidthIsOneStep()
    {
        Rig rig;
        rig.add();
        rig.session.select({rig.frame});
        const int steps = rig.session.undoNames().size();
        rig.click(rig.button(768, rig.host.widths));
        rig.session.setPreviewAsDesignWidth();
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 768.0);
        QCOMPARE(rig.session.undoNames().size(), steps + 1);
        rig.session.undo();
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);
    }

    void draggingAHandleOfABrowserViewIsAPreviewThatEndsOnRelease()
    {
        Rig rig;
        rig.add();
        rig.session.select({rig.frame});
        const auto steps = rig.session.undoNames();
        // The right-middle handle.
        const QPoint handle = rig.view({700, 400}).toPoint();
        QTest::mousePress(&rig.canvas, Qt::LeftButton, Qt::NoModifier, handle);
        QTest::mouseMove(&rig.canvas, handle - QPoint(40, 0));
        QTest::mouseMove(&rig.canvas, handle - QPoint(120, 0));
        QVERIFY(rig.session.isPreviewOnly());
        QVERIFY(rig.object(rig.frame)->path.bounds().width() < 600);
        QTest::mouseRelease(&rig.canvas, Qt::LeftButton, Qt::NoModifier, handle - QPoint(120, 0));
        QVERIFY(!rig.session.isPreviewOnly());
        QCOMPARE(rig.object(rig.frame)->path.bounds().width(), 600.0);
        QVERIFY(rig.session.undoNames() == steps);
    }

    void theSignInStripOffersAndRecordsTheAnswer()
    {
        Rig rig;
        rig.add();
        rig.host.offered = true;
        const QRectF frame = rig.canvas.documentToView().mapRect(QRectF(100, 200, 600, 400));
        // Sign In… is the left one of the two buttons at the strip's right end.
        const QPoint signIn(int(frame.right() - 143), int(frame.bottom() - 28));
        const QPoint notNow(int(frame.right() - 58), int(frame.bottom() - 28));
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, notNow);
        QCOMPARE(rig.host.dismissals, 1);
        QCOMPARE(rig.host.signIns, 0);
        rig.host.offered = true;
        QTest::mouseClick(&rig.canvas, Qt::LeftButton, Qt::NoModifier, signIn);
        QCOMPARE(rig.host.signIns, 1);
        // The strip doesn't take the press for the frame under it.
        QVERIFY(!rig.session.isSelected(rig.frame));
    }

    void theToolIsKnownByName()
    {
        QVERIFY(std::find(allTools.begin(), allTools.end(), Tool::browserView) != allTools.end());
        QCOMPARE(toolNamed(QStringLiteral("browserView")), std::optional<Tool>(Tool::browserView));
    }

    void liveAndTheSignInWindowEachHoldTheProfile()
    {
        BrowserPool::Options options;
        options.profile = QDir(m_directory.path()).filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        options.writeState = false;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);
        Rig rig;
        rig.add();
        BrowserViews *views = BrowserViews::of(rig.session);
        // The sign-in window is up: Live reporting "not running" must not free the profile under it.
        BrowserViews::setSignInWindow(true);
        QTRY_COMPARE(views->state(rig.frame), BrowserViews::State::liveOpen);
        BrowserViews::setLiveOpen(false);
        QCOMPARE(views->state(rig.frame), BrowserViews::State::liveOpen);
        // Live's window is up: Sign In... is not offered, and asking for it does nothing.
        BrowserViews::setLiveOpen(true);
        BrowserViews::setSignInWindow(false);
        QCOMPARE(views->state(rig.frame), BrowserViews::State::liveOpen);
        QVERIFY(!views->signInOffered());
        views->signIn();
        QVERIFY(!BrowserViews::isSigningIn());
        BrowserViews::setLiveOpen(false);
        QTRY_VERIFY(views->state(rig.frame) != BrowserViews::State::liveOpen);
        BrowserViews::setSignInAnswered(false);
        BrowserViews::shutdownPool();
    }

    void signingInStopsTheFramesAndResumesThemWhenTheWindowCloses()
    {
        const QDir directory(m_directory.path());
        // A browser that stays open for a moment, and never draws anything.
        const QString script = directory.filePath(QStringLiteral("chromium"));
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\necho \"$@\" > \"$0.arguments\"\nsleep 1\n");
        file.close();
        QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("OMASTRATOR_CHROMIUM", script.toUtf8());
        BrowserPool::Options options;
        options.profile = directory.filePath(QStringLiteral("profile"));
        options.cache = Browser::Cache::minimal;
        options.writeState = false;
        BrowserViews::setPoolOptions(options);
        BrowserViews::setSignInAnswered(false);

        Rig rig;
        rig.add();
        BrowserViews *views = BrowserViews::of(rig.session);
        QVERIFY(views->signInOffered());
        views->signIn();
        QVERIFY(BrowserViews::isSigningIn());
        QVERIFY(BrowserViews::signInAnswered());
        QVERIFY(!views->signInOffered());
        QTRY_COMPARE(views->state(rig.frame), BrowserViews::State::liveOpen);
        QCOMPARE(views->message(rig.frame), QStringLiteral("Signing in…"));
        // It is the pool's own profile, in a normal window that nothing drives.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(script + QStringLiteral(".arguments")), 5000);
        QFile arguments(script + QStringLiteral(".arguments"));
        QVERIFY(arguments.open(QIODevice::ReadOnly));
        const QString line = QString::fromUtf8(arguments.readAll());
        QVERIFY(line.contains(QStringLiteral("--user-data-dir=") + options.profile));
        QVERIFY(!line.contains(QStringLiteral("--headless")));
        QVERIFY(!line.contains(QStringLiteral("--remote-debugging")));
        QTRY_VERIFY_WITH_TIMEOUT(!BrowserViews::isSigningIn(), 10'000);
        QVERIFY(views->state(rig.frame) != BrowserViews::State::liveOpen);
        BrowserViews::setSignInAnswered(false);
        BrowserViews::shutdownPool();
        qunsetenv("OMASTRATOR_CHROMIUM");
    }
};

QTEST_MAIN(BrowserBarTests)
#include "BrowserBarTests.moc"
