#include "ContentView.h"
#include "UI/BrowserViews.h"
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// The Frame tool's island (docs/WINDOW-LAYOUT.md, row 4): the selected frame's Browser View switch, and while it's on the
// server state and Live controls. No Chromium runs: OMASTRATOR_CHROMIUM names nothing, so a page shows as unavailable.
namespace {
template <typename Widget> Widget &find(QWidget &root, const char *name)
{
    Widget *found = root.findChild<Widget *>(QString::fromLatin1(name));
    if (!found)
        throw std::runtime_error(std::string("no widget named ") + name);
    return *found;
}

struct Editor {
    EditorSession session;
    ContentView view{session};
    Editor()
    {
        session.createDocument(QSizeF(2000, 1500));
        view.resize(1400, 800);
        view.show();
        if (!QTest::qWaitForWindowActive(&view))
            throw std::runtime_error("the editor never became active");
        session.selectTool(Tool::frame);
    }
    QCheckBox &browserSwitch() { return find<QCheckBox>(view, "frameBrowserView"); }
    QWidget &live() { return find<QWidget>(view, "frameLive"); }
};
}

class FrameIslandTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void theSwitchWaitsForExactlyOneFrame();
    void theSwitchFlipsTheFrameInOneUndoStep();
    void theSwitchFollowsOtherDoors();
    void anOnFrameWithAPageShowsItsServerAndLiveControls();

private:
    QTemporaryDir m_config;
};

void FrameIslandTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_config.isValid());
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    // A page never opens here: the island only reads the state.
    qputenv("OMASTRATOR_CHROMIUM", QByteArray("/nonexistent/chromium"));
}

void FrameIslandTests::cleanup()
{
    QSettings().clear();
}

void FrameIslandTests::theSwitchWaitsForExactlyOneFrame()
{
    Editor editor;
    QVERIFY(!editor.browserSwitch().isEnabled());
    QVERIFY(editor.live().isHidden());
    const QUuid first = editor.session.addFrame(QRectF(10, 10, 300, 200));
    QVERIFY(editor.browserSwitch().isEnabled());
    QVERIFY(!editor.browserSwitch().isChecked());
    QVERIFY(editor.live().isHidden());
    const QUuid second = editor.session.addFrame(QRectF(400, 10, 300, 200));
    editor.session.select({first, second});
    QVERIFY(!editor.browserSwitch().isEnabled());
    // Several frames, even one on: no single frame to show Live for.
    editor.session.setBrowserViewOn(first, true);
    QVERIFY(editor.live().isHidden());
    editor.session.select({});
    QVERIFY(!editor.browserSwitch().isEnabled());
    QVERIFY(!editor.browserSwitch().isChecked());
}

void FrameIslandTests::theSwitchFlipsTheFrameInOneUndoStep()
{
    Editor editor;
    const QUuid frame = editor.session.addFrame(QRectF(10, 10, 300, 200));
    const size_t steps = editor.session.undoNames().size();
    editor.browserSwitch().click();
    QVERIFY(editor.session.browserViewOn(frame));
    QVERIFY(editor.browserSwitch().isChecked());
    QCOMPARE(editor.session.undoNames().size(), steps + 1);
    QCOMPARE(editor.session.undoName(), QStringLiteral("Turn On Browser View"));
    // On shows the Live group; a frame with no page yet asks for one first.
    QVERIFY(!editor.live().isHidden());
    QCOMPARE(find<QLabel>(editor.view, "frameServer").text(), QStringLiteral("Type a URL"));
    QVERIFY(!find<QPushButton>(editor.view, "frameEditPage").isEnabled());
    editor.session.undo();
    QVERIFY(!editor.session.browserViewOn(frame));
    QVERIFY(!editor.browserSwitch().isChecked());
    QVERIFY(editor.live().isHidden());
    editor.session.redo();
    QVERIFY(editor.browserSwitch().isChecked());
    editor.session.select({frame});
    editor.browserSwitch().click();
    QVERIFY(!editor.session.browserViewOn(frame));
    QCOMPARE(editor.session.undoName(), QStringLiteral("Turn Off Browser View"));
    QVERIFY(editor.live().isHidden());
}

void FrameIslandTests::theSwitchFollowsOtherDoors()
{
    Editor editor;
    const QUuid frame = editor.session.addFrame(QRectF(10, 10, 300, 200));
    BrowserViews::of(editor.session)->setBrowserViewOn(frame, true);
    QVERIFY(editor.browserSwitch().isChecked());
    QVERIFY(!editor.live().isHidden());
    editor.session.setBrowserViewOn(frame, false);
    QVERIFY(!editor.browserSwitch().isChecked());
    QVERIFY(editor.live().isHidden());
    // A locked frame keeps its switch where it is.
    editor.session.setLocked(frame, true);
    editor.session.select({frame});
    QVERIFY(!editor.browserSwitch().isEnabled());
}

void FrameIslandTests::anOnFrameWithAPageShowsItsServerAndLiveControls()
{
    Editor editor;
    // Opening or adding a view with a page starts no server: the island says so.
    const QUuid frame = editor.session.addBrowserView(QRectF(10, 10, 640, 400), QUrl(QStringLiteral("https://example.com/")));
    editor.session.select({frame});
    QVERIFY(editor.browserSwitch().isChecked());
    QVERIFY(!editor.live().isHidden());
    QCOMPARE(find<QLabel>(editor.view, "frameServer").text(), QStringLiteral("No server"));
    QVERIFY(find<QPushButton>(editor.view, "frameEditPage").isEnabled());
    QVERIFY(!find<QPushButton>(editor.view, "frameStopLive").isEnabled());
    for (const char *name : {"frameReload", "frameChanges", "frameHistory", "frameDeploy"})
        QVERIFY2(!find<QPushButton>(editor.view, name).isHidden(), name);
    // The help gives way to the Live controls, so the island stays one row.
    QVERIFY(find<QPushButton>(editor.view, "frameDeploy").y() == find<QPushButton>(editor.view, "frameReload").y());
    editor.browserSwitch().click();
    QVERIFY(!editor.session.browserViewOn(frame));
    QVERIFY(editor.live().isHidden());
}

QTEST_MAIN(FrameIslandTests)
#include "FrameIslandTests.moc"
